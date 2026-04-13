using System;
using System.Collections.Generic;
using System.Numerics;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

/// <summary>
/// Renders grid lines, axes, and stock box as GL_LINES using the line shader program.
/// </summary>
internal sealed class GridRenderer : IDisposable
{
    private readonly GL _gl;
    private uint _vao;
    private uint _vbo;
    private int _vertexCount;

    public GridRenderer(GL gl)
    {
        _gl = gl;
    }

    public unsafe void Initialize()
    {
        _vao = _gl.GenVertexArray();
        _vbo = _gl.GenBuffer();

        _gl.BindVertexArray(_vao);
        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _vbo);

        uint stride = 8 * sizeof(float); // pos(3) + color(4) + category(1) - same layout as LineRenderer

        _gl.EnableVertexAttribArray(0);
        _gl.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, stride, (void*)0);

        _gl.EnableVertexAttribArray(1);
        _gl.VertexAttribPointer(1, 4, VertexAttribPointerType.Float, false, stride, (void*)(3 * sizeof(float)));

        _gl.EnableVertexAttribArray(2);
        _gl.VertexAttribPointer(2, 1, VertexAttribPointerType.Float, false, stride, (void*)(7 * sizeof(float)));

        _gl.BindVertexArray(0);
    }

    public unsafe void BuildGeometry(
        double minX, double maxX,
        double minY, double maxY,
        double minZ, double maxZ,
        bool showGrid,
        bool showStockBox)
    {
        var verts = new List<float>(256);
        const float gridAlpha = 0.22f;
        const float axisAlpha = 1.00f;
        const float stockAlpha = 0.70f;

        if (showGrid)
        {
            // XY grid at Z=0, spanning document bounds + padding
            float gMinX = (float)(minX - 20);
            float gMaxX = (float)(maxX + 20);
            float gMinY = (float)(minY - 20);
            float gMaxY = (float)(maxY + 20);

            float step = ComputeGridStep((float)(maxX - minX), (float)(maxY - minY));
            float startX = MathF.Floor(gMinX / step) * step;
            float startY = MathF.Floor(gMinY / step) * step;

            for (float x = startX; x <= gMaxX; x += step)
            {
                AddLine(verts, x, gMinY, 0, x, gMaxY, 0, RenderCategories.Grid, gridAlpha);
            }
            for (float y = startY; y <= gMaxY; y += step)
            {
                AddLine(verts, gMinX, y, 0, gMaxX, y, 0, RenderCategories.Grid, gridAlpha);
            }

            // Make the origin axes read clearly by spanning the visible work envelope.
            float axisMinX = MathF.Min(gMinX, 0f);
            float axisMaxX = MathF.Max(gMaxX, 0f);
            float axisMinY = MathF.Min(gMinY, 0f);
            float axisMaxY = MathF.Max(gMaxY, 0f);
            float axisLift = MathF.Max(step * 0.02f, 0.05f);
            float zMin = MathF.Min((float)minZ - 10f, 0f);
            float zMax = MathF.Max((float)maxZ + 10f, 20f);
            AddLine(verts, axisMinX, 0, axisLift, axisMaxX, 0, axisLift, RenderCategories.AxisX, axisAlpha);
            AddLine(verts, 0, axisMinY, axisLift, 0, axisMaxY, axisLift, RenderCategories.AxisY, axisAlpha);
            AddLine(verts, 0, 0, zMin, 0, 0, zMax, RenderCategories.AxisZ, axisAlpha);
        }

        if (showStockBox)
        {
            float sx0 = (float)minX, sx1 = (float)maxX;
            float sy0 = (float)minY, sy1 = (float)maxY;
            float sz0 = (float)minZ, sz1 = (float)maxZ;

            // Bottom face
            AddLine(verts, sx0, sy0, sz0, sx1, sy0, sz0, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy0, sz0, sx1, sy1, sz0, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy1, sz0, sx0, sy1, sz0, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx0, sy1, sz0, sx0, sy0, sz0, RenderCategories.Stock, stockAlpha);
            // Top face
            AddLine(verts, sx0, sy0, sz1, sx1, sy0, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy0, sz1, sx1, sy1, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy1, sz1, sx0, sy1, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx0, sy1, sz1, sx0, sy0, sz1, RenderCategories.Stock, stockAlpha);
            // Verticals
            AddLine(verts, sx0, sy0, sz0, sx0, sy0, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy0, sz0, sx1, sy0, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx1, sy1, sz0, sx1, sy1, sz1, RenderCategories.Stock, stockAlpha);
            AddLine(verts, sx0, sy1, sz0, sx0, sy1, sz1, RenderCategories.Stock, stockAlpha);
        }

        _vertexCount = verts.Count / 8;

        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _vbo);
        if (_vertexCount > 0)
        {
            float[] arr = verts.ToArray();
            fixed (float* ptr = arr)
                _gl.BufferData(BufferTargetARB.ArrayBuffer, (nuint)(arr.Length * sizeof(float)), ptr, BufferUsageARB.DynamicDraw);
        }
        else
        {
            _gl.BufferData(BufferTargetARB.ArrayBuffer, 0, null, BufferUsageARB.DynamicDraw);
        }
    }

    public unsafe void Draw(uint program, float[] viewProj, bool isDark)
    {
        if (_vertexCount == 0) return;

        _gl.UseProgram(program);

        int loc = _gl.GetUniformLocation(program, "uViewProj");
        fixed (float* ptr = viewProj)
            _gl.UniformMatrix4(loc, 1, false, ptr);

        _gl.Uniform1(_gl.GetUniformLocation(program, "uIsDark"), isDark ? 1 : 0);
        // Grid is always "completed" so everything is full brightness
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCompletedSegmentCount"), int.MaxValue);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCategoryMask"), uint.MaxValue);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uDimAlpha"), 1.0f);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowCompleted"), 1);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowRemaining"), 1);

        _gl.LineWidth(2.0f);
        _gl.BindVertexArray(_vao);
        _gl.DrawArrays(PrimitiveType.Lines, 0, (uint)_vertexCount);
        _gl.BindVertexArray(0);
        _gl.LineWidth(1.0f);
    }

    private static void AddLine(List<float> verts,
        float x0, float y0, float z0,
        float x1, float y1, float z1,
        float category,
        float alpha)
    {
        // v0
        verts.Add(x0); verts.Add(y0); verts.Add(z0);
        verts.Add(0f); verts.Add(0f); verts.Add(0f); verts.Add(alpha);
        verts.Add(category);
        // v1
        verts.Add(x1); verts.Add(y1); verts.Add(z1);
        verts.Add(0f); verts.Add(0f); verts.Add(0f); verts.Add(alpha);
        verts.Add(category);
    }

    private static float ComputeGridStep(float spanX, float spanY)
    {
        float maxSpan = MathF.Max(spanX, spanY);
        if (maxSpan <= 0) return 10f;
        // Pick a nice round step
        float raw = maxSpan / 12f;
        float pow = MathF.Pow(10, MathF.Floor(MathF.Log10(raw)));
        float norm = raw / pow;
        float step = norm < 2 ? 1 * pow : norm < 5 ? 2 * pow : 5 * pow;
        return MathF.Max(step, 1f);
    }

    public void Dispose()
    {
        _gl.DeleteVertexArray(_vao);
        _gl.DeleteBuffer(_vbo);
    }
}

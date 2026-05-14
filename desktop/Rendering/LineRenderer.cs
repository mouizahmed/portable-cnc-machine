using System;
using System.Numerics;
using PortableCncApp.Services.GCode;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

/// <summary>
/// Renders toolpath segments as GL_LINES.
/// Geometry is uploaded once per document. Progress and visibility changes are cheap uniform updates.
/// </summary>
internal sealed class LineRenderer : IDisposable
{
    private readonly GL _gl;
    private uint _vao;
    private uint _vbo;
    private int _totalSegmentCount;
    private int[] _segmentSourceLines = Array.Empty<int>(); // parallel to segment order for binary search

    // Vertex layout: position(3) + color(4) + category(1) = 8 floats = 32 bytes
    private const int FloatsPerVertex = 8;
    private const int BytesPerVertex = FloatsPerVertex * sizeof(float);

    public int TotalSegmentCount => _totalSegmentCount;

    public LineRenderer(GL gl)
    {
        _gl = gl;
    }

    public unsafe void Initialize()
    {
        _vao = _gl.GenVertexArray();
        _vbo = _gl.GenBuffer();

        _gl.BindVertexArray(_vao);
        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _vbo);

        // position (location 0)
        _gl.EnableVertexAttribArray(0);
        _gl.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, (uint)BytesPerVertex, (void*)0);

        // color (location 1)
        _gl.EnableVertexAttribArray(1);
        _gl.VertexAttribPointer(1, 4, VertexAttribPointerType.Float, false, (uint)BytesPerVertex, (void*)(3 * sizeof(float)));

        // category (location 2)
        _gl.EnableVertexAttribArray(2);
        _gl.VertexAttribPointer(2, 1, VertexAttribPointerType.Float, false, (uint)BytesPerVertex, (void*)(7 * sizeof(float)));

        _gl.BindVertexArray(0);
    }

    public unsafe void UploadGeometry(GCodeDocument? document)
    {
        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _vbo);

        if (document == null || !document.HasGeometry)
        {
            _totalSegmentCount = 0;
            _segmentSourceLines = Array.Empty<int>();
            _gl.BufferData(BufferTargetARB.ArrayBuffer, 0, null, BufferUsageARB.StaticDraw);
            return;
        }

        var segments = document.Segments;
        _totalSegmentCount = segments.Length;
        _segmentSourceLines = new int[segments.Length];

        float[] data = new float[segments.Length * 2 * FloatsPerVertex];
        int di = 0;

        for (int i = 0; i < segments.Length; i++)
        {
            var seg = segments[i];
            _segmentSourceLines[i] = seg.SourceLine;

            GetCategoryAndAlpha(seg, out float cat, out float alpha);

            // start vertex
            data[di++] = (float)seg.Start.X;
            data[di++] = (float)seg.Start.Y;
            data[di++] = (float)seg.Start.Z;
            data[di++] = 0f; data[di++] = 0f; data[di++] = 0f; data[di++] = alpha;
            data[di++] = cat;

            // end vertex
            data[di++] = (float)seg.End.X;
            data[di++] = (float)seg.End.Y;
            data[di++] = (float)seg.End.Z;
            data[di++] = 0f; data[di++] = 0f; data[di++] = 0f; data[di++] = alpha;
            data[di++] = cat;
        }

        fixed (float* ptr = data)
        {
            _gl.BufferData(BufferTargetARB.ArrayBuffer, (nuint)(data.Length * sizeof(float)), ptr, BufferUsageARB.StaticDraw);
        }
    }

    public void Draw(
        uint program,
        float[] viewProj,
        bool isDark,
        int completedSegmentCount,
        bool showRapids,
        bool showCuts,
        bool showArcs,
        bool showPlunges,
        bool showCompleted,
        bool showRemaining)
    {
        if (_totalSegmentCount == 0) return;

        _gl.UseProgram(program);

        SetMatrix(program, "uViewProj", viewProj);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uIsDark"), isDark ? 1 : 0);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCompletedSegmentCount"), completedSegmentCount);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uDimAlpha"), 0.22f);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowCompleted"), showCompleted ? 1 : 0);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowRemaining"), showRemaining ? 1 : 0);

        uint mask = 0u;
        if (showRapids)  mask |= 1u;       // bit 0
        if (showCuts)    mask |= 2u;       // bit 1
        if (showArcs)    mask |= 4u;       // bit 2
        if (showPlunges) mask |= 8u;       // bit 3
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCategoryMask"), mask);

        _gl.BindVertexArray(_vao);
        _gl.DrawArrays(PrimitiveType.Lines, 0, (uint)(_totalSegmentCount * 2));
        _gl.BindVertexArray(0);
    }

    public void DrawPoints(
        uint program,
        float[] viewProj,
        bool isDark,
        int completedSegmentCount,
        bool showRapids,
        bool showCuts,
        bool showArcs,
        bool showPlunges,
        bool showCompleted,
        bool showRemaining)
    {
        if (_totalSegmentCount == 0) return;

        _gl.UseProgram(program);

        SetMatrix(program, "uViewProj", viewProj);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uIsDark"), isDark ? 1 : 0);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCompletedSegmentCount"), completedSegmentCount);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uDimAlpha"), 0.45f);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowCompleted"), showCompleted ? 1 : 0);
        _gl.Uniform1(_gl.GetUniformLocation(program, "uShowRemaining"), showRemaining ? 1 : 0);

        uint mask = 0u;
        if (showRapids)  mask |= 1u;
        if (showCuts)    mask |= 2u;
        if (showArcs)    mask |= 4u;
        if (showPlunges) mask |= 8u;
        _gl.Uniform1(_gl.GetUniformLocation(program, "uCategoryMask"), mask);

        _gl.PointSize(2.5f);
        _gl.BindVertexArray(_vao);
        _gl.DrawArrays(PrimitiveType.Points, 0, (uint)(_totalSegmentCount * 2));
        _gl.BindVertexArray(0);
    }

    /// <summary>Returns the number of segments whose SourceLine &lt;= currentLine.</summary>
    public int ComputeCompletedCount(int currentLine)
    {
        if (_segmentSourceLines.Length == 0) return 0;
        int lo = 0, hi = _segmentSourceLines.Length - 1, result = 0;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (_segmentSourceLines[mid] <= currentLine)
            {
                result = mid + 1;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return result;
    }

    /// <summary>Returns the position of the last segment endpoint at or before currentLine.</summary>
    public Vector3 GetMarkerPosition(int currentLine)
    {
        if (_segmentSourceLines.Length == 0) return Vector3.Zero;
        int completed = ComputeCompletedCount(currentLine);
        if (completed == 0) return Vector3.Zero;

        // We need the segment to look up its End, but we only stored SourceLines.
        // This is called rarely so we can store the end positions separately.
        // For now return Zero - MarkerRenderer will use a stored position array.
        return Vector3.Zero;
    }

    private unsafe void SetMatrix(uint program, string name, float[] mat)
    {
        int loc = _gl.GetUniformLocation(program, name);
        fixed (float* ptr = mat)
            _gl.UniformMatrix4(loc, 1, false, ptr);
    }

    private static void GetCategoryAndAlpha(ToolpathSegment seg, out float category, out float alpha)
    {
        if (seg.IsRapid)
        {
            category = RenderCategories.Rapid;
        }
        else if (seg.IsArc)
        {
            category = RenderCategories.Arc;
        }
        else if (seg.IsPlungeOrRetract)
        {
            category = RenderCategories.Plunge;
        }
        else
        {
            category = RenderCategories.Cut;
        }

        alpha = 1f;
    }

    public void Dispose()
    {
        _gl.DeleteVertexArray(_vao);
        _gl.DeleteBuffer(_vbo);
    }
}

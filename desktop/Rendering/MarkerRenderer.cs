using System;
using System.Numerics;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

/// <summary>
/// Renders a small tool position marker using the mesh shader.
/// Uses a simple diamond (octahedron) shape.
/// </summary>
internal sealed class MarkerRenderer : IDisposable
{
    private readonly GL _gl;
    private uint _vao;
    private uint _vbo;
    private uint _ebo;
    private int _indexCount;

    public MarkerRenderer(GL gl)
    {
        _gl = gl;
    }

    public unsafe void Initialize()
    {
        _vao = _gl.GenVertexArray();
        _vbo = _gl.GenBuffer();
        _ebo = _gl.GenBuffer();

        // Octahedron: 6 vertices, 8 faces
        float r = 2.5f; // radius
        float h = 4.0f; // height top/bottom
        float[] vertices = new float[]
        {
            //   position          normal (will be computed per face, but we use face normals via duplicate verts)
            //  x      y    z
             0,  h,   0,    // 0: top
             r,  0,   0,    // 1: right
             0,  0,   r,    // 2: front
            -r,  0,   0,    // 3: left
             0,  0,  -r,    // 4: back
             0, -h,   0,    // 5: bottom
        };

        // 8 triangular faces (indices)
        uint[] indices = new uint[]
        {
            0,1,2,  0,2,3,  0,3,4,  0,4,1,  // upper hemisphere
            5,2,1,  5,3,2,  5,4,3,  5,1,4   // lower hemisphere
        };

        _indexCount = indices.Length;

        _gl.BindVertexArray(_vao);

        _gl.BindBuffer(BufferTargetARB.ArrayBuffer, _vbo);
        fixed (float* p = vertices)
            _gl.BufferData(BufferTargetARB.ArrayBuffer, (nuint)(vertices.Length * sizeof(float)), p, BufferUsageARB.StaticDraw);

        _gl.BindBuffer(BufferTargetARB.ElementArrayBuffer, _ebo);
        fixed (uint* p = indices)
            _gl.BufferData(BufferTargetARB.ElementArrayBuffer, (nuint)(indices.Length * sizeof(uint)), p, BufferUsageARB.StaticDraw);

        // position (location 0) - 3 floats, stride 3 floats
        _gl.EnableVertexAttribArray(0);
        _gl.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 3 * sizeof(float), (void*)0);

        // We will pass normals as a uniform for simplicity (use view-space up)
        // location 1 not needed for this simple marker

        _gl.BindVertexArray(0);
    }

    public unsafe void Draw(uint program, float[] viewProj, Vector3 position, bool isDark)
    {
        _gl.UseProgram(program);

        int vpLoc = _gl.GetUniformLocation(program, "uViewProj");
        fixed (float* ptr = viewProj)
            _gl.UniformMatrix4(vpLoc, 1, false, ptr);

        float[] model = GlMath.CreateTranslation(position.X, position.Y, position.Z);
        int modelLoc = _gl.GetUniformLocation(program, "uModel");
        fixed (float* ptr = model)
            _gl.UniformMatrix4(modelLoc, 1, false, ptr);

        float mr = isDark ? 0.95f : 0.8f;
        float mg = isDark ? 0.3f  : 0.1f;
        float mb = isDark ? 0.3f  : 0.05f;
        _gl.Uniform4(_gl.GetUniformLocation(program, "uColor"), mr, mg, mb, 1.0f);

        _gl.BindVertexArray(_vao);
        _gl.DrawElements(PrimitiveType.Triangles, (uint)_indexCount, DrawElementsType.UnsignedInt, (void*)0);
        _gl.BindVertexArray(0);
    }

    public void Dispose()
    {
        _gl.DeleteVertexArray(_vao);
        _gl.DeleteBuffer(_vbo);
        _gl.DeleteBuffer(_ebo);
    }
}

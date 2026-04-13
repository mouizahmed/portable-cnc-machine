using System;
using System.Reflection;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

internal static class GlShaderHelper
{
    public static uint CompileProgram(GL gl, string vertResourceName, string fragResourceName)
    {
        string vertSrc = LoadEmbeddedResource(vertResourceName);
        string fragSrc = LoadEmbeddedResource(fragResourceName);

        uint vert = CompileShader(gl, ShaderType.VertexShader, vertSrc);
        uint frag = CompileShader(gl, ShaderType.FragmentShader, fragSrc);

        uint program = gl.CreateProgram();
        gl.AttachShader(program, vert);
        gl.AttachShader(program, frag);
        gl.LinkProgram(program);

        gl.GetProgram(program, ProgramPropertyARB.LinkStatus, out int linked);
        if (linked == 0)
        {
            string log = gl.GetProgramInfoLog(program);
            gl.DeleteProgram(program);
            gl.DeleteShader(vert);
            gl.DeleteShader(frag);
            throw new InvalidOperationException($"Shader link failed: {log}");
        }

        gl.DetachShader(program, vert);
        gl.DetachShader(program, frag);
        gl.DeleteShader(vert);
        gl.DeleteShader(frag);

        return program;
    }

    private static uint CompileShader(GL gl, ShaderType type, string source)
    {
        uint shader = gl.CreateShader(type);
        gl.ShaderSource(shader, source);
        gl.CompileShader(shader);

        gl.GetShader(shader, ShaderParameterName.CompileStatus, out int compiled);
        if (compiled == 0)
        {
            string log = gl.GetShaderInfoLog(shader);
            gl.DeleteShader(shader);
            throw new InvalidOperationException($"Shader compile failed ({type}): {log}");
        }

        return shader;
    }

    private static string LoadEmbeddedResource(string name)
    {
        var asm = Assembly.GetExecutingAssembly();
        using var stream = asm.GetManifestResourceStream(name)
            ?? throw new InvalidOperationException($"Embedded resource not found: {name}");
        using var reader = new System.IO.StreamReader(stream);
        return reader.ReadToEnd();
    }
}

using System;
using System.Numerics;

namespace PortableCncApp.Rendering;

/// <summary>
/// Column-major OpenGL matrix helpers. All matrices are float[16] in column-major order
/// as expected by glUniformMatrix4fv with transpose=false.
/// </summary>
internal static class GlMath
{
    public static float[] Identity() => new float[]
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    /// <summary>Right-handed perspective projection for OpenGL NDC z ∈ [-1, 1].</summary>
    public static float[] CreatePerspective(float fovYRadians, float aspect, float near, float far)
    {
        float f = 1f / MathF.Tan(fovYRadians * 0.5f);
        float[] m = new float[16];
        m[0]  = f / aspect;
        m[5]  = f;
        m[10] = -(far + near) / (far - near);
        m[11] = -1f;
        m[14] = -(2f * far * near) / (far - near);
        return m;
    }

    /// <summary>Right-handed look-at view matrix.</summary>
    public static float[] CreateLookAt(Vector3 eye, Vector3 target, Vector3 up)
    {
        Vector3 f = Vector3.Normalize(target - eye); // forward
        Vector3 s = Vector3.Normalize(Vector3.Cross(f, up)); // right
        Vector3 u = Vector3.Cross(s, f); // true up

        float[] m = new float[16];
        // column 0
        m[0] = s.X;  m[1] = u.X;  m[2] = -f.X; m[3] = 0f;
        // column 1
        m[4] = s.Y;  m[5] = u.Y;  m[6] = -f.Y; m[7] = 0f;
        // column 2
        m[8]  = s.Z; m[9]  = u.Z; m[10] = -f.Z; m[11] = 0f;
        // column 3
        m[12] = -Vector3.Dot(s, eye);
        m[13] = -Vector3.Dot(u, eye);
        m[14] =  Vector3.Dot(f, eye);
        m[15] = 1f;
        return m;
    }

    /// <summary>Column-major matrix multiplication: returns A * B.</summary>
    public static float[] Multiply(float[] a, float[] b)
    {
        float[] c = new float[16];
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                for (int k = 0; k < 4; k++)
                    c[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
        return c;
    }

    public static float[] CreateTranslation(float x, float y, float z)
    {
        float[] m = Identity();
        m[12] = x;
        m[13] = y;
        m[14] = z;
        return m;
    }

    public static float[] CreateScale(float s) => CreateScale(s, s, s);

    public static float[] CreateScale(float x, float y, float z)
    {
        float[] m = new float[16];
        m[0] = x; m[5] = y; m[10] = z; m[15] = 1f;
        return m;
    }
}

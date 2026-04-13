using System;
using System.Numerics;

namespace PortableCncApp.Rendering;

/// <summary>
/// Orbit camera with Z-up convention matching CNC / G-code coordinate space.
/// This uses a rotated camera frame instead of a fixed world-up look-at so the
/// viewer can rotate above and below the workplane without sudden flips.
/// </summary>
internal sealed class OrbitCamera
{
    private float _azimuth = MathF.PI * 0.4f;
    private float _elevation = -MathF.PI * 0.25f;
    private float _distance = 300f;
    private Vector3 _target = Vector3.Zero;

    private const float FovY = MathF.PI / 4f;
    private const float Near = 0.5f;
    private const float Far = 10000f;
    private const float MinElevation = -MathF.PI + 0.05f;
    private const float MaxElevation = MathF.PI - 0.05f;

    public float[] GetViewProjection(float aspectRatio)
    {
        var eye = ComputeEye();
        var view = GlMath.CreateLookAt(eye, _target, ComputeUp());
        var proj = GlMath.CreatePerspective(FovY, aspectRatio, Near, Far);
        return GlMath.Multiply(proj, view);
    }

    public void Orbit(float deltaX, float deltaY)
    {
        _azimuth -= deltaX * 0.005f;
        _elevation = Math.Clamp(_elevation - deltaY * 0.005f, MinElevation, MaxElevation);
    }

    public void Pan(float deltaX, float deltaY, float viewportWidth, float viewportHeight)
    {
        var eye = ComputeEye();
        var forward = Vector3.Normalize(_target - eye);
        var up = ComputeUp();
        var right = Vector3.Normalize(Vector3.Cross(forward, up));
        up = Vector3.Normalize(Vector3.Cross(right, forward));

        float scale = _distance / MathF.Max(viewportHeight, 1f);
        _target -= right * (deltaX * scale);
        _target += up * (deltaY * scale);
    }

    public void Zoom(float delta)
    {
        _distance = Math.Clamp(_distance * (1f + delta), 1f, 9000f);
    }

    public void FitToBounds(float minX, float maxX, float minY, float maxY, float minZ, float maxZ, float aspectRatio)
    {
        _target = new Vector3(
            (minX + maxX) * 0.5f,
            (minY + maxY) * 0.5f,
            (minZ + maxZ) * 0.5f);

        float sizeX = MathF.Max(maxX - minX, 10f);
        float sizeY = MathF.Max(maxY - minY, 10f);
        float sizeZ = MathF.Max(maxZ - minZ, 10f);

        float safeAspect = MathF.Max(aspectRatio, 0.1f);
        float horizontalFov = 2f * MathF.Atan(MathF.Tan(FovY * 0.5f) * safeAspect);

        float widthDistance = (sizeX * 0.5f) / MathF.Tan(horizontalFov * 0.5f);
        float heightDistance = (MathF.Max(sizeY, sizeZ) * 0.5f) / MathF.Tan(FovY * 0.5f);
        float radiusDistance = new Vector3(sizeX, sizeY, sizeZ).Length() * 0.5f;

        _distance = MathF.Max(MathF.Max(widthDistance, heightDistance), radiusDistance) * 1.85f;
        _distance = MathF.Max(_distance, 95f);
    }

    public void SetPreset(string preset)
    {
        switch (preset)
        {
            case "Top":
                _azimuth = 0f;
                _elevation = -MathF.PI * 0.5f + 0.01f;
                break;
            case "Front":
                _azimuth = 0f;
                _elevation = 0f;
                break;
            case "Side":
                _azimuth = MathF.PI * 0.5f;
                _elevation = 0f;
                break;
            default:
                _azimuth = MathF.PI * 0.4f;
                _elevation = -MathF.PI * 0.25f;
                break;
        }
    }

    private Vector3 ComputeEye()
    {
        var rotation = ComputeRotation();
        var offset = Vector3.Transform(new Vector3(0f, _distance, 0f), rotation);
        return _target + offset;
    }

    private Vector3 ComputeUp()
    {
        return Vector3.Normalize(Vector3.Transform(Vector3.UnitZ, ComputeRotation()));
    }

    private Quaternion ComputeRotation()
    {
        var yaw = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, _azimuth);
        var rightAxis = Vector3.Transform(Vector3.UnitX, yaw);
        var pitch = Quaternion.CreateFromAxisAngle(rightAxis, -_elevation);
        return Quaternion.Normalize(pitch * yaw);
    }
}

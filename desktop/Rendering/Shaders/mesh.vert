#version 300 es

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec4 uColor;

out vec3 vNormal;
out vec4 vColor;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vNormal = aNormal;
    vColor = uColor;
}

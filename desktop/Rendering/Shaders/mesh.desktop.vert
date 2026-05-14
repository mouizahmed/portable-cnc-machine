#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec4 uColor;

out vec3 vNormal;
out vec4 vColor;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vNormal = normalize(aPosition);
    vColor = uColor;
}

#version 330 core

in vec3 vNormal;
in vec4 vColor;
out vec4 FragColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.75));
    float diff = max(dot(normalize(vNormal), lightDir), 0.15);
    FragColor = vec4(vColor.rgb * diff, vColor.a);
}

#version 300 es

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aCategory;

uniform mat4 uViewProj;
uniform int uIsDark;
uniform int uCompletedSegmentCount;
uniform uint uCategoryMask;
uniform float uDimAlpha;
uniform int uShowCompleted;
uniform int uShowRemaining;

out vec4 vColor;

vec3 resolveCategoryColor(uint cat)
{
    bool isDark = uIsDark != 0;

    if (cat == 0u) return isDark ? vec3(1.0, 0.75, 0.0) : vec3(0.8, 0.55, 0.0);
    if (cat == 1u) return isDark ? vec3(0.15, 0.8, 1.0) : vec3(0.0, 0.35, 0.85);
    if (cat == 2u) return isDark ? vec3(0.2, 0.9, 0.4) : vec3(0.0, 0.55, 0.15);
    if (cat == 3u) return isDark ? vec3(1.0, 0.3, 0.15) : vec3(0.75, 0.1, 0.0);
    if (cat == 4u) return isDark ? vec3(0.5, 0.5, 0.5) : vec3(0.55, 0.55, 0.55);
    if (cat == 5u) return vec3(0.9, 0.2, 0.2);
    if (cat == 6u) return vec3(0.2, 0.8, 0.3);
    if (cat == 7u) return vec3(0.3, 0.4, 1.0);
    if (cat == 8u) return isDark ? vec3(0.85, 0.5, 0.2) : vec3(0.3, 0.2, 0.1);
    return aColor.rgb;
}

void main()
{
    gl_Position = uViewProj * vec4(aPosition, 1.0);

    int segIdx = gl_VertexID / 2;
    bool isCompleted = segIdx < uCompletedSegmentCount;

    uint cat = uint(aCategory + 0.5);
    bool isCategoryVisible = (uCategoryMask & (1u << cat)) != 0u;

    bool segVisible = isCategoryVisible &&
                      ((isCompleted && uShowCompleted != 0) ||
                       (!isCompleted && uShowRemaining != 0));

    float alpha = segVisible ? aColor.a : 0.0;
    vColor = vec4(resolveCategoryColor(cat), alpha);
}

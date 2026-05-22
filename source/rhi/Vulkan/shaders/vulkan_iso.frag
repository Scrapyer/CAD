#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform IsoPushConstants {
    mat4 mvp;
    vec4 color;
} pc;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.35, -0.65, 0.62));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 baseColor = pc.color.rgb;
    if (baseColor.r + baseColor.g + baseColor.b <= 0.0) {
        baseColor = vColor;
    }
    vec3 color = baseColor * (0.35 + diffuse * 0.65);
    outColor = vec4(color, pc.color.a);
}

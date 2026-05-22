#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vScalar;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform MeshPushConstants {
    mat4 mvp;
    vec4 contour;
} pc;

vec3 jetColor(float t)
{
    t = clamp(t, 0.0, 1.0);
    float r = 0.0;
    float g = 0.0;
    float b = 0.0;
    if (t < 0.125) {
        r = 0.0;
        g = 0.0;
        b = 0.5 + t / 0.125 * 0.5;
    } else if (t < 0.375) {
        r = 0.0;
        g = (t - 0.125) / 0.25;
        b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) / 0.25;
        g = 1.0;
        b = 1.0 - (t - 0.375) / 0.25;
    } else if (t < 0.875) {
        r = 1.0;
        g = 1.0 - (t - 0.625) / 0.25;
        b = 0.0;
    } else {
        r = 1.0 - (t - 0.875) / 0.125 * 0.5;
        g = 0.0;
        b = 0.0;
    }
    return vec3(r, g, b);
}

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.35, -0.65, 0.62));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 baseColor = vColor;
    if (pc.contour.w > 0.5) {
        float scalarMin = pc.contour.x;
        float scalarMax = pc.contour.y;
        int numBands = max(1, int(pc.contour.z + 0.5));
        float range = scalarMax - scalarMin;
        float t = (range > 1.0e-10) ? clamp((vScalar - scalarMin) / range, 0.0, 1.0) : 0.5;
        int band = int(t * float(numBands));
        if (band >= numBands) {
            band = numBands - 1;
        }
        float qt = (float(band) + 0.5) / float(numBands);
        baseColor = jetColor(qt);
    }
    vec3 color = baseColor * (0.35 + diffuse * 0.65);
    outColor = vec4(color, 1.0);
}

#version 450

layout(location = 0) in float vScalar;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform LinePushConstants {
    mat4 mvp;
    vec4 color;
    vec4 contour;
} pc;

vec3 jetColor(float t)
{
    t = clamp(t, 0.0, 1.0);
    float r = 0.0;
    float g = 0.0;
    float b = 0.0;
    if (t < 0.125) {
        b = 0.5 + t / 0.125 * 0.5;
    } else if (t < 0.375) {
        g = (t - 0.125) / 0.25;
        b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) / 0.25;
        g = 1.0;
        b = 1.0 - (t - 0.375) / 0.25;
    } else if (t < 0.875) {
        r = 1.0;
        g = 1.0 - (t - 0.625) / 0.25;
    } else {
        r = 1.0 - (t - 0.875) / 0.125 * 0.5;
    }
    return vec3(r, g, b);
}

void main()
{
    vec3 baseColor = pc.color.rgb;
    if (pc.contour.w > 0.5) {
        float range = pc.contour.y - pc.contour.x;
        float t = (range > 1.0e-10) ? clamp((vScalar - pc.contour.x) / range, 0.0, 1.0) : 0.5;
        int numBands = max(1, int(pc.contour.z + 0.5));
        int band = int(t * float(numBands));
        if (band >= numBands) {
            band = numBands - 1;
        }
        float qt = (float(band) + 0.5) / float(numBands);
        baseColor = jetColor(qt);
    }
    outColor = vec4(baseColor, pc.color.a);
}

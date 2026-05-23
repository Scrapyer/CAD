#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in float vScalar;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uWireframe;
uniform float uWireAlpha;
uniform float uSurfaceAlpha;
uniform bool uUseVertexColor;
uniform bool uContourMode;
uniform float uScalarMin;
uniform float uScalarMax;
uniform int uNumBands;
uniform samplerBuffer uTriPartMap;
out vec4 outColor;

// 部件调色板（与 C++ 端 kPartPalette 一致）
const vec3 kPalette[8] = vec3[8](
    vec3(0.61, 0.86, 0.63),
    vec3(0.54, 0.71, 0.98),
    vec3(0.98, 0.70, 0.53),
    vec3(0.82, 0.62, 0.98),
    vec3(0.58, 0.89, 0.83),
    vec3(0.98, 0.89, 0.69),
    vec3(0.94, 0.56, 0.66),
    vec3(0.71, 0.71, 0.98)
);

vec3 jetColor(float t) {
    t = clamp(t, 0.0, 1.0);
    float r, g, b;
    if (t < 0.125)      { r = 0.0; g = 0.0; b = 0.5 + t/0.125*0.5; }
    else if (t < 0.375) { r = 0.0; g = (t-0.125)/0.25; b = 1.0; }
    else if (t < 0.625) { r = (t-0.375)/0.25; g = 1.0; b = 1.0-(t-0.375)/0.25; }
    else if (t < 0.875) { r = 1.0; g = 1.0-(t-0.625)/0.25; b = 0.0; }
    else                 { r = 1.0-(t-0.875)/0.125*0.5; g = 0.0; b = 0.0; }
    return vec3(r, g, b);
}

void main() {
    vec3 surfaceColor = uUseVertexColor ? vColor : uColor;

    if (uContourMode) {
        // 云图模式：标量值量化 + Jet colormap
        float range = uScalarMax - uScalarMin;
        float t = (range > 1e-10) ? clamp((vScalar - uScalarMin) / range, 0.0, 1.0) : 0.5;
        int band = int(t * float(uNumBands));
        if (band >= uNumBands) band = uNumBands - 1;
        float qt = (float(band) + 0.5) / float(uNumBands);
        surfaceColor = jetColor(qt);
    } else if (uUseVertexColor) {
        // 部件颜色模式：用 gl_PrimitiveID 查 triToPart texture buffer
        int partIdx = int(texelFetch(uTriPartMap, gl_PrimitiveID).r);
        int idx = partIdx % 8;
        if (idx < 0) idx += 8;
        surfaceColor = kPalette[idx];
    }

    if (uWireframe) {
        outColor = vec4(surfaceColor, uWireAlpha);
        return;
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    // 主方向光
    vec3 L1 = normalize(-uLightDir);
    float diff1 = max(dot(N, L1), 0.0);

    // 补光（从相反方向来，强度较弱，避免背光面全黑）
    vec3 L2 = normalize(vec3(0.3, 0.5, 0.4));
    float diff2 = max(dot(N, L2), 0.0);

    // Blinn-Phong 高光（主光源）
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L1 + V);

    float ambient, kDiff1, kDiff2, kSpec, shininess;
    if (uContourMode) {
        // 云图模式：高环境光保护色谱颜色，无高光
        ambient = 0.55;
        kDiff1  = 0.35;
        kDiff2  = 0.10;
        kSpec   = 0.0;
        shininess = 32.0;
    } else {
        // 几何模式：柔和高光，避免冲淡部件颜色
        ambient = 0.65;
        kDiff1  = 0.35;
        kDiff2  = 0.20;
        kSpec   = 0.10;
        shininess = 64.0;
    }

    float spec = pow(max(dot(N, H), 0.0), shininess) * kSpec;
    float diffuse = diff1 * kDiff1 + diff2 * kDiff2;
    float sideFactor = gl_FrontFacing ? 1.0 : 0.8;

    vec3 color = surfaceColor * (ambient + diffuse) * sideFactor + vec3(spec * sideFactor);
    // 防止过曝
    color = min(color, vec3(1.0));
    outColor = vec4(color, uSurfaceAlpha);
}

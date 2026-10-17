#pragma once

inline constexpr char kVideoEffectsShader[] = R"HLSL(
Texture2D video : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Parameters : register(b0) {
    float4 geometry; // reciprocal texture dimensions, sharpness, curvature
    float4 color;    // contrast, saturation, inverse gamma, temperature
    float4 crt;      // scanline strength, mask strength, line count, unused
    float4 rect;     // content rectangle in normalized texture coordinates
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VSMain(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv.x * 2 - 1, 1 - v.uv.y * 2, 0, 1);
    return v;
}
float3 ReadVideo(float2 uv) {
    float2 halfPixel = geometry.xy * 0.5;
    return video.SampleLevel(linearClamp, clamp(uv, rect.xy + halfPixel, rect.zw - halfPixel), 0).rgb;
}
float Luma(float3 v) { return dot(v, float3(0.2126, 0.7152, 0.0722)); }
float4 PSMain(Vertex v) : SV_Target {
    float2 p = (v.uv - rect.xy) / (rect.zw - rect.xy);
    if (any(p < 0) || any(p > 1)) return float4(0, 0, 0, 1);
    float2 centered = p * 2 - 1;
    // Inverse barrel mapping; outside the tube remains black.
    float2 warped = centered * (1 + geometry.w * centered.yx * centered.yx);
    p = warped * 0.5 + 0.5;
    if (any(p < 0) || any(p > 1)) return float4(0, 0, 0, 1);
    float2 uv = rect.xy + p * (rect.zw - rect.xy);
    float3 c = ReadVideo(uv);
    if (geometry.z > 0) {
        float3 n = ReadVideo(uv - float2(0, geometry.y));
        float3 s = ReadVideo(uv + float2(0, geometry.y));
        float3 w = ReadVideo(uv - float2(geometry.x, 0));
        float3 e = ReadVideo(uv + float2(geometry.x, 0));
        float3 nw = ReadVideo(uv - geometry.xy);
        float3 ne = ReadVideo(uv + float2(geometry.x, -geometry.y));
        float3 sw = ReadVideo(uv + float2(-geometry.x, geometry.y));
        float3 se = ReadVideo(uv + geometry.xy);
        float3 lo = min(c, min(min(min(n, s), min(w, e)), min(min(nw, ne), min(sw, se))));
        float3 hi = max(c, max(max(max(n, s), max(w, e)), max(max(nw, ne), max(sw, se))));
        float3 blur = c * 0.25 + (n + s + w + e) * 0.125 + (nw + ne + sw + se) * 0.0625;
        float activity = Luma(hi) - Luma(lo);
        float gate = smoothstep(0.003, 0.025, activity);
        float limiter = 1.0 - 0.55 * smoothstep(0.18, 0.70, activity);
        float gain = geometry.z * (1.25 + geometry.z * 1.50) * gate * limiter;
        float3 margin = (hi - lo) * (0.04 + geometry.z * 0.08);
        c = clamp(c + (c - blur) * gain, lo - margin, hi + margin);
    }
    // Creative SDR controls follow input-range interpretation.
    c *= float3(1 + color.w * 0.12, 1, 1 - color.w * 0.12);
    c = lerp(Luma(c).xxx, c, color.y);
    c = saturate((c - 0.5) * color.x + 0.5);
    if (abs(color.z - 1) > 0.0001) c = pow(c, color.z);
    if (crt.x > 0) {
        float beam = 0.5 + 0.5 * cos(p.y * crt.z * 6.28318530718);
        c *= 1 - crt.x * (1 - beam);
    }
    if (crt.y > 0) {
        float stripe = fmod(floor(v.position.x), 3.0);
        float3 phosphor = stripe < 1 ? float3(1, 0, 0) :
                          stripe < 2 ? float3(0, 1, 0) : float3(0, 0, 1);
        c *= lerp(float3(1, 1, 1), phosphor, crt.y);
    }
    return float4(saturate(c), 1);
}
)HLSL";

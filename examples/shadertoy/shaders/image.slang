module image;

// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Source: https://www.shadertoy.com/view/7cfGzn
// Copyright (c) 2026 @WorkingClassHacker
// Based on "Abstract Shine" by @Frosbyte / @Frostbyte.
//
// Adapted from the original Shadertoy fragment shader to an offline Slang compute shader.

uniform uint2 dims;
uniform float timeSeconds;
uniform RWTexture2D<float4> outputTexture;

static float2x2 rotation(float angle) {
    float4 c = cos(angle + float4(0.0, 33.0, 11.0, 0.0));
    return float2x2(c.x, c.y, c.z, c.w);
}

static float3 palette(float i) {
    const float3 a = float3(0.50, 0.38, 0.26);
    const float3 b = float3(0.50, 0.35, 0.25);
    const float3 c = float3(1.00);
    const float3 d = float3(0.00, 0.12, 0.25);
    return a + b * cos(6.2831853 * (c * i + d));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatch_thread_id: SV_DispatchThreadID) {
    uint2 pixel = dispatch_thread_id.xy;
    if (any(pixel >= dims))
        return;

    float2 resolution = float2(dims);
    float2 frag_coord = float2(pixel) + 0.5;
    float2 uv = (frag_coord - 0.5 * resolution + 0.5) / resolution.y;

    float t = fmod(timeSeconds, 6.283185);
    float3 ray_direction = normalize(float3(2.0 * frag_coord - resolution, resolution.y));
    float3 p = float3(0.0, 0.0, t);

    float s = 0.0;
    float i = 0.0;
    float4 frag_color = 0.0;
    for (i = 0.0; i < 20.0; i += 1.0) {
        p.xy = mul(rotation(-p.z * 0.01 - t * 0.05), p.xy);

        s = 0.6;
        s = max(s, 4.0 * (-length(p.xy) + 10.0));
        s += abs(p.y * 0.004 + sin(t - p.x * 0.5) * 0.9 + 1.0);

        p += ray_direction * s;
        frag_color += 1.0 / (s * 0.2);
    }

    frag_color *= float4(palette(length(p) / (abs(sin(timeSeconds * 0.02) * 50.0) + 6.0)), 1.0);
    frag_color -= 20.0 * smoothstep(0.001,
                                    abs(sin(timeSeconds * 5.0)),
                                    0.7 - length(sin(uv * 200.0) / 1.5) - abs(uv.y) + 0.2);
    frag_color /= 50.0;

    float l = length(uv);
    frag_color *= 1.2 - l;
    frag_color = lerp(frag_color, float4(palette(l - 0.23).rgbr), 1.0 - smoothstep(0.01, 0.95, l));
    frag_color = tanh(frag_color + frag_color);

    outputTexture[pixel] = float4(saturate(frag_color.rgb), 1.0);
}

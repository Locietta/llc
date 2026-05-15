module brdf_2d;

static const float kPi = 3.14159265358979323846f;
static const uint kChannelCount = 7;
static const uint kInvalidIndex = 0xffffffffu;
static const uint kGradientWindowSize = 64;

groupshared float gGradientWaveSums[kGradientWindowSize];

struct InputParams {
    float L_x;
    float L_y;
    float L_z;
    float V_x;
    float V_y;
    float V_z;
};

struct Params {
    float3 L;
    float3 V;
};

struct AdamState {
    float mean;
    float variance;
    int iteration;
};

Params packInputParams(InputParams inputParams) {
    Params params;
    params.L = normalize(float3(inputParams.L_x, inputParams.L_y, inputParams.L_z));
    params.V = normalize(float3(inputParams.V_x, inputParams.V_y, inputParams.V_z));
    return params;
}

uint brdfIndex(no_diff uint width, no_diff uint x, no_diff uint y, no_diff uint channel) {
    return ((y * width + x) * kChannelCount) + channel;
}

[Differentiable]
float square(float x) {
    return x * x;
}

[Differentiable]
float clampValue(float x, float xMin, float xMax) {
    return max(min(x, xMax), xMin);
}

[Differentiable]
float sqr(float x) {
    return x * x;
}

[Differentiable]
float schlickFresnel(float u) {
    float m = clampValue(1.0f - u, 0.0f, 1.0f);
    float m2 = m * m;
    return m2 * m2 * m;
}

[Differentiable]
float gtr1(float nDotH, float a) {
    if (a >= 1.0f)
        return 1.0f / kPi;
    float a2 = a * a;
    float t = 1.0f + (a2 - 1.0f) * nDotH * nDotH;
    return (a2 - 1.0f) / (kPi * log(a2) * t);
}

[Differentiable]
float gtr2(float nDotH, float a) {
    float a2 = a * a;
    float t = 1.0f + (a2 - 1.0f) * nDotH * nDotH;
    return a2 / (kPi * t * t);
}

[Differentiable]
float smithG_GGX(float nDotV, float alphaG) {
    float a = alphaG * alphaG;
    float b = nDotV * nDotV;
    return 1.0f / (nDotV + sqrt(a + b - a * b));
}

[Differentiable]
float3 disneyBrdf(
    float3 L,
    float3 V,
    float3 N,
    float3 baseColor,
    float roughness,
    float metallic,
    float specular) {
    const float subsurface = 0.0f;
    const float specularTint = 0.0f;
    const float sheen = 0.0f;
    const float sheenTint = 0.5f;
    const float clearcoat = 0.0f;
    const float clearcoatGloss = 1.0f;

    float nDotL = dot(N, L);
    float nDotV = dot(N, V);
    if (nDotL < 0.0f || nDotV < 0.0f)
        return float3(0.0f);

    float3 H = normalize(L + V);
    float nDotH = dot(N, H);
    float lDotH = dot(L, H);

    float3 cdLin = baseColor;
    float cdLum = 0.3f * cdLin[0] + 0.6f * cdLin[1] + 0.1f * cdLin[2];

    float3 cTint = cdLum > 0.0f ? cdLin / cdLum : float3(1.0f);
    float3 cSpec0 = lerp(specular * float3(0.08f) * lerp(float3(1.0f), cTint, specularTint), cdLin, metallic);
    float3 cSheen = lerp(float3(1.0f), cTint, sheenTint);

    float FL = schlickFresnel(nDotL);
    float FV = schlickFresnel(nDotV);
    float fd90 = 0.5f + 2.0f * lDotH * lDotH * roughness;
    float fd = lerp(1.0f, fd90, FL) * lerp(1.0f, fd90, FV);

    float fss90 = lDotH * lDotH * roughness;
    float fss = lerp(1.0f, fss90, FL) * lerp(1.0f, fss90, FV);
    float ss = 1.25f * (fss * (1.0f / (nDotL + nDotV) - 0.5f) + 0.5f);

    float a = sqr(roughness);
    float Ds = gtr2(nDotH, a);
    float FH = schlickFresnel(lDotH);
    float3 Fs = lerp(cSpec0, float3(1.0f), FH);
    float Gs = smithG_GGX(nDotL, a) * smithG_GGX(nDotV, a);

    float3 fSheen = FH * sheen * cSheen;
    float Dr = gtr1(nDotH, lerp(0.1f, 0.001f, clearcoatGloss));
    float Fr = lerp(0.04f, 1.0f, FH);
    float Gr = smithG_GGX(nDotL, 0.25f) * smithG_GGX(nDotV, 0.25f);

    return ((1.0f / kPi) * lerp(fd, ss, subsurface) * cdLin + fSheen) * (1.0f - metallic) +
        Gs * Fs * Ds + 0.25f * clearcoat * Gr * Fr * Dr;
}

[Differentiable]
float bilinWeight(float t) {
    if (abs(t) > 1.0f)
        return 0.0f;
    return 1.0f - abs(t);
}

[Differentiable]
float loadBrdfValue(
    no_diff uniform float *brdf,
    no_diff uint width,
    no_diff uint height,
    no_diff int2 loc,
    no_diff uint channel,
    no_diff uint overrideIndex,
    float overrideValue) {
    int2 clamped = min(max(loc, int2(0, 0)), int2(int(width) - 1, int(height) - 1));
    uint index = brdfIndex(width, uint(clamped.x), uint(clamped.y), channel);
    if (index == overrideIndex)
        return overrideValue;
    return brdf[index];
}

[Differentiable]
float sampleBilinear(
    no_diff uniform float *brdf,
    no_diff uint width,
    no_diff uint height,
    no_diff float2 uv,
    no_diff uint channel,
    no_diff uint overrideIndex,
    float overrideValue) {
    float accum = 0.0f;
    float2 pixelCoord = uv * float2(width, height) - 0.5f;
    int2 pixelCoordInt = int2(floor(pixelCoord));
    float2 fracOffset = float2(pixelCoordInt) - pixelCoord;

    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            int2 finalCoord = pixelCoordInt + int2(dx, dy);
            float2 relativeDiff = fracOffset + float2(dx, dy);
            float w = bilinWeight(relativeDiff.x) * bilinWeight(relativeDiff.y);
            accum += loadBrdfValue(brdf, width, height, finalCoord, channel, overrideIndex, overrideValue) * w;
        }
    }
    return accum;
}

[Differentiable]
float3 computeLighting(
    no_diff uniform float *brdf,
    no_diff uint width,
    no_diff uint height,
    no_diff float2 uv,
    no_diff Params params,
    no_diff uint overrideIndex,
    float overrideValue) {
    float3 baseColor = float3(
        sampleBilinear(brdf, width, height, uv, 0, overrideIndex, overrideValue),
        sampleBilinear(brdf, width, height, uv, 1, overrideIndex, overrideValue),
        sampleBilinear(brdf, width, height, uv, 2, overrideIndex, overrideValue));

    float3 N = float3(
        sampleBilinear(brdf, width, height, uv, 3, overrideIndex, overrideValue),
        sampleBilinear(brdf, width, height, uv, 4, overrideIndex, overrideValue),
        sampleBilinear(brdf, width, height, uv, 5, overrideIndex, overrideValue));

    float roughness = sampleBilinear(brdf, width, height, uv, 6, overrideIndex, overrideValue);
    baseColor = baseColor * baseColor;
    N = normalize(N * 2.0f - 1.0f);

    return disneyBrdf(params.L, params.V, N, baseColor, roughness, 0.0f, 1.0f);
}

[Differentiable]
float pixelLossForParam(
    no_diff uniform float *fullBrdf,
    no_diff uniform float *halfBrdf,
    no_diff uint paramIndex,
    float paramValue,
    no_diff float2 uv,
    no_diff Params params,
    no_diff uint fullWidth,
    no_diff uint fullHeight,
    no_diff uint halfWidth,
    no_diff uint halfHeight) {
    let reference = no_diff sqrt(max(computeLighting(fullBrdf, fullWidth, fullHeight, uv, params, kInvalidIndex, 0.0f), 0.0f));
    float3 optimized = sqrt(max(computeLighting(halfBrdf, halfWidth, halfHeight, uv, params, paramIndex, paramValue), 0.0f));
    float3 diff = optimized - reference;
    return dot(diff, diff);
}

[numthreads(kGradientWindowSize, 1, 1)]
[require(spvGroupNonUniformArithmetic)]
void computeGradient(
    uint3 groupId: SV_GroupID,
    uint3 groupThreadId: SV_GroupThreadID,
    uniform float *fullBrdf,
    uniform float *halfBrdf,
    uniform float *gradients,
    uniform uint parameterCount,
    uniform uint fullWidth,
    uniform uint fullHeight,
    uniform uint halfWidth,
    uniform uint halfHeight,
    uniform InputParams inputParams) {
    uint tid = groupId.x;
    uint localTid = groupThreadId.x;
    if (tid >= parameterCount)
        return;

    uint texelIndex = tid / kChannelCount;
    uint hx = texelIndex % halfWidth;
    uint hy = texelIndex / halfWidth;
    float param = halfBrdf[tid];
    Params params = packInputParams(inputParams);

    int minX = max(0, int(hx) * 2 - 3);
    int maxX = min(int(fullWidth) - 1, int(hx) * 2 + 4);
    int minY = max(0, int(hy) * 2 - 3);
    int maxY = min(int(fullHeight) - 1, int(hy) * 2 + 4);

    int windowX = minX + int(localTid & 7);
    int windowY = minY + int(localTid >> 3);
    float localGrad = 0.0f;
    if (windowX <= maxX && windowY <= maxY) {
        float2 uv = (float2(windowX, windowY) + 0.5f) / float2(fullWidth, fullHeight);
        DifferentialPair<float> paramPair = diffPair(param);
        bwd_diff(pixelLossForParam)(
            fullBrdf,
            halfBrdf,
            tid,
            paramPair,
            uv,
            params,
            fullWidth,
            fullHeight,
            halfWidth,
            halfHeight,
            1.0f);
        localGrad = paramPair.d;
    }

    localGrad = WaveActiveSum(localGrad);

    uint waveSize = WaveGetLaneCount();
    uint waveId = localTid / waveSize;
    if (WaveIsFirstLane())
        gGradientWaveSums[waveId] = localGrad;

    GroupMemoryBarrierWithGroupSync();

    uint waveCount = (kGradientWindowSize + waveSize - 1) / waveSize;
    float groupGrad = localTid < waveCount ? gGradientWaveSums[localTid] : 0.0f;
    groupGrad = WaveActiveSum(groupGrad);

    if (localTid == 0)
        gradients[tid] = groupGrad / float(fullWidth * fullHeight);
}

[numthreads(256, 1, 1)]
void adamStep(
    uint tid: SV_DispatchThreadID,
    uniform AdamState *states,
    uniform float *params,
    uniform float *gradients,
    uniform uint parameterCount,
    uniform float learningRate) {
    if (tid >= parameterCount)
        return;

    float grad = gradients[tid];
    if (isnan(grad))
        grad = 0.0f;
    if (isinf(grad))
        grad = grad > 0.0f ? 10000.0f : -10000.0f;

    const float beta1 = 0.9f;
    const float beta2 = 0.999f;
    const float epsilon = 1e-7f;

    AdamState state = states[tid];
    state.iteration++;
    state.mean = beta1 * state.mean + (1.0f - beta1) * grad;
    state.variance = beta2 * state.variance + (1.0f - beta2) * grad * grad;
    float meanHat = state.mean / (1.0f - pow(beta1, float(state.iteration)));
    float varianceHat = state.variance / (1.0f - pow(beta2, float(state.iteration)));
    params[tid] -= learningRate * meanHat / sqrt(max(0.0f, varianceHat) + epsilon);
    params[tid] = clamp(params[tid], 0.0f, 1.0f);
    gradients[tid] = 0.0f;
    states[tid] = state;
}

[numthreads(16, 16, 1)]
void renderBrdf(
    uint2 tid: SV_DispatchThreadID,
    uniform float *brdf,
    uniform float *output,
    uniform uint outputWidth,
    uniform uint outputHeight,
    uniform uint brdfWidth,
    uniform uint brdfHeight,
    uniform InputParams inputParams) {
    if (tid.x >= outputWidth || tid.y >= outputHeight)
        return;

    Params params = packInputParams(inputParams);
    float2 uv = (float2(tid) + 0.5f) / float2(outputWidth, outputHeight);
    float3 lighting = sqrt(max(computeLighting(brdf, brdfWidth, brdfHeight, uv, params, kInvalidIndex, 0.0f), 0.0f));
    uint offset = (tid.y * outputWidth + tid.x) * 3;
    output[offset + 0] = lighting.r;
    output[offset + 1] = lighting.g;
    output[offset + 2] = lighting.b;
}

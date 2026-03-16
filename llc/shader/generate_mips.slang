module generate_mips;

[shader("compute")]
[numthreads(16, 16, 1)]
void mainCompute<let DST_FORMAT : int>(
    uint3 tid: SV_DispatchThreadID,
    uniform uint2 srcSize,
    uniform uint2 dstSize,
    Texture2D<float4> src,
    RWTexture2D<float4, 0, DST_FORMAT> dst) {
    if (any(tid.xy >= dstSize))
        return;

    uint2 base = min(tid.xy * 2, srcSize - 1);
    uint2 p0 = base;
    uint2 p1 = uint2(min(base.x + 1, srcSize.x - 1), base.y);
    uint2 p2 = uint2(base.x, min(base.y + 1, srcSize.y - 1));
    uint2 p3 = uint2(min(base.x + 1, srcSize.x - 1), min(base.y + 1, srcSize.y - 1));

    float4 color = 0.25 * (src[p0] + src[p1] + src[p2] + src[p3]);
    dst[tid.xy] = color;
}

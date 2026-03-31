module reduce;

import span;

public interface IReduceElement {
    __init(int v);
    This dadd(This other);
    static This waveSum(This a);
};

T operator +<T : IReduceElement>(T a, T b) {
    return a.dadd(b);
}

public extern struct ReduceElement : IReduceElement;
public extern struct ReduceTexture {
    ReduceElement load(uint2 sourceSize, uint index);
};

static const uint THREAD_GROUP_SIZE = 256;
static const uint WARP_SIZE = 32;
static const uint WAVE_PER_GROUP = THREAD_GROUP_SIZE / WARP_SIZE;

groupshared ReduceElement g_wave_sums[WAVE_PER_GROUP];

ReduceElement reduce_group_sum(uint localIndex, uint laneIndex, uint waveIndex, ReduceElement sum) {
    sum = ReduceElement.waveSum(sum);
    if (WaveIsFirstLane()) g_wave_sums[waveIndex] = sum;

    // Synchronize all threads in the group to ensure all wave reductions are visible.
    GroupMemoryBarrierWithGroupSync();

    sum = (localIndex < WAVE_PER_GROUP) ? g_wave_sums[laneIndex] : ReduceElement(0);
    if (waveIndex == 0) sum = ReduceElement.waveSum(sum);
    return sum;
}

[shader("compute")]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void reduce(
    uint3 groupThreadID: SV_GroupThreadID,
    uint3 groupID: SV_GroupID,
    uniform Span<ReduceElement> source,
    uniform MutSpan<ReduceElement> result) {
    uint localIndex = groupThreadID.x;
    uint index = groupID.x * THREAD_GROUP_SIZE * 2 + localIndex;
    uint num_elements = (uint) source.length;

    var sum = index < num_elements ? source[index] : ReduceElement(0);
    sum = sum + ((index + THREAD_GROUP_SIZE < num_elements) ? source[index + THREAD_GROUP_SIZE] : ReduceElement(0));

    uint laneIndex = WaveGetLaneIndex();
    uint waveIndex = localIndex / WARP_SIZE;

    sum = reduce_group_sum(localIndex, laneIndex, waveIndex, sum);
    if (localIndex == 0) result[groupID.x] = sum;
}

[shader("compute")]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void reduce_texture(
    uint3 groupThreadID: SV_GroupThreadID,
    uint3 groupID: SV_GroupID,
    uniform uint2 sourceSize,
    uniform ReduceTexture source,
    uniform MutSpan<ReduceElement> result) {
    uint localIndex = groupThreadID.x;
    uint index = groupID.x * THREAD_GROUP_SIZE * 2 + localIndex;
    uint num_elements = sourceSize.x * sourceSize.y;

    ReduceElement sum = source.load(sourceSize, index);
    if (index + THREAD_GROUP_SIZE < num_elements)
        sum = sum + source.load(sourceSize, index + THREAD_GROUP_SIZE);

    uint laneIndex = WaveGetLaneIndex();
    uint waveIndex = localIndex / WARP_SIZE;

    sum = reduce_group_sum(localIndex, laneIndex, waveIndex, sum);
    if (localIndex == 0) result[groupID.x] = sum;
}

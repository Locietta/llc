module reduce;

public interface IReduceElement {
    __init(int v);
    This dadd(This other);
    static This waveSum(This a);
};

T operator +<T : IReduceElement>(T a, T b) {
    return a.dadd(b);
}

extern struct ReduceElement : IReduceElement;

static const uint THREAD_GROUP_SIZE = 256;
static const uint WARP_SIZE = 32;
static const uint WAVE_PER_GROUP = THREAD_GROUP_SIZE / WARP_SIZE;

groupshared ReduceElement g_wave_sums[WAVE_PER_GROUP];

[shader("compute")]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void reduce(
    uint3 groupThreadID: SV_GroupThreadID,
    uint3 groupID: SV_GroupID,
    uniform StructuredBuffer<ReduceElement> source,
    uniform RWStructuredBuffer<ReduceElement> result) {

    uint localIndex = groupThreadID.x;
    uint index = groupID.x * THREAD_GROUP_SIZE * 2 + localIndex;
    uint num_elements = source.getCount();

    var sum = index < num_elements ? source[index] : ReduceElement(0);
    sum = sum + ((index + THREAD_GROUP_SIZE < num_elements) ? source[index + THREAD_GROUP_SIZE] : ReduceElement(0));

    uint laneIndex = WaveGetLaneIndex();
    uint waveIndex = localIndex / WARP_SIZE;

    sum = ReduceElement.waveSum(sum);
    if (WaveIsFirstLane()) g_wave_sums[waveIndex] = sum;

    // Synchronize all threads in the group to ensure all wave reductions are visible
    GroupMemoryBarrierWithGroupSync();

    sum = (localIndex < WAVE_PER_GROUP) ? g_wave_sums[laneIndex] : ReduceElement(0);
    if (waveIndex == 0) sum = ReduceElement.waveSum(sum);
    if (localIndex == 0) result[groupID.x] = sum;
}

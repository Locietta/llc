module reduce;

public
StructuredBuffer<float> source;
public
RWStructuredBuffer<float> result;
public
Texture2D<float> sourceTexture;
public
uint sourceCount;
public
uint2 textureSize;

static const uint THREAD_GROUP_SIZE = 256;
groupshared float waveSums[32];

float loadTextureValue(uint linearIndex) {
    uint2 coord = uint2(linearIndex % textureSize.x, linearIndex / textureSize.x);
    return sourceTexture[coord];
}

[shader("compute")]
    [numthreads(THREAD_GROUP_SIZE, 1, 1)] void
    reduceBuffer(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {
        uint localIndex = groupThreadID.x;
        uint index = groupID.x * THREAD_GROUP_SIZE * 2 + localIndex;
        float sum = index < sourceCount ? source[index] : 0.0f;
        sum += index + THREAD_GROUP_SIZE < sourceCount ? source[index + THREAD_GROUP_SIZE] : 0.0f;
        uint laneIndex = WaveGetLaneIndex();
        uint waveIndex = localIndex / 32;
        sum = WaveActiveSum(sum);
        if (WaveIsFirstLane()) {
            waveSums[waveIndex] = sum;
        }
        GroupMemoryBarrierWithGroupSync();
        sum = localIndex < THREAD_GROUP_SIZE / 32 ? waveSums[laneIndex] : 0.0f;
        if (waveIndex == 0) {
            sum = WaveActiveSum(sum);
        }
        if (localIndex == 0) {
            result[groupID.x] = sum;
        }
    }

        [shader("compute")]
        [numthreads(THREAD_GROUP_SIZE, 1, 1)] void reduceTexture(uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {
    uint localIndex = groupThreadID.x;
    uint index = groupID.x * THREAD_GROUP_SIZE * 2 + localIndex;
    float sum = index < sourceCount ? loadTextureValue(index) : 0.0f;
    sum += index + THREAD_GROUP_SIZE < sourceCount ? loadTextureValue(index + THREAD_GROUP_SIZE) : 0.0f;
    uint laneIndex = WaveGetLaneIndex();
    uint waveIndex = localIndex / 32;
    sum = WaveActiveSum(sum);
    if (WaveIsFirstLane()) {
        waveSums[waveIndex] = sum;
    }
    GroupMemoryBarrierWithGroupSync();
    sum = localIndex < THREAD_GROUP_SIZE / 32 ? waveSums[laneIndex] : 0.0f;
    if (waveIndex == 0) {
        sum = WaveActiveSum(sum);
    }
    if (localIndex == 0) {
        result[groupID.x] = sum;
    }
}

/// Simple A+B compute shader
module APlusB;

StructuredBuffer<float> A;
StructuredBuffer<float> B;
RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(128, 1, 1)]
void computeMain(uint3 threadId: SV_DispatchThreadID) {
    uint index = threadId.x;
    result[index] = A[index] + B[index];
}

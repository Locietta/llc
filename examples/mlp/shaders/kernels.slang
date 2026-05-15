module kernels;

import common;
import mlp;
import network;
import adam;

[Differentiable]
half computeLoss(MyNetwork *network, no_diff half x, no_diff half y) {
    return loss(network, x, y);
}

[numthreads(256, 1, 1)]
[require(spvGroupNonUniformBallot, spvGroupNonUniformArithmetic, spvCooperativeVectorNV)]
void learnGradient(
    uint tid: SV_DispatchThreadID,
    uniform MyNetwork *network,
    uniform Atomic<uint> *lossBuffer,
    uniform float2 *inputs,
    uniform uint count) {
    if (tid >= count)
        return;

    var input = (half2) inputs[tid];
    bwd_diff(computeLoss)(network, input.x, input.y, 1.0h);
    let thisLoss = (float) computeLoss(network, input.x, input.y);
    let maxLoss = WaveActiveMax(thisLoss);
    if (WaveIsFirstLane()) {
        lossBuffer.max(bit_cast<uint>(maxLoss));
    }
}

[numthreads(256, 1, 1)]
void adjustParameters(
    uint tid: SV_DispatchThreadID,
    uniform AdamState *states,
    uniform half *params,
    uniform half *gradients,
    uniform uint count) {
    if (tid >= count)
        return;
    if (isnan(gradients[tid])) {
        gradients[tid] = 0.0h;
        return;
    }
    AdamOptimizer::step(states[tid], params[tid], gradients[tid]);
}

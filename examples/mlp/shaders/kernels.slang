module kernels;

import common;
import mlp;
import network;
import adam;

public half4 groundtruth(half x, half y) {
    return {
        (x + y) / (1 + y * y),
        2 * x + y,
        0.5 * x * x + 1.2 * y,
        x + 0.5 * y * y,
    };
}

[Differentiable]
public half loss(MyNetwork *network, no_diff half x, no_diff half y) {
    let networkResult = network.eval(x, y);
    let gt = no_diff groundtruth(x, y);
    let diff = networkResult - gt;
    return dot(diff, diff);
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
    bwd_diff(loss)(network, input.x, input.y, 1.0h);
    let thisLoss = (float) loss(network, input.x, input.y);
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

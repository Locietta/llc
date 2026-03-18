module adam;

import common;

public struct AdamState {
    internal half mean;
    internal half variance;
    internal int iteration;
}

public struct AdamOptimizer {
    public static const half beta1 = 0.9h;
    public static const half beta2 = 0.999h;
    public static const half epsilon = 1e-7h;
    public static const half learningRate = 0.01h;

    public static void step(inout AdamState state, inout half param, inout half grad) {
        state.iteration++;
        if (isinf(grad)) {
            grad = grad > 0 ? 10000.0h : -10000.0h;
        }
        state.mean = beta1 * state.mean + (half(1.f) - beta1) * grad;
        state.variance = beta2 * state.variance + (half(1.f) - beta2) * grad * grad;
        half meanHat = state.mean / (half(1.f) - pow(beta1, half(state.iteration)));
        half varianceHat = state.variance / (half(1.f) - pow(beta2, half(state.iteration)));
        param -= learningRate * meanHat / (sqrt(max(half(0.f), varianceHat) + epsilon));
        grad = half(0.f);
    }
}

module network;

import common;
import mlp;
import mlvec;

public struct MyNetwork {
    public FeedForwardLayer<4, 16> layer1;
    public FeedForwardLayer<16, 4> layer2;

    [Differentiable]
    internal MLVec<4> encodeInput(half x, half y) {
        return MLVec<4>.fromArray(
            {
                x,
                y,
                x * x,
                y * y,
            });
    }

    [Differentiable]
    internal MLVec<4> _eval(half x, half y) {
        let encoding = encodeInput(x, y);
        let layer1Output = layer1.eval(encoding);
        let layer2Output = layer2.eval(layer1Output);
        return layer2Output;
    }

    [Differentiable]
    public half4 eval(no_diff half x, no_diff half y) {
        let mlv = _eval(x, y);
        let arr = mlv.toArray();
        return half4(arr[0], arr[1], arr[2], arr[3]);
    }
}

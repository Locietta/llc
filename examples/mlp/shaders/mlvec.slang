module mlvec;

import common;

public struct MLVec<int N> : IDifferentiable {
    public CoopVec<half, N> data;
    public typealias Differential = MLVec<N>;

    public static MLVec<N> fromArray(half[N] values) {
        MLVec<N> result;
        [ForceUnroll]
        for (int i = 0; i < N; i++)
            result.data[i] = values[i];
        return result;
    }

    internal static half[N] coopVecToArray(CoopVec<half, N> v) {
        half[N] arr;
        [ForceUnroll]
        for (int i = 0; i < N; i++)
            arr[i] = v[i];
        return arr;
    }

    [BackwardDerivativeOf(fromArray)]
    internal static void fromArrayBwd(inout DifferentialPair<half[N]> values, MLVec<N> dResult) {
        values = diffPair(values.p, coopVecToArray(dResult.data));
    }

    internal static half[N] toArray(MLVec<N> vec) {
        return coopVecToArray(vec.data);
    }

    [BackwardDerivativeOf(toArray)]
    internal static void toArrayBwd(inout DifferentialPair<MLVec<N>> vec, half[N] dResult) {
        vec = diffPair(vec.p, MLVec<N>.fromArray(dResult));
    }

    [Differentiable]
    public half[N] toArray() {
        return toArray(this);
    }

    public override static Differential dadd(Differential d0, Differential d1) {
        return { d0.data + d1.data };
    }

    public override static Differential dmul<U : __BuiltinRealType>(U s, Differential d) {
        return { d.data * __realCast<half>(s) };
    }

    public override static Differential dzero() {
        return {};
    }
}

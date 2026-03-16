module mlp;

import common;
import mlvec;

static const CoopVecComponentType kComponentType = CoopVecComponentType.Float16;

public struct FeedForwardLayer<int InputSize, int OutputSize> {
    internal void *weights;
    internal void *weightsGrad;
    internal void *biases;
    internal void *biasesGrad;

    public MLVec<OutputSize> eval(MLVec<InputSize> input) {
        var output = coopVecMatMulAdd<NFloat, OutputSize>(
            input.data,
            kComponentType,
            weights,
            kComponentType,
            biases,
            kComponentType,
            CoopVecMatrixLayout.RowMajor,
            false,
            InputSize * sizeof(NFloat));
        output = max(output, output * 0.001h);
        return { output };
    }

    [BackwardDerivativeOf(eval)]
    public void evalBwd(
        inout DifferentialPair<MLVec<InputSize>> input,
        MLVec<OutputSize> resultGrad) {
        let fwd = eval(input.p);

        [ForceUnroll]
        for (int i = 0; i < OutputSize; i++) {
            if (fwd.data[i] < 0.0)
                resultGrad.data[i] *= 0.01h;
        }

        coopVecOuterProductAccumulate(
            resultGrad.data,
            input.p.data,
            weightsGrad,
            0,
            CoopVecMatrixLayout.TrainingOptimal,
            kComponentType);

        coopVecReduceSumAccumulate(resultGrad.data, (void *) biasesGrad);

        let dInput = coopVecMatMul<NFloat, InputSize>(
            resultGrad.data,
            kComponentType,
            weights,
            kComponentType,
            CoopVecMatrixLayout.ColumnMajor,
            false,
            InputSize * sizeof(NFloat));

        input = { input.p, { dInput } };
    }
}

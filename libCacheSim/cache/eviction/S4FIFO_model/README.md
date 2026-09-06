# S4FIFO "lite" model

Plain-C, dependency-free port of the S4FIFO parameter-prediction model used by
S4FIFO's optional learned control plane (`auto-tune=1`).

## Provenance

- Trained by `train_xgb_18class_lite.py`, exported to C via
  [m2cgen](https://github.com/BayesWitnesses/m2cgen).
- Ported from `williamnixon20/CacheLib@72bb8103da1edbba5a294f672eb87647b16a2599`,
  `cachelib/allocator/s4fifo_model/`.
- This is a smaller, weaker distillation of the full model served at the
  `hxia7/s4fifo-api` HF Space (a 372MB, 20-model LightGBM ensemble) made
  specifically to be embeddable as plain C — the full model is >1GB once
  exported this way.

## Model details

- Type: LightGBM multiclass classifier, 18 classes.
- Ensemble size: 5 models, averaged.
- Trees per model: 50 boosting rounds x 18 classes = 900 trees.
- Max tree depth: 4.
- Reported quality (on the paper's held-out traces): ~47% top-1 class
  accuracy, +15.34% mean miss-ratio improvement over FIFO, -3.90% worst case
  vs FIFO.

## Files

- `model_0.h` .. `model_4.h`: one `static void model_N_score(double *input,
  double *output)` per ensemble member — `input` is the 75-feature vector
  (see `S4FIFO_predictor.h`), `output` is that model's 18 class
  probabilities.
- `softmax.h`: shared softmax helper used by each model.
- `ensemble.h`: averages the 5 models' probabilities and returns the argmax
  class.

These are `static`/header-only and must only ever be `#include`d from a
single translation unit (`S4FIFO_predictor.c`) — they are not meant to be
compiled as standalone sources.

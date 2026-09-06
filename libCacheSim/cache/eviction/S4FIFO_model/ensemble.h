//
// Ensemble aggregation for the S4FIFO "lite" model: 5 LightGBM multiclass
// models (50 trees each, depth 4, 18 classes), trained via
// train_xgb_18class_lite.py and exported to plain C via m2cgen.
//
// Ported verbatim (aggregation logic unchanged) from
// williamnixon20/CacheLib@72bb8103da1edbba5a294f672eb87647b16a2599
// cachelib/allocator/s4fifo_model/S4FIFOEnsemble.h
//
// Include all 5 generated models (header-only, static functions).
#include "model_0.h"
#include "model_1.h"
#include "model_2.h"
#include "model_3.h"
#include "model_4.h"

#define S4FIFO_MODEL_N_CLASSES 18

static void s4fifo_model_ensemble_score(double *input, double *result) {
  double probs_0[S4FIFO_MODEL_N_CLASSES];
  double probs_1[S4FIFO_MODEL_N_CLASSES];
  double probs_2[S4FIFO_MODEL_N_CLASSES];
  double probs_3[S4FIFO_MODEL_N_CLASSES];
  double probs_4[S4FIFO_MODEL_N_CLASSES];

  model_0_score(input, probs_0);
  model_1_score(input, probs_1);
  model_2_score(input, probs_2);
  model_3_score(input, probs_3);
  model_4_score(input, probs_4);

  for (int c = 0; c < S4FIFO_MODEL_N_CLASSES; c++) {
    result[c] =
        (probs_0[c] + probs_1[c] + probs_2[c] + probs_3[c] + probs_4[c]) / 5.0;
  }
}

static int s4fifo_model_ensemble_predict(double *input) {
  double probs[S4FIFO_MODEL_N_CLASSES];
  s4fifo_model_ensemble_score(input, probs);
  int best = 0;
  for (int c = 1; c < S4FIFO_MODEL_N_CLASSES; c++) {
    if (probs[c] > probs[best]) best = c;
  }
  return best;
}

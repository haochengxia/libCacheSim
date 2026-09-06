//
// S4FIFO_predictor.h - learned control plane for S4FIFO ("auto-tune=1").
//
// Given a snapshot of cache-level features collected by S4FIFO_features.h,
// predicts a full 5-knob S4FIFO configuration using the "lite" model
// vendored under S4FIFO_model/ (a 5-model LightGBM multiclass ensemble
// choosing among 18 candidate configurations, distilled/exported to plain
// C via m2cgen - see S4FIFO_model/README.md for provenance).
//
// Only compiled in when built with -DENABLE_S4FIFO_LEARNED=ON; the model
// path is not yet configurable (see S4FIFO_model/README.md) - that is a
// deliberate, documented follow-up, not an oversight.
//

#pragma once

#include <stdbool.h>

#include "S4FIFO_features.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S4FIFO_MODEL_N_CONFIGS 18

// One candidate S4FIFO configuration. Field names/types match S4FIFO.c's own
// parameters directly so a prediction can be applied without conversion.
typedef struct {
  double small_size_ratio;
  int move_to_main_threshold;
  int ghost_to_main_threshold;
  double ghost_size_ratio;
  double small_skip_ratio;
} S4FIFOConfigEntry;

/**
 * @brief predict the best S4FIFO configuration for the workload summarized
 * by `fv`.
 *
 * Mirrors the guard used by the model this was distilled from: a prediction
 * is only attempted once at least 10000 requests and 100 hits have been
 * observed in `fv` (an all-zero/too-small feature vector is not a
 * meaningful classifier input).
 *
 * @param fv observed features (see S4FIFO_features.h)
 * @param out populated with the predicted configuration iff this returns
 * true
 * @return true if a prediction was made (out is valid), false if the
 * guard wasn't met or the model produced an out-of-range class (out is
 * untouched - callers should keep their current configuration)
 */
bool s4fifo_predict(const S4FIFO_feature_vector_t *fv, S4FIFOConfigEntry *out);

#ifdef __cplusplus
}
#endif

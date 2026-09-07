//
// S4FIFO_predictor.c - see S4FIFO_predictor.h.
//
// prepare_model_input()'s feature layout/derivations and kS4FIFOConfigs are
// ported verbatim from williamnixon20/CacheLib@72bb8103
// cachelib/allocator/S4FIFOLightGBMPredictor.h - do not "clean up" the
// formulas here without retraining the model, the two must match exactly.
//

#include "S4FIFO_predictor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "S4FIFO_model_real.h"
#include "libCacheSim/logging.h"

// The vendored model is its own translation unit: it's ~8.3MB of generated,
// header-only/static code and must never be #included from more than one
// place.
#include "S4FIFO_model/ensemble.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S4FIFO_MODEL_N_FEATURES 75
#define S4FIFO_PREDICT_MIN_REQUESTS 10000
#define S4FIFO_PREDICT_MIN_HITS 100

// The 18 configurations the model was trained to choose among. Values and
// order must match the model exactly - the model's output is a class index
// into this table, nothing more.
static const S4FIFOConfigEntry kS4FIFOConfigs[S4FIFO_MODEL_N_CONFIGS] = {
    {0.20, 1, 0, 3.0, 0.25},  // Class 0
    {0.05, 1, 0, 0.9, 0.25},  // Class 1
    {0.50, 1, 0, 0.9, 0.25},  // Class 2
    {0.20, 1, 0, 0.9, 0.25},  // Class 3
    {0.05, 2, 0, 6.0, 0.25},  // Class 4
    {0.10, 2, 1, 3.0, 0.25},  // Class 5
    {0.30, 2, 0, 3.0, 0.25},  // Class 6
    {0.05, 2, 0, 3.0, 0.25},  // Class 7
    {0.10, 2, 0, 0.9, 0.25},  // Class 8
    {0.70, 1, 1, 0.9, 0.25},  // Class 9
    {0.20, 1, 1, 0.9, 0.25},  // Class 10
    {0.05, 1, 1, 0.9, 0.25},  // Class 11
    {0.30, 1, 0, 6.0, 0.25},  // Class 12
    {0.20, 2, 0, 0.9, 0.25},  // Class 13
    {0.90, 2, 0, 3.0, 0.25},  // Class 14
    {0.10, 2, 0, 6.0, 0.25},  // Class 15
    {0.30, 2, 1, 3.0, 0.25},  // Class 16
    {0.05, 2, 0, 0.9, 0.25},  // Class 17
};

// Histogram bins are named hist_{queue}_0 .. hist_{queue}_19 and the model
// was trained on features sorted ALPHABETICALLY by name, so bin order in
// the model's input is the lexicographic order of "0".."19", not numeric
// order: 0, 1, 10, 11, ..., 19, 2, 3, ..., 9.
static const int kHistOrder[20] = {0, 1, 10, 11, 12, 13, 14, 15,
                                   16, 17, 18, 19, 2,  3,  4,  5,
                                   6,  7,  8,  9};

// Feature order (alphabetical, matching training):
// 0: H_g, 1: H_m, 2: H_s, 3: decay_rate_small, 4: entropy_gap,
// 5: ghost_pressure, 6-25: hist_ghost_0..19, 26-45: hist_main_0..19,
// 46-65: hist_small_0..19, 66: log_C, 67: probation_efficiency,
// 68: ratio_estimate, 69: rho_onehit, 70: rho_unique, 71: scan_intensity,
// 72: tail_heaviness, 73: thrashing_risk, 74: total_reqs
static void s4fifo_prepare_model_input(const S4FIFO_feature_vector_t *fv,
                                       double *input /* [75] */) {
  double log_c = fv->log_cache_capacity;

  double probation_efficiency = (double)fv->hits_small / (fv->hits_main + 1e-6);
  double ghost_pressure =
      (double)fv->hits_ghost / ((double)fv->total_hits + fv->hits_ghost + 1e-6);
  double entropy_gap = fv->hit_ratio_main - fv->hit_ratio_small;
  double decay_rate_small = fv->hist_small[0] - fv->hist_small[1];

  double tail_heaviness = 0.0;
  for (int i = 10; i < 20; i++) tail_heaviness += fv->hist_main[i];

  double cache_size = pow(10.0, log_c);
  double working_set_size = (double)fv->total_requests * fv->unique_ratio;
  double ratio_estimate = cache_size / (working_set_size + 1e-6);
  if (ratio_estimate < 0.0001) ratio_estimate = 0.0001;
  if (ratio_estimate > 1.0) ratio_estimate = 1.0;

  double thrashing_risk = fv->unique_ratio / (ratio_estimate * 100.0 + 1e-6);
  double scan_intensity = fv->one_hit_ratio * (1.0 - ratio_estimate);

  input[0] = fv->hit_ratio_ghost;
  input[1] = fv->hit_ratio_main;
  input[2] = fv->hit_ratio_small;
  input[3] = decay_rate_small;
  input[4] = entropy_gap;
  input[5] = ghost_pressure;

  for (int i = 0; i < 20; i++) input[6 + i] = fv->hist_ghost[kHistOrder[i]];
  for (int i = 0; i < 20; i++) input[26 + i] = fv->hist_main[kHistOrder[i]];
  for (int i = 0; i < 20; i++) input[46 + i] = fv->hist_small[kHistOrder[i]];

  input[66] = log_c;
  input[67] = probation_efficiency;
  input[68] = ratio_estimate;
  input[69] = fv->one_hit_ratio;
  input[70] = fv->unique_ratio;
  input[71] = scan_intensity;
  input[72] = tail_heaviness;
  input[73] = thrashing_risk;
  input[74] = (double)fv->total_requests;
}

// Same feature engineering as s4fifo_prepare_model_input(), minus the two
// entries (log_C, ratio_estimate) the real model's feature set doesn't
// include - see model_metadata.json's feature_columns on the HF Space this
// was ported from. Re-derives the 75-feature vector and drops indices 66
// and 68 rather than duplicating the derivation math, so the two backends
// can never silently drift apart on the features they share.
static void s4fifo_prepare_model_input73(const S4FIFO_feature_vector_t *fv,
                                         double *input73 /* [73] */) {
  double input75[S4FIFO_MODEL_N_FEATURES];
  s4fifo_prepare_model_input(fv, input75);
  int j = 0;
  for (int i = 0; i < S4FIFO_MODEL_N_FEATURES; i++) {
    if (i == 66 || i == 68) continue;  // log_C, ratio_estimate
    input73[j++] = input75[i];
  }
}

// Guards both backends: not enough data collected yet for a meaningful
// prediction (see S4FIFO_predictor.h).
static bool s4fifo_has_enough_data(const S4FIFO_feature_vector_t *fv) {
  return fv->total_requests >= S4FIFO_PREDICT_MIN_REQUESTS &&
         fv->total_hits >= S4FIFO_PREDICT_MIN_HITS;
}

bool s4fifo_predict(const S4FIFO_feature_vector_t *fv, S4FIFOConfigEntry *out) {
  if (!s4fifo_has_enough_data(fv)) {
    return false;
  }

  double input[S4FIFO_MODEL_N_FEATURES];
  s4fifo_prepare_model_input(fv, input);

  int class_id = s4fifo_model_ensemble_predict(input);
  if (class_id < 0 || class_id >= S4FIFO_MODEL_N_CONFIGS) {
    return false;
  }

  *out = kS4FIFOConfigs[class_id];
  return true;
}

const S4FIFOConfigEntry *s4fifo_get_config_table(void) { return kS4FIFOConfigs; }

bool s4fifo_predict_auto(const S4FIFO_feature_vector_t *fv,
                         const char *model_path, S4FIFOConfigEntry *out) {
  if (!s4fifo_has_enough_data(fv)) {
    return false;
  }

  if (model_path != NULL && model_path[0] != '\0') {
    if (s4fifo_real_model_load(model_path)) {
      double input73[73];
      s4fifo_prepare_model_input73(fv, input73);
      if (s4fifo_real_model_predict(input73, out)) {
        return true;
      }
      WARN("S4FIFO: real model at %s produced no prediction, "
           "falling back to the lite model\n", model_path);
    } else {
      WARN_ONCE("S4FIFO: failed to load real model at %s, "
                "falling back to the lite model\n", model_path);
    }
  }

  return s4fifo_predict(fv, out);
}

#ifdef __cplusplus
}
#endif

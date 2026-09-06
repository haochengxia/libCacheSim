//
// S4FIFO_model_real.h - loader/evaluator for the *real* s4fifo-api model
// (20-model, 140400-tree LightGBM ensemble), exported to a compact binary
// format (see scripts/s4fifo_export_model.py) since compiling it as C
// source (the vendored S4FIFO_model/ approach) would be well over 1GB.
//
// Opt-in via `model-path=/path/to/model.s4m`; the file lives outside the
// repo (not committed - hosted at
// https://huggingface.co/harvardMadsys/s4fifo-control-plane-model, private
// - see S4FIFO_model/README.md) and is loaded once, lazily, shared
// read-only across every cache instance in the process.
//

#pragma once

#include <stdbool.h>

#include "S4FIFO_predictor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief load the model at `path` if it hasn't been loaded yet (a no-op,
 * returning true, if a model - any model - is already loaded: this
 * simulator only ever runs with one model-path per process).
 *
 * Thread-safe; safe to call from every cache instance's init.
 *
 * @return true if a model is loaded and ready (whether by this call or an
 * earlier one), false if `path` couldn't be loaded (bad path/format).
 */
bool s4fifo_real_model_load(const char *path);

/**
 * @brief predict using the loaded real model. Requires a prior successful
 * s4fifo_real_model_load().
 *
 * @param input73 the 73-feature vector, in model_metadata.json's
 * feature_columns order (see s4fifo_prepare_model_input73)
 * @param out populated with the predicted configuration iff this returns
 * true
 */
bool s4fifo_real_model_predict(const double *input73, S4FIFOConfigEntry *out);

#ifdef __cplusplus
}
#endif

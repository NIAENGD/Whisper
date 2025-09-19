#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque denoiser handle wrapping the RNNoise neural denoiser. */
typedef struct RNNoiseDenoiser RNNoiseDenoiser;

/**
 * Creates a denoiser that operates on 16 kHz PCM frames.
 * @param channels Either 1 (mono) or 2 (stereo).
 * @return Newly allocated denoiser or NULL if allocation fails or the
 *         configuration is not supported.
 */
RNNoiseDenoiser *rnnoise_denoiser_create(int channels);

/** Releases the denoiser and all associated resources. */
void rnnoise_denoiser_destroy(RNNoiseDenoiser *denoiser);

/**
 * Processes a frame of interleaved 32-bit float PCM samples in-place.
 * The frame length must be a multiple of 160 samples (10 ms at 16 kHz).
 * @return 0 on success or a negative value on failure.
 */
int rnnoise_denoiser_process_interleaved(RNNoiseDenoiser *denoiser, float *pcm, size_t frame_length);

/**
 * Processes a frame of planar 32-bit float PCM samples in-place.
 * Each entry of the channels array must contain frame_length samples.
 * @return 0 on success or a negative value on failure.
 */
int rnnoise_denoiser_process_planar(RNNoiseDenoiser *denoiser, float **channels, size_t frame_length);

#ifdef __cplusplus
} // extern "C"
#endif


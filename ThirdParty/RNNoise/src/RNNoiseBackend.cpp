#include "RNNoiseBackend.h"

#include <algorithm>
#include <memory>
#include <new>
#include <vector>

extern "C" {
#include "rnnoise.h"
}

namespace {
constexpr size_t kInputFrame = 160;              // 10 ms at 16 kHz
constexpr size_t kUpsampleFactor = 3;            // 16 kHz -> 48 kHz
constexpr size_t kOutputFrame = kInputFrame * kUpsampleFactor;
}

struct RNNoiseDenoiser {
    int channels;
    std::vector<DenoiseState *> states;
    std::vector<float> upsampleBuffer;
    std::vector<float> planarScratch;
};

static int ensure_buffers(RNNoiseDenoiser *denoiser, size_t frame_length) {
    if (!denoiser) {
        return -1;
    }
    const size_t perChannelUpsampled = frame_length * kUpsampleFactor;
    const size_t upsampleSize = perChannelUpsampled * static_cast<size_t>(denoiser->channels);
    if (denoiser->upsampleBuffer.size() < upsampleSize) {
        try {
            denoiser->upsampleBuffer.resize(upsampleSize);
        } catch (...) {
            return -1;
        }
    }
    const size_t planarSize = frame_length * static_cast<size_t>(denoiser->channels);
    if (denoiser->planarScratch.size() < planarSize) {
        try {
            denoiser->planarScratch.resize(planarSize);
        } catch (...) {
            return -1;
        }
    }
    return 0;
}

RNNoiseDenoiser *rnnoise_denoiser_create(int channels) {
    if (channels != 1 && channels != 2) {
        return nullptr;
    }

    std::unique_ptr<RNNoiseDenoiser> denoiser(new (std::nothrow) RNNoiseDenoiser);
    if (!denoiser) {
        return nullptr;
    }

    denoiser->channels = channels;
    denoiser->states.resize(static_cast<size_t>(channels), nullptr);

    for (int i = 0; i < channels; ++i) {
        DenoiseState *state = rnnoise_create(nullptr);
        if (!state) {
            rnnoise_denoiser_destroy(denoiser.release());
            return nullptr;
        }
        denoiser->states[static_cast<size_t>(i)] = state;
    }

    return denoiser.release();
}

void rnnoise_denoiser_destroy(RNNoiseDenoiser *denoiser) {
    if (!denoiser) {
        return;
    }
    for (DenoiseState *state : denoiser->states) {
        if (state) {
            rnnoise_destroy(state);
        }
    }
    delete denoiser;
}

static int process_block(RNNoiseDenoiser *denoiser, float **channels, size_t frame_length) {
    const size_t blocks = frame_length / kInputFrame;
    const size_t channelCount = static_cast<size_t>(denoiser->channels);
    float *upsampleBase = denoiser->upsampleBuffer.data();

    for (size_t block = 0; block < blocks; ++block) {
        for (size_t ch = 0; ch < channelCount; ++ch) {
            float *channelSamples = channels[ch] + block * kInputFrame;
            float *upsample = upsampleBase + ch * kOutputFrame;

            // Zero-order hold upsample to 48 kHz.
            for (size_t i = 0; i < kInputFrame; ++i) {
                const float sample = channelSamples[i];
                const size_t baseIndex = i * kUpsampleFactor;
                upsample[baseIndex] = sample;
                upsample[baseIndex + 1] = sample;
                upsample[baseIndex + 2] = sample;
            }

            rnnoise_process_frame(denoiser->states[ch], upsample, upsample);

            // Average groups of three samples to get back to 16 kHz.
            for (size_t i = 0; i < kInputFrame; ++i) {
                const size_t baseIndex = i * kUpsampleFactor;
                const float restored = (upsample[baseIndex] + upsample[baseIndex + 1] + upsample[baseIndex + 2]) / 3.0f;
                channelSamples[i] = restored;
            }
        }
    }

    return 0;
}

int rnnoise_denoiser_process_planar(RNNoiseDenoiser *denoiser, float **channels, size_t frame_length) {
    if (!denoiser || !channels) {
        return -1;
    }
    if (frame_length == 0 || frame_length % kInputFrame != 0) {
        return -1;
    }
    if (ensure_buffers(denoiser, frame_length) != 0) {
        return -1;
    }

    return process_block(denoiser, channels, frame_length);
}

int rnnoise_denoiser_process_interleaved(RNNoiseDenoiser *denoiser, float *pcm, size_t frame_length) {
    if (!denoiser || !pcm) {
        return -1;
    }
    if (frame_length == 0 || frame_length % kInputFrame != 0) {
        return -1;
    }
    if (ensure_buffers(denoiser, frame_length) != 0) {
        return -1;
    }

    const size_t channelCount = static_cast<size_t>(denoiser->channels);
    std::vector<float *> channelPtrs(channelCount, nullptr);

    for (size_t ch = 0; ch < channelCount; ++ch) {
        channelPtrs[ch] = denoiser->planarScratch.data() + ch * frame_length;
    }

    for (size_t i = 0; i < frame_length; ++i) {
        for (size_t ch = 0; ch < channelCount; ++ch) {
            channelPtrs[ch][i] = pcm[i * channelCount + ch];
        }
    }

    int result = process_block(denoiser, channelPtrs.data(), frame_length);
    if (result != 0) {
        return result;
    }

    for (size_t i = 0; i < frame_length; ++i) {
        for (size_t ch = 0; ch < channelCount; ++ch) {
            pcm[i * channelCount + ch] = channelPtrs[ch][i];
        }
    }

    return 0;
}


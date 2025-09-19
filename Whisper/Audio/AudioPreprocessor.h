#pragma once

#include <cstdint>
#include <vector>

#include "../API/iMediaFoundation.cl.h"

namespace Whisper
{
        class AudioPreprocessor
        {
        public:
                AudioPreprocessor();

                HRESULT initialize( const iAudioBuffer* source );

                const iAudioBuffer* cleanedBuffer() const noexcept;
                const iAudioBuffer* originalBuffer() const noexcept;

                const std::vector<float>& originalMono() const noexcept { return originalMono_; }
                const std::vector<float>& cleanedMono() const noexcept { return cleanedMono_; }
                const std::vector<float>& cleanedStereo() const noexcept { return cleanedStereo_; }
                bool hasStereo() const noexcept { return !cleanedStereo_.empty(); }
                int64_t timeOffset() const noexcept { return timeOffset_; }

        private:
                class BufferView final : public iAudioBuffer
                {
                        const std::vector<float>* mono_ = nullptr;
                        const std::vector<float>* stereo_ = nullptr;
                        int64_t timeOffset_ = 0;
                public:
                        void assign( const std::vector<float>* mono, const std::vector<float>* stereo, int64_t time ) noexcept;

                        // iAudioBuffer
                        uint32_t COMLIGHTCALL countSamples() const override final;
                        const float* COMLIGHTCALL getPcmMono() const override final;
                        const float* COMLIGHTCALL getPcmStereo() const override final;
                        HRESULT COMLIGHTCALL getTime( int64_t& rdi ) const override final;

                        // IUnknown compatibility (no-op reference counting for stack lifetime)
                        HRESULT COMLIGHTCALL QueryInterface( REFIID riid, void** ppvObject ) override final;
                        uint32_t COMLIGHTCALL AddRef() override final;
                        uint32_t COMLIGHTCALL Release() override final;
                };

                std::vector<float> originalMono_;
                std::vector<float> cleanedMono_;
                std::vector<float> cleanedStereo_;
                int64_t timeOffset_ = 0;

                BufferView cleanedView_;
                BufferView originalView_;
        };
}


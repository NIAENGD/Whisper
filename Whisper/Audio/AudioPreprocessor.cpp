#include "stdafx.h"
#include "AudioPreprocessor.h"

#include <algorithm>
#include <memory>

#include "../ThirdParty/RNNoise/include/RNNoiseBackend.h"
#include "../Whisper/audioConstants.h"
#include "../Whisper/voiceActivityDetection.h"

using namespace Whisper;

namespace
{
        inline size_t roundUp( size_t value, size_t block )
        {
                if( block == 0 )
                        return value;
                if( value == 0 )
                        return 0;
                const size_t rem = value % block;
                return rem == 0 ? value : ( value + block - rem );
        }
}

AudioPreprocessor::AudioPreprocessor() = default;

void AudioPreprocessor::BufferView::assign( const std::vector<float>* mono, const std::vector<float>* stereo, int64_t time ) noexcept
{
        mono_ = mono;
        stereo_ = stereo;
        timeOffset_ = time;
}

uint32_t COMLIGHTCALL AudioPreprocessor::BufferView::countSamples() const
{
        if( nullptr == mono_ )
                return 0;
        return static_cast<uint32_t>( mono_->size() );
}

const float* COMLIGHTCALL AudioPreprocessor::BufferView::getPcmMono() const
{
        if( nullptr == mono_ || mono_->empty() )
                return nullptr;
        return mono_->data();
}

const float* COMLIGHTCALL AudioPreprocessor::BufferView::getPcmStereo() const
{
        if( nullptr == stereo_ || stereo_->empty() )
                return nullptr;
        return stereo_->data();
}

HRESULT COMLIGHTCALL AudioPreprocessor::BufferView::getTime( int64_t& rdi ) const
{
        rdi = timeOffset_;
        return S_OK;
}

HRESULT COMLIGHTCALL AudioPreprocessor::BufferView::QueryInterface( REFIID riid, void** ppvObject )
{
        if( nullptr == ppvObject )
                return E_POINTER;
        *ppvObject = nullptr;

        if( riid == iAudioBuffer::iid() || riid == ComLight::IUnknown::iid() )
        {
                *ppvObject = this;
                AddRef();
                return S_OK;
        }
        return E_NOINTERFACE;
}

uint32_t COMLIGHTCALL AudioPreprocessor::BufferView::AddRef()
{
        return 1;
}

uint32_t COMLIGHTCALL AudioPreprocessor::BufferView::Release()
{
        return 1;
}

const iAudioBuffer* AudioPreprocessor::cleanedBuffer() const noexcept
{
        return &cleanedView_;
}

const iAudioBuffer* AudioPreprocessor::originalBuffer() const noexcept
{
        return &originalView_;
}

HRESULT AudioPreprocessor::initialize( const iAudioBuffer* source )
{
        if( nullptr == source )
                return E_POINTER;

        const uint32_t sampleCount = source->countSamples();
        if( sampleCount == 0 )
                return OLE_E_BLANK;

        const float* mono = source->getPcmMono();
        if( nullptr == mono )
                return E_FAIL;

        try
        {
                originalMono_.assign( mono, mono + sampleCount );
                cleanedMono_ = originalMono_;
        }
        catch( const std::bad_alloc& )
        {
                return E_OUTOFMEMORY;
        }

        const float* stereo = source->getPcmStereo();
        const bool hasStereo = stereo != nullptr;
        if( hasStereo )
        {
                try
                {
                        cleanedStereo_.assign( stereo, stereo + static_cast<size_t>( sampleCount ) * 2 );
                }
                catch( const std::bad_alloc& )
                {
                        return E_OUTOFMEMORY;
                }
        }
        else
                cleanedStereo_.clear();

        CHECK( source->getTime( timeOffset_ ) );

        const size_t countSamples = cleanedMono_.size();
        if( countSamples == 0 )
                return OLE_E_BLANK;

        std::unique_ptr<RNNoiseDenoiser, decltype( &rnnoise_denoiser_destroy )> denoiser( rnnoise_denoiser_create( hasStereo ? 2 : 1 ), &rnnoise_denoiser_destroy );
        if( !denoiser )
                return E_OUTOFMEMORY;

        if( countSamples > 0 )
        {
                const size_t padded = roundUp( countSamples, 160 );

                if( hasStereo )
                {
                        std::vector<float> left( padded, 0.0f );
                        std::vector<float> right( padded, 0.0f );

                        for( size_t i = 0; i < countSamples; ++i )
                        {
                                left[ i ] = cleanedStereo_[ i * 2 ];
                                right[ i ] = cleanedStereo_[ i * 2 + 1 ];
                        }

                        float* channels[ 2 ] = { left.data(), right.data() };
                        if( rnnoise_denoiser_process_planar( denoiser.get(), channels, padded ) != 0 )
                                return E_FAIL;

                        for( size_t i = 0; i < countSamples; ++i )
                        {
                                const float l = left[ i ];
                                const float r = right[ i ];
                                cleanedStereo_[ i * 2 ] = l;
                                cleanedStereo_[ i * 2 + 1 ] = r;
                                cleanedMono_[ i ] = 0.5f * ( l + r );
                        }
                }
                else
                {
                        std::vector<float> monoPad( padded, 0.0f );
                        std::copy( cleanedMono_.begin(), cleanedMono_.end(), monoPad.begin() );
                        float* channel = monoPad.data();
                        if( rnnoise_denoiser_process_planar( denoiser.get(), &channel, padded ) != 0 )
                                return E_FAIL;
                        std::copy( monoPad.begin(), monoPad.begin() + countSamples, cleanedMono_.begin() );
                }
        }

        std::vector<float> mask( countSamples, 1.0f );
        if( countSamples >= VAD::FFT_POINTS )
        {
                        const size_t paddedVad = roundUp( countSamples, (size_t)VAD::FFT_POINTS );
                        std::vector<float> vadInput( paddedVad, 0.0f );
                        std::copy( cleanedMono_.begin(), cleanedMono_.end(), vadInput.begin() );

                        VAD vad;
                        vad.clear();

                        const size_t frames = paddedVad / VAD::FFT_POINTS;
                        for( size_t frame = 0; frame < frames; ++frame )
                        {
                                const size_t upto = ( frame + 1 ) * VAD::FFT_POINTS;
                                const size_t lastSpeech = vad.detect( vadInput.data(), upto );
                                const float weight = ( lastSpeech >= upto ) ? 1.0f : 0.1f;
                                const size_t start = frame * VAD::FFT_POINTS;
                                const size_t end = std::min( upto, countSamples );
                                std::fill( mask.begin() + start, mask.begin() + end, weight );
                        }
        }

        for( size_t i = 0; i < countSamples; ++i )
                cleanedMono_[ i ] *= mask[ i ];

        if( hasStereo )
        {
                for( size_t i = 0; i < countSamples; ++i )
                {
                        cleanedStereo_[ i * 2 ] *= mask[ i ];
                        cleanedStereo_[ i * 2 + 1 ] *= mask[ i ];
                }
        }

        cleanedView_.assign( &cleanedMono_, hasStereo ? &cleanedStereo_ : nullptr, timeOffset_ );
        originalView_.assign( &originalMono_, nullptr, timeOffset_ );

        return S_OK;
}


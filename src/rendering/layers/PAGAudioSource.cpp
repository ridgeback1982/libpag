#include "base/utils/TimeUtil.h"
#include "pag/file.h"
#include "pag/pag.h"
#include "rendering/utils/LockGuard.h"
#include "rendering/utils/ScopedLock.h"
#include "rendering/utils/media/FFAudioReader.h"
#include "rendering/utils/media/FFAudioResampler.h"

//ffmpeg
extern "C" {
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libavutil/audio_fifo.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
}

//zzy, do some enhance later here
namespace pag {

PAGAudioSource::PAGAudioSource(const std::string& path) {
  _ffAudioReader = std::make_unique<FFAudioReader>(path);
}

PAGAudioSource::~PAGAudioSource() {
    releaseSourceBuffer();
}

void PAGAudioSource::releaseSourceBuffer() {
    if (_sourceBuffer) {
        for (int i = 0; i < _ffAudioReader->getChannels(); i++) {
            if (_sourceBuffer[i]) {
                delete[] _sourceBuffer[i];
                _sourceBuffer[i] = NULL;
            }
        }
        delete[] _sourceBuffer;
    }
}

void PAGAudioSource::setSpeed(float speed) {
    if (_ffAudioReader) {
        _ffAudioReader->setSpeed(speed);
    }
}

void PAGAudioSource::setVolumeForMix(int volume) {
    printf("PAGAudioSource::setVolumeForMix, volume:%d\n", volume);
    _mixVolume = volume;
}

void PAGAudioSource::setCutFrom(int64_t timeMicroSec) {
    if (_ffAudioReader) {
        _ffAudioReader->setCutFrom(timeMicroSec);
    }
}

void PAGAudioSource::setCutTo(int64_t timeMicroSec) {
    if (_ffAudioReader) {
        _ffAudioReader->setCutTo(timeMicroSec);
    }
}

int PAGAudioSource::readAudioBySamples(int64_t samples, uint8_t** buffers, int bufferSize, int targetSampleRate, int targetFormat, int targetChannels) {
    if (targetSampleRate == 0 || targetChannels == 0)
        return 0;
    if (_ffAudioResampler == nullptr) {
        //do not change the format of the dst audio
        _ffAudioResampler = std::make_unique<FFAudioResampler>(targetSampleRate, targetChannels, targetFormat);
    }
    int samplerate = (int)_ffAudioReader->getSampleRate();
    int channels = _ffAudioReader->getChannels();
    int wanted_samples = (int)av_rescale_rnd(samples, samplerate, targetSampleRate, AV_ROUND_UP);
    if (_wantedSourceSamples < wanted_samples) {
        releaseSourceBuffer();
        if (channels <= 0)
            return 0;
        _sourceBuffer = new uint8_t*[channels];
        if (_sourceBuffer == NULL)
            return 0;
        
        _wantedSourceSamples = wanted_samples;
        _sourceBufferSize = _ffAudioReader->getBytesPerSample() * _wantedSourceSamples;
        for (int i = 0; i < channels; i++) {
            _sourceBuffer[i] = new uint8_t[_sourceBufferSize];
            if (_sourceBuffer[i] == NULL) {
                delete[] _sourceBuffer;
                _sourceBuffer = NULL;
                return 0;
            }
        }
    }
    
    int src_samples = _ffAudioReader->readSamples(_sourceBuffer, channels, _wantedSourceSamples);
    if (src_samples <= 0) {
        return 0;
    }
    
    //resample/enhance here
    auto srcSampleRate = _ffAudioReader->getSampleRate();
    auto srcFormat = (AVSampleFormat)_ffAudioReader->getFormat();
    auto srcChannels = _ffAudioReader->getChannels();
    int dst_filled_samples = 0;
    if (srcSampleRate != targetSampleRate ||
        srcFormat != targetFormat ||
        srcChannels != targetChannels) {
        dst_filled_samples = _ffAudioResampler->process(buffers, bufferSize, (const uint8_t**)_sourceBuffer, _sourceBufferSize, _wantedSourceSamples, (int)srcSampleRate, srcChannels, srcFormat);
    } else {
        dst_filled_samples = src_samples;
        if (channels == 1) {
          for (int i = 0; i < targetChannels; i++) {
            memcpy(buffers[i], _sourceBuffer[0], _ffAudioReader->getBytesPerSample() * src_samples);
          }
        } else {
          if (targetChannels == 1) {
            memcpy(buffers[0], _sourceBuffer[0], _ffAudioReader->getBytesPerSample() * src_samples);
          } else {
            for(int i = 0; i < targetChannels; i++) {
              memcpy(buffers[i], _sourceBuffer[i], _ffAudioReader->getBytesPerSample() * src_samples);
            }
          }
        }
    }
    
    return dst_filled_samples;
}


}   // namespace pag

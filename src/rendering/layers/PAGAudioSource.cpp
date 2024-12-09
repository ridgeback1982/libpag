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
  if (_audioFifo != nullptr) {
    av_audio_fifo_free((AVAudioFifo*)_audioFifo);
  }
}

void PAGAudioSource::setVolumeForMix(int volume) {
    printf("PAGAudioSource::setVolumeForMix, volume:%d\n", volume);
    _mixVolume = volume;
    // if (_maxVolume == -1) {
    //     // get max volume
    //     _ffAudioReader->getMaxVolume();
    // }
}

int PAGAudioSource::readAudioBySamples(int64_t samples, uint8_t* buffer, int bufferSize, int targetSampleRate, int targetFormat, int targetChannels) {
    if (targetSampleRate == 0 || targetChannels == 0)
        return 0;
    if (_audioFifo == nullptr) {
        _audioFifo = av_audio_fifo_alloc((AVSampleFormat)targetFormat, targetChannels, (int)samples);
    }
    if (_ffAudioResampler == nullptr) {
        //do not change the format of the dst audio
        _ffAudioResampler = std::make_unique<FFAudioResampler>(targetSampleRate, targetChannels, targetFormat);
    }
    int samplerate = (int)_ffAudioReader->getSampleRate();
    int wanted_samples = (int)av_rescale_rnd(samples, samplerate, targetSampleRate, AV_ROUND_UP);
    if (_wantedSourceSamples < wanted_samples) {
        if (_sourceBuffer) {
            for (int i = 0; i < _ffAudioReader->getChannels(); i++) {
                if (_sourceBuffer[i]) {
                    delete[] _sourceBuffer[i];
                    _sourceBuffer[i] = NULL;
                }
            }
            delete[] _sourceBuffer;
        }
        int channels = _ffAudioReader->getChannels();
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
    
    int src_samples = _ffAudioReader->readSamples(_sourceBuffer, _wantedSourceSamples);
    if (src_samples <= 0) {
        return 0;
    }
    
    //resample/enhance here
    auto srcSampleRate = _ffAudioReader->getSamplesPerChannelOfDuration(1000000);
    auto srcFormat = (AVSampleFormat)_ffAudioReader->getFormat();
    auto srcChannels = 1;   //_ffAudioReader->getChannels();    //zzy, hardcode mono channel
    int dst_filled_samples = 0;
    if (srcSampleRate != targetSampleRate ||
        srcFormat != targetFormat ||
        srcChannels != targetChannels) {
        dst_filled_samples = _ffAudioResampler->resample(buffer, bufferSize, _sourceBuffer[0], _sourceBufferSize, _wantedSourceSamples, (int)srcSampleRate, srcChannels, srcFormat);
    } else {
        dst_filled_samples = src_samples;
        memcpy(buffer, _sourceBuffer[0], _ffAudioReader->getBytesPerSample() * src_samples);
    }

    //change speed here, use fifo
    
    return dst_filled_samples;
}


}   // namespace pag

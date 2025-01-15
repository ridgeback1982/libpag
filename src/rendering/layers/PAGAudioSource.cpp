#include <fstream>
#include <iostream>
#include "base/utils/TimeUtil.h"
#include "pag/file.h"
#include "pag/pag.h"
#include "rendering/utils/LockGuard.h"
#include "rendering/utils/ScopedLock.h"
#include "rendering/utils/media/FFAudioReader.h"
#include "rendering/utils/media/FFAudioResampler.h"
#include "rendering/utils/media/WRTCAudioGain.h"
#include "rendering/utils/media/FFAudioGain.h"

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

PAGAudioSource::PAGAudioSource(const std::string& path, AudioSourceType type) : _audioSourceType(type) {
  _ffAudioReader = std::make_unique<FFAudioReader>(path);
  _wAudioGain = std::make_unique<WRTCAudioGain>();
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
    if (_audioSourceType == AudioSourceType::Voice) {
        _mixVolume = std::min(volume, 12);      //10dB is normal loud level, 15 is 破音 level
    } else {
        _mixVolume = volume;
    }
    printf("PAGAudioSource::setVolumeForMix, volume:%d, type:%d\n", _mixVolume, _audioSourceType);
}

void PAGAudioSource::setSuppress(bool suppress) {
    _suppress = suppress;
    printf("PAGAudioSource::setSuppress, %d\n", suppress);
}

void PAGAudioSource::setLoop(bool loop) {
    if (_ffAudioReader) {
        _ffAudioReader->setLoop(loop);
    }
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

int PAGAudioSource::volumeForMix() const {
     return _mixVolume; 
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
    
    //have to loop to always get a frame, in case of big buffer in the filter(e.g. loudnorm filter of BGM enhance)
    int dst_filled_samples = 0;
    bool drains = false;
    do {
        dst_filled_samples = 0;
        int src_samples = _ffAudioReader->readSamples(_sourceBuffer, channels, _wantedSourceSamples);
        if (src_samples < 0) {
            break;
        } else if (src_samples > 0) {
            //resample here
            auto srcSampleRate = _ffAudioReader->getSampleRate();
            auto srcFormat = (AVSampleFormat)_ffAudioReader->getFormat();
            auto srcChannels = _ffAudioReader->getChannels();
            
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
                    for(int i = 0; i < targetChannels; i++) {
                        memcpy(buffers[i], _sourceBuffer[i], _ffAudioReader->getBytesPerSample() * src_samples);
                    }
                }
            }
        }
        if (dst_filled_samples < 0) {
            break;
        }
        
        //extra gain audio if needed(for voice)
        if (_audioSourceType == AudioSourceType::Voice) {
            if (_useWRTCAudioGainForVoice) {
                if (_wAudioGain) {
                    if (dst_filled_samples > 0) {
                        if (_wAudioGain->processInplace(dst_filled_samples, buffers, bufferSize, targetSampleRate, targetFormat, targetChannels) < 0) {
                            std::cerr << "processInplace failed" << std::endl;
                            dst_filled_samples = 0;
                            break;
                        }
                    } else {
                        drains = true;
                    }
                }
            }
        } else if (_audioSourceType == AudioSourceType::Bgm) {
            if (_suppress) {
                if (!_ffAudioGain) {
                    _ffAudioGain = std::make_unique<FFAudioGain>(targetSampleRate, targetChannels, targetFormat);
                    _ffAudioGain->setMode(FFGainMode::Suppress);
                    std::cout << "Create suppress audio gain for BGM" << std::endl;
                }
                
                AVFrame* input = nullptr;
                if (dst_filled_samples > 0) {
                    input = av_frame_alloc();
                    input->nb_samples = (int)dst_filled_samples;
                    input->format = static_cast<AVSampleFormat>(targetFormat);
                    av_channel_layout_default(&input->ch_layout, targetChannels);
                    input->sample_rate = targetSampleRate;
                    if (av_frame_get_buffer(input, 0) < 0) {
                        std::cerr << "Failed to allocate the input frame" << std::endl;
                        av_frame_free(&input);
                        return 0;
                    }
                    for (int i = 0; i < targetChannels; i++) {
                        memcpy(input->data[i], buffers[i], _ffAudioReader->getBytesPerSample() * dst_filled_samples);
                    }
                } else {
                    //no more data, so flush the filter
                    input = nullptr;    //for 断点debug
                }
                
                AVFrame* output = nullptr;
                if (dst_filled_samples > 0) {
                    _ffAudioGain->setOuputSamples(dst_filled_samples);
                }
                int ret = _ffAudioGain->process(input, &output);
                if (ret == ErrorCode::SUCCESS) {
                    int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(targetFormat));
                    for(int i = 0; i < targetChannels; i++) {
                        memcpy(buffers[i], output->data[i], bytesPerSample * output->nb_samples);
                    }
                    dst_filled_samples = output->nb_samples;
                } else if (ret == ErrorCode::AGAIN) {
                    //printf("failed to get gained audio samples, %d\n", ret);
                    dst_filled_samples = 0;     //data is not ready
                } else if (ret == ErrorCode::SOURCE_DRAINS) {
                    dst_filled_samples = 0; 
                    drains = true;
                } else {
                    dst_filled_samples = 0;
                    av_frame_free(&input);
                    av_frame_free(&output);
                    break;
                }
                av_frame_free(&input);
                av_frame_free(&output);
            } else {
                if (dst_filled_samples == 0) {
                    drains = true;
                }
            }
        }
    } while (dst_filled_samples == 0 && !drains);
    
    
    return dst_filled_samples;
}


}   // namespace pag

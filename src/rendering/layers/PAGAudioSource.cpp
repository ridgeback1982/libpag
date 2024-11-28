#include "base/utils/TimeUtil.h"
#include "pag/file.h"
#include "pag/pag.h"
#include "rendering/utils/LockGuard.h"
#include "rendering/utils/ScopedLock.h"
#include "rendering/utils/media/FFAudioReader.h"

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

PAGAudioSource::PAGAudioSource(const std::string& path, float volume) : _volume(volume) {
  _ffAudioReader = std::make_shared<FFAudioReader>(path);
}

PAGAudioSource::~PAGAudioSource() {
  if (_audioFifo != nullptr) {
    av_audio_fifo_free((AVAudioFifo*)_audioFifo);
  }
}

int PAGAudioSource::readAudioBySamples(int64_t samples, uint8_t* buffer, int bufferSize, int targetSampleRate, int targetFormat, int targetChannels) {
    if (targetSampleRate == 0 || targetChannels == 0)
        return 0;
    if (_audioFifo == nullptr) {
        _audioFifo = av_audio_fifo_alloc((AVSampleFormat)targetFormat, targetChannels, (int)samples);
    }
    if (_wantedSourceSamples < samples) {
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
        int samplerate = (int)_ffAudioReader->getSamplesPerChannelOfDuration(1000000);
        _wantedSourceSamples = (int)av_rescale_rnd(samples, samplerate, targetSampleRate, AV_ROUND_UP);
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
    int dst_filled_samples = 0;
    if (srcSampleRate != targetSampleRate) {
        SwrContext *swr_ctx = NULL;
        int ret;

        // Define input audio format
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, 1);
        enum AVSampleFormat in_sample_fmt = srcFormat;
        int in_sample_rate = (int)srcSampleRate;

        // Define output audio format
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 1);
        enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;
        int out_sample_rate = targetSampleRate;

        // Step 1: Allocate the SwrContext with swr_alloc_set_opts2
        swr_ctx = swr_alloc();
        if (!swr_ctx) {
            fprintf(stderr, "Could not allocate resampler context\n");
            return -1;
        }

        ret = swr_alloc_set_opts2(&swr_ctx,
                              &out_ch_layout, out_sample_fmt, out_sample_rate,
                              &in_ch_layout, in_sample_fmt, in_sample_rate,
                              0, NULL);
        if (ret < 0) {
            fprintf(stderr, "Failed to set options for SwrContext\n");
            return ret;
        }

        // Initialize the context
        if ((ret = swr_init(swr_ctx)) < 0) {
            fprintf(stderr, "Failed to initialize the resampler\n");
            swr_free(&swr_ctx);
            return ret;
        }

        uint8_t ** dst_data = new uint8_t*[1];
        *dst_data = buffer;
        int dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, in_sample_rate) + src_samples,
                                        out_sample_rate, in_sample_rate, AV_ROUND_UP);
        if (dst_nb_samples * av_get_bytes_per_sample((AVSampleFormat)targetFormat) > bufferSize) {
            return -1;
        }

        uint8_t ** src_data = new uint8_t*[1];
        *src_data = _sourceBuffer[0];
        int src_nb_samples = src_samples;
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
        //printf("audio swr_convert, samples:%d|%d, sr:%d|%d, size:%d|%d, ret:%d \n", dst_nb_samples, src_nb_samples, out_sample_rate, in_sample_rate, bufferSize, _sourceBufferSize, ret);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            return 0;
        }
        if (ret > 0) {
            //put in the converted data into the audio fifo
            uint8_t** audioChunks = new uint8_t*[1];
            audioChunks[0] = dst_data[0];
            if (av_audio_fifo_realloc((AVAudioFifo*)_audioFifo, av_audio_fifo_size((AVAudioFifo*)_audioFifo) + ret) < 0) {
              printf("cannot reallocate audio fifo\n");
              return 0;
            }
            if (av_audio_fifo_write((AVAudioFifo*)_audioFifo, (void**)audioChunks, ret) < 0) {
              printf("failed to write to audio fifo\n");
              return 0;
            }
            //check if there is some left in swr's internal cache, and must flush it out. Or it will be dropped next convert
            if (ret < dst_nb_samples) {
                ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)nullptr, 0);
                if (av_audio_fifo_realloc((AVAudioFifo*)_audioFifo, av_audio_fifo_size((AVAudioFifo*)_audioFifo) + ret) < 0) {
                    printf("cannot reallocate audio fifo\n");
                    return 0;
                }
                if (av_audio_fifo_write((AVAudioFifo*)_audioFifo, (void**)audioChunks, ret) < 0) {
                    printf("failed to write to audio fifo\n");
                    return 0;
                }
            }
            //read out the converted data from the audio fifo if there is enough
            if (av_audio_fifo_size((AVAudioFifo*)_audioFifo) >= samples) {
              if (av_audio_fifo_read((AVAudioFifo*)_audioFifo, (void**)audioChunks, (int)samples) < 0) {
                return 0;
              }
              ret = (int)samples;
            } else {
              ret = 0;
            }
            delete[] audioChunks;
        }
        
        dst_filled_samples = ret;
        free(src_data);
        free(dst_data);
        swr_free(&swr_ctx);
    } else {
        dst_filled_samples = src_samples;
        memcpy(buffer, _sourceBuffer[0], _ffAudioReader->getBytesPerSample() * src_samples);
    }
    
    return dst_filled_samples;
}


}   // namespace pag

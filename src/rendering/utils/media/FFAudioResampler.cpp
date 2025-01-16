#include "FFAudioResampler.h"
#include <iostream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
    #include <libswresample/swresample.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/audio_fifo.h>
}

//zzy
namespace pag {

FFAudioResampler::FFAudioResampler(int dstSampleRate, int dstChannels, int dstFormat): 
    _dstSampleRate(dstSampleRate), _dstChannels(dstChannels), _dstFormat(dstFormat) {
    _audioFifo = av_audio_fifo_alloc((AVSampleFormat)dstFormat, dstChannels, (int)dstSampleRate);
}

FFAudioResampler::~FFAudioResampler() {
    if (_audioFifo != nullptr) {
        av_audio_fifo_free((AVAudioFifo*)_audioFifo);
    }
}

int FFAudioResampler::process(uint8_t** dst, int dstLength,
        const uint8_t** src, int srcLength, int srcSamples, int srcSampleRate, int srcChannels, int srcFormat) {
    int ret = -1;
    if (srcSampleRate != _dstSampleRate ||
        srcChannels != _dstChannels ||
        srcFormat != _dstFormat)   {
        SwrContext *swr_ctx = NULL;

        // Define input audio format
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, srcChannels);
        enum AVSampleFormat in_sample_fmt = (AVSampleFormat)srcFormat;
        int in_sample_rate = (int)srcSampleRate;

        // Define output audio format
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, _dstChannels);
        enum AVSampleFormat out_sample_fmt = (AVSampleFormat)_dstFormat; //AV_SAMPLE_FMT_FLTP;
        int out_sample_rate = _dstSampleRate;

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

        int dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, in_sample_rate) + srcSamples,
                                        out_sample_rate, in_sample_rate, AV_ROUND_UP);
        if (dst_nb_samples * av_get_bytes_per_sample((AVSampleFormat)_dstFormat) > dstLength) {
            return -1;
        }

        int src_nb_samples = srcSamples;
        ret = swr_convert(swr_ctx, dst, dst_nb_samples, src, src_nb_samples);
        //printf("audio swr_convert, samples:%d|%d, sr:%d|%d, size:%d|%d, ret:%d \n", dst_nb_samples, src_nb_samples, out_sample_rate, in_sample_rate, bufferSize, _sourceBufferSize, ret);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            return 0;
        }
        if (ret > 0) {
            //put in the converted data into the audio fifo
            // uint8_t** audioChunks = new uint8_t*[1];
            // audioChunks[0] = dst_data[0];
            if (av_audio_fifo_realloc((AVAudioFifo*)_audioFifo, av_audio_fifo_size((AVAudioFifo*)_audioFifo) + ret) < 0) {
              std::cerr << "cannot reallocate audio fifo" << std::endl;
              return 0;
            }
            if (av_audio_fifo_write((AVAudioFifo*)_audioFifo, (void**)dst, ret) < 0) {
              std::cerr << "failed to write to audio fifo" << std::endl;
              return 0;
            }
            //check if there is some left in swr's internal cache, and must flush it out. Or it will be dropped next convert
            if (ret < dst_nb_samples) {
                //flush using null input
                ret = swr_convert(swr_ctx, dst, dst_nb_samples, (const uint8_t **)nullptr, 0);
                if (av_audio_fifo_realloc((AVAudioFifo*)_audioFifo, av_audio_fifo_size((AVAudioFifo*)_audioFifo) + ret) < 0) {
                    std::cerr << "cannot reallocate audio fifo" << std::endl;
                    return 0;
                }
                if (av_audio_fifo_write((AVAudioFifo*)_audioFifo, (void**)dst, ret) < 0) {
                    std::cerr << "failed to write to audio fifo" << std::endl;
                    return 0;
                }
            }
            //read out the converted data from the audio fifo if there is enough
            if (av_audio_fifo_size((AVAudioFifo*)_audioFifo) >= dst_nb_samples) {
              if (av_audio_fifo_read((AVAudioFifo*)_audioFifo, (void**)dst, (int)dst_nb_samples) < 0) {
                return 0;
              }
              ret = (int)dst_nb_samples;
            } else {
              ret = 0;
            }
            // delete[] audioChunks;
        }
        
        // free(src_data);
        // free(dst_data);
        swr_free(&swr_ctx);
    } else {
        int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(srcFormat));
        if (bytesPerSample * srcSamples > srcLength) {
            return -1;
        }
        if (srcChannels == 1) {
          for (int i = 0; i < _dstChannels; i++) {
            memcpy(dst[i], src[0], bytesPerSample * srcSamples);
          }
        } else {
//          if (_dstChannels == 1) {
//            memcpy(dst[0], src[0], bytesPerSample * srcSamples);
//          } else {
//            for(int i = 0; i < _dstChannels; i++) {
//              memcpy(dst[i], src[i], bytesPerSample * srcSamples);
//            }
//          }
            for(int i = 0; i < _dstChannels; i++) {
              memcpy(dst[i], src[i], bytesPerSample * srcSamples);
            }
        }
        ret = srcSamples;
    }
    return ret;
}

}

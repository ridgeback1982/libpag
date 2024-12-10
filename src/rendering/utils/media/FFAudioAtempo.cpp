#include "FFAudioAtempo.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
    #include <libswresample/swresample.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
}

//zzy
namespace pag {

FFAudioAtempo::FFAudioAtempo(int sampleRate, int channels, int format) :
    _sampleRate(sampleRate), _channels(channels), _format(format) {
    _fifo = av_audio_fifo_alloc((AVSampleFormat)_format, _channels, (int)_sampleRate);
    _filterGraph = avfilter_graph_alloc();
}

FFAudioAtempo::~FFAudioAtempo() {
    if (_fifo != nullptr) {
        av_audio_fifo_free(_fifo);
    }
    if (_filterGraph != nullptr) {
        avfilter_graph_free(&_filterGraph);
    }
}

int FFAudioAtempo::process(const AVFrame* input, AVFrame** output) {
    int ret = ErrorCode::SUCCESS;
    if (availableSamples() >= _outputSamples) {
        auto frame = av_frame_alloc();
        frame->nb_samples = _outputSamples;
        frame->format = _format;
        av_channel_layout_default(&frame->ch_layout, _channels);
        frame->sample_rate = _sampleRate;
        if (av_frame_get_buffer(frame, 0) < 0) {
          printf("Failed to allocate the input frame\n");
          av_frame_free(&frame);
          return -1;
        }
        if (av_audio_fifo_read((AVAudioFifo*)_fifo, (void**)frame->data, (int)_outputSamples) < 0) {
            printf("Failed to read from the FIFO\n");
            return -1;
        } 
        *output = frame;
    } else {
        //read data from atempo filter and judge whether it is enough to output
        if (_buffersrc_ctx == nullptr) {
            const AVFilter *buffersrc = avfilter_get_by_name("abuffer");
            if (!buffersrc) {
                printf("Required filters not available\n");
                return -1;
            }
            char src_args[512];
            snprintf(src_args, sizeof(src_args),
                    "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                    _sampleRate, _sampleRate, av_get_sample_fmt_name((AVSampleFormat)_format), _channels == 1 ? "mono" : "stereo");

            ret = avfilter_graph_create_filter(&_buffersrc_ctx, buffersrc, "in", src_args, NULL, _filterGraph);
            if (ret < 0) {
                printf("Error creating buffer source for input\n");
                return -1;
            }
        }

        if (_buffersink_ctx == nullptr) {
            const AVFilter *buffersink = avfilter_get_by_name("abuffersink");
            if (!buffersink) {
                printf("Required filters not available\n");
                return -1;
            }
            // Configure abuffersink (output sink)
            ret = avfilter_graph_create_filter(&_buffersink_ctx, buffersink, "out", NULL, NULL, _filterGraph);
            if (ret < 0) {
                printf("Error creating buffer sink for output\n");
                return -1;
            }
            // Configure buffersink to enforce same planar format(not change)
            enum AVSampleFormat out_sample_fmts[] = { (AVSampleFormat)_format, AV_SAMPLE_FMT_NONE };
            if (av_opt_set_int_list(_buffersink_ctx, "sample_fmts", out_sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN) < 0) {
                printf("Error setting output format on buffer sink\n");
                return -1;
            }
        }
        
        if (_atempo_ctx == nullptr) {
            const AVFilter *atempo = avfilter_get_by_name("atempo");
            if (!atempo) {
                printf("Required filters not available\n");
                return -1;
            }
            // Configure atempo (speed adjustment)
            char atempo_args[128];
            snprintf(atempo_args, sizeof(atempo_args), "%.2f", _speed);
            ret = avfilter_graph_create_filter(&_atempo_ctx, atempo, "atempo", atempo_args, NULL, _filterGraph);
            if (ret < 0) {
                printf("Error creating atempo filter\n");
                return -1;
            }

            // Link filters
            ret = avfilter_link(_buffersrc_ctx, 0, _atempo_ctx, 0);
            if (ret < 0) {
                printf("Error linking buffer source to atempo\n");
                return -1;
            }

            ret = avfilter_link(_atempo_ctx, 0, _buffersink_ctx, 0);
            if (ret < 0) {
                printf("Error linking atempo to buffer sink\n");
                return -1;
            }

            // Configure the filter graph
            ret = avfilter_graph_config(_filterGraph, NULL);
            if (ret < 0) {
                printf("Error configuring the filter graph\n");
                return -1;
            }
        }
        

        ret = av_buffersrc_add_frame(_buffersrc_ctx, (AVFrame*)input);
        if (ret < 0) {
            printf("Error adding input frame to buffer source\n");
            return -1;
        }
        // Pull filtered frame from the filtergraph
        AVFrame *filtered_frame = av_frame_alloc();
        while ((ret = av_buffersink_get_frame(_buffersink_ctx, filtered_frame)) >= 0) {
            // printf("Processed frame with %d samples\n", filtered_frame->nb_samples);

            if (av_audio_fifo_realloc(_fifo, av_audio_fifo_size(_fifo) + filtered_frame->nb_samples) < 0) {
              printf("cannot reallocate audio fifo\n");
              return -1;
            }
            if (av_audio_fifo_write(_fifo, (void**)filtered_frame->data, filtered_frame->nb_samples) < 0) {
              printf("failed to write to audio fifo\n");
              return -1;
            }
            // Release filtered frame for reuse
            av_frame_unref(filtered_frame);
        }

        if (availableSamples() >= _outputSamples) {
            // printf("FFAudioAtempo::process, get enough samples:%d|%d\n", availableSamples(), _outputSamples);
            auto frame = av_frame_alloc();
            frame->nb_samples = _outputSamples;
            frame->format = _format;
            av_channel_layout_default(&frame->ch_layout, _channels);
            frame->sample_rate = _sampleRate;
            if (av_frame_get_buffer(frame, 0) < 0) {
                printf("Failed to allocate the input frame\n");
                av_frame_free(&frame);
                return -1;
            }
            if (av_audio_fifo_read((AVAudioFifo*)_fifo, (void**)frame->data, (int)_outputSamples) < 0) {
                printf("Failed to read from the FIFO\n");
                return -1;
            } 
            *output = frame;
            ret = ErrorCode::SUCCESS;
        } else {
            //not enough data to output
            *output = nullptr;
            if (input == nullptr) {
                return ErrorCode::SOURCE_DRAINS;
            } else {
                return ErrorCode::AGAIN;
            }
        }
    }
    
    return ret;
}

int FFAudioAtempo::availableSamples() const {
    if (_fifo != nullptr) {
        return av_audio_fifo_size(_fifo);
    }
    return 0;
}

}

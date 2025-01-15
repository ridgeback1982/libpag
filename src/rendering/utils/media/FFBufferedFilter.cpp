#include "FFBufferedFilter.h"
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
}

//zzy
namespace pag {

FFBufferedFilter::FFBufferedFilter(int sampleRate, int channels, int format) :
    _sampleRate(sampleRate), _channels(channels), _format(format) {
    _fifo = av_audio_fifo_alloc((AVSampleFormat)_format, _channels, (int)_sampleRate);
    _filterGraph = avfilter_graph_alloc();
}

FFBufferedFilter::~FFBufferedFilter() {
    if (_fifo != nullptr) {
        av_audio_fifo_free(_fifo);
    }
    if (_filterGraph != nullptr) {
        avfilter_graph_free(&_filterGraph);
    }
}

const char* sample_rate_to_string(int sample_rate) {
    static char buffer[64];  // Thread-local static buffer to hold the result
    snprintf(buffer, sizeof(buffer), "%d", sample_rate);
    return buffer;
}

int FFBufferedFilter::setupBufferFilter() {
    int ret = 0;
    //read data from atempo filter and judge whether it is enough to output
    if (_buffersrc_ctx == nullptr) {
        const AVFilter *buffersrc = avfilter_get_by_name("abuffer");
        if (!buffersrc) {
            std::cerr << "Required filters not available" << std::endl;
            return -1;
        }
        char src_args[512];
        snprintf(src_args, sizeof(src_args),
                "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                _sampleRate, _sampleRate, av_get_sample_fmt_name((AVSampleFormat)_format), _channels == 1 ? "mono" : "stereo");

        ret = avfilter_graph_create_filter(&_buffersrc_ctx, buffersrc, "in", src_args, NULL, _filterGraph);
        if (ret < 0) {
            std::cerr << "Error creating buffer source for input" << std::endl;
            return -1;
        }
    }

    if (_buffersink_ctx == nullptr) {
        const AVFilter *buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersink) {
            std::cerr << "Required filters not available" << std::endl;
            return -1;
        }
        // Configure abuffersink (output sink)
        ret = avfilter_graph_create_filter(&_buffersink_ctx, buffersink, "out", NULL, NULL, _filterGraph);
        if (ret < 0) {
            std::cerr << "Error creating buffer sink for output" << std::endl;
            return -1;
        }
        // Configure buffersink to enforce same planar format(not change)
        enum AVSampleFormat out_sample_fmts[] = { (AVSampleFormat)_format, AV_SAMPLE_FMT_NONE };
        if (av_opt_set_int_list(_buffersink_ctx, "sample_fmts", out_sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN) < 0) {
            std::cerr << "Error setting output format on buffer sink" << std::endl;
            return -1;
        }
        const int64_t out_channel_layouts[] = { (int64_t)(_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO), -1 };
        if (av_opt_set_int_list(_buffersink_ctx, "channel_layouts", out_channel_layouts, -1,
            AV_OPT_SEARCH_CHILDREN) < 0) {
            std::cerr << "Error setting output format on buffer sink" << std::endl;
            return -1;
        }
        const int out_sample_rates[] = { _sampleRate , -1 };
        if (av_opt_set_int_list(_buffersink_ctx, "sample_rates", out_sample_rates, -1,
            AV_OPT_SEARCH_CHILDREN) < 0) {
            std::cerr << "Error setting output format on buffer sink" << std::endl;
            return -1;
        }
    }
    return ret;
}

int FFBufferedFilter::process(AVFrame* input, AVFrame** output) {
    int ret = ErrorCode::SUCCESS;
    ret = setupBufferFilter();
    if (ret != ErrorCode::SUCCESS) {
        std::cerr << "Error setting up buffer filter" << std::endl;
        return ret;
    }
    ret = setupCoreFilter();
    if (ret != ErrorCode::SUCCESS) {
        std::cerr << "Error setting up core filter" << std::endl;
        return ret;
    }
    
    //zzy, if input is null, I want to indicate flush
    //but if we call av_buffersrc_add_frame_flags with null input, it means EOF, and it will always return -22 and never recover
    //so if we want to flush, we just skip the call of av_buffersrc_add_frame_flags
    if (input) {
        ret = av_buffersrc_add_frame_flags(_buffersrc_ctx, input, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret < 0) {
            std::cerr << "Error adding input frame to buffer source" << std::endl;
            return -1;
        }
        _bufferedSamples ++;
    }
    

    // Pull filtered frame from the filtergraph
    AVFrame *filtered_frame = av_frame_alloc();
    while ((ret = av_buffersink_get_frame(_buffersink_ctx, filtered_frame)) >= 0) {
        // printf("Processed frame with %d samples\n", filtered_frame->nb_samples);

        if (av_audio_fifo_realloc(_fifo, av_audio_fifo_size(_fifo) + filtered_frame->nb_samples) < 0) {
            std::cerr << "cannot reallocate audio fifo" << std::endl;
            return -1;
        }
        if (av_audio_fifo_write(_fifo, (void**)filtered_frame->data, filtered_frame->nb_samples) < 0) {
            std::cerr << "failed to write to audio fifo" << std::endl;
            return -1;
        }
        //debug
//        std::cout << "[dbg]got one output, samples:" << filtered_frame->nb_samples << ", input times:" << _bufferedSamples << ", every input samples:" << input->nb_samples << ", availables:" << availableSamples() << std::endl;
        _bufferedSamples = 0;
        
        // Release filtered frame for reuse
        av_frame_unref(filtered_frame);
    }
    av_frame_free(&filtered_frame);
    av_frame_unref(input);

    int available = availableSamples();
    if (available >= _outputSamples && _outputSamples > 0) {
//        printf("FFBufferedFilter::process, get enough samples:%d|%d\n", available, _outputSamples);
        auto frame = av_frame_alloc();
        frame->nb_samples = _outputSamples;
        frame->format = _format;
        av_channel_layout_default(&frame->ch_layout, _channels);
        frame->sample_rate = _sampleRate;
        if (av_frame_get_buffer(frame, 0) < 0) {
            std::cerr << "Failed to allocate the input frame" << std::endl;
            av_frame_free(&frame);
            return -1;
        }
        if (av_audio_fifo_read((AVAudioFifo*)_fifo, (void**)frame->data, (int)_outputSamples) < 0) {
            std::cerr << "Failed to read from the FIFO" << std::endl;
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
    
    return ret;
}

int FFBufferedFilter::availableSamples() const {
    if (_fifo != nullptr) {
        return av_audio_fifo_size(_fifo);
    }
    return 0;
}

}

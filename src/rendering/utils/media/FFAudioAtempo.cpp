#include "FFAudioAtempo.h"
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

FFAudioAtempo::FFAudioAtempo(int sampleRate, int channels, int format) 
    : FFBufferedFilter(sampleRate, channels, format) {

}

FFAudioAtempo::~FFAudioAtempo() {

}

int FFAudioAtempo::setupCoreFilter() {
    int ret = 0;
    if (_atempo_ctx == nullptr) {
        const AVFilter *atempo = avfilter_get_by_name("atempo");
        if (!atempo) {
            std::cerr << "Required filters not available" << std::endl;
            return -1;
        }
        // Configure atempo (speed adjustment)
        char atempo_args[128];
        snprintf(atempo_args, sizeof(atempo_args), "%.2f", _speed);
        ret = avfilter_graph_create_filter(&_atempo_ctx, atempo, "atempo", atempo_args, NULL, _filterGraph);
        if (ret < 0) {
            std::cerr << "Error creating atempo filter" << std::endl;
            return -1;
        }

        // Link filters
        ret = avfilter_link(_buffersrc_ctx, 0, _atempo_ctx, 0);
        if (ret < 0) {
            std::cerr << "Error linking buffer source to atempo" << std::endl;
            return -1;
        }

        ret = avfilter_link(_atempo_ctx, 0, _buffersink_ctx, 0);
        if (ret < 0) {
            std::cerr << "Error linking atempo to buffer sink" << std::endl;
            return -1;
        }

        // Configure the filter graph
        ret = avfilter_graph_config(_filterGraph, NULL);
        if (ret < 0) {
            std::cerr << "Error configuring the filter graph" << std::endl;
            return -1;
        }
    }
    return ret;
}

}

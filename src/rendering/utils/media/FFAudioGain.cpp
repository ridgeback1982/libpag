#include "FFAudioGain.h"
#include "FFError.h"
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

FFAudioGain::FFAudioGain(int sampleRate, int channels, int format) 
    : FFBufferedFilter(sampleRate, channels, format) {
}

FFAudioGain::~FFAudioGain() {

}

int FFAudioGain::setupCoreFilter() {
    int ret = 0;
    if (_setUp == false) {
        char filter_desc[1024] = "";
        //zzy, I don't know why, loudnorm's output sample is distorted
//        snprintf(filter_desc, sizeof(filter_desc),
//            "loudnorm=I=-23:TP=-1.5:LRA=11,acompressor=threshold=-20dB:ratio=8:attack=5:release=100:makeup=%ddB",
//            _gain);
        snprintf(filter_desc, sizeof(filter_desc),
            "acompressor=threshold=-13dB:ratio=2:attack=20:release=500:makeup=%ddB",
            _gain);

        // Parse and configure the filter graph
        AVFilterInOut *inputs = NULL, *outputs = NULL;
        ret = avfilter_graph_parse2(_filterGraph, filter_desc, &inputs, &outputs);
        if (ret < 0) {
            std::cerr << "Error parsing filter graph: " << ret << std::endl;
            return -1;
        }

        // Connect the source to the corresponding input
        ret = avfilter_link(_buffersrc_ctx, 0, inputs->filter_ctx, inputs->pad_idx);
        if (ret < 0) {
            std::cerr << "Error linking buffer source for input" << std::endl;
            return -1;
        }

        // Connect the output
        ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, _buffersink_ctx, 0);
        if (ret < 0) {
            std::cerr << "Error linking buffer sink" << std::endl;
            return -1;
        }

        ret = avfilter_graph_config(_filterGraph, NULL);
        if (ret < 0) {
            std::cerr << "Error configuring filter graph" << std::endl;
            return -1;
        }

        _setUp = true;
    }
    
    return ret;
}


}

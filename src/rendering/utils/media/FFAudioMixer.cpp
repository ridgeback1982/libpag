#include "FFAudioMixer.h"
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


#define CHECK_ERR(err) if (err < 0) { printf("Error: %d\n", err); return -1; }


FFAudioMixer::FFAudioMixer(int sampleRate, int channelCount, int format) :
    _sampleRate(sampleRate), _channelCount(channelCount), _format(format)
{

}

FFAudioMixer::~FFAudioMixer() {

}

int FFAudioMixer::setupFilterGraph(const std::vector<AudioPreMixData> &srcBuffers, 
  AVFilterGraph* graph, AVFilterContext*** sources, AVFilterContext** sink) {
    int ret = 0;
    // Define filter description dynamically based on the number of inputs
    char filter_desc[1024] = "";
    char temp[256];

    for (size_t i = 0; i < srcBuffers.size(); i++) {
        snprintf(temp, sizeof(temp), "[%zu:a]volume=%ddB[a%zu];", i, srcBuffers[i].volume, i);
        strcat(filter_desc, temp);
    }

    //memset(temp, 0, sizeof(temp));
    for (size_t i = 0; i < srcBuffers.size(); i++) {
        snprintf(temp, sizeof(temp), "[a%zu]", i);
        strcat(filter_desc, temp);
    }

    snprintf(temp, sizeof(temp), "amix=inputs=%zu:duration=longest:dropout_transition=0", srcBuffers.size());
    strcat(filter_desc, temp);

    //zzy, don't append loudnorm here, because it delays filter output(EAGAIN)
    //The better way to gain audio is to use another filter separately
    // strcat(filter_desc, "[mixed];[mixed]");
    // snprintf(temp, sizeof(temp), "loudnorm=i=-16:tp=-1.5:lra=11");
    // strcat(filter_desc, temp);

    // Parse and configure the filter graph
    AVFilterInOut *inputs = NULL, *outputs = NULL;
    ret = avfilter_graph_parse2(graph, filter_desc, &inputs, &outputs);
    if (ret < 0) {
        std::cerr << "Error parsing filter graph:" << ret << std::endl;
        return -1;
    }

    // Create buffer sources and sinks
    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");

    AVFilterContext** source_ctxs = (AVFilterContext**)malloc(sizeof(AVFilterContext*) * srcBuffers.size());
    AVFilterInOut* tmp_inputs = inputs;
    for (int i = 0; i < (int)srcBuffers.size(); i++) {
        char args[512];
        snprintf(args, sizeof(args),
                "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                _sampleRate, _sampleRate, av_get_sample_fmt_name((AVSampleFormat)_format), _channelCount == 1 ? "mono" : "stereo");
        char name[16];
        snprintf(name, sizeof(name), "a%d", i);
        ret = avfilter_graph_create_filter(&source_ctxs[i], abuffersrc, name, args, NULL, graph);
        if (ret < 0) {
            std::cerr << "Error creating buffer source for input " << i << std::endl;
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return -1;
        }

        // Connect the source to the corresponding input
        ret = avfilter_link(source_ctxs[i], 0, tmp_inputs->filter_ctx, tmp_inputs->pad_idx);
        if (ret < 0) {
            std::cerr << "Error linking buffer source for input " << i << std::endl;
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return -1;
        }

        tmp_inputs = tmp_inputs->next;  // Advance to the next input
    }

    // Create buffer sink
    AVFilterContext* sink_ctx;
    ret = avfilter_graph_create_filter(&sink_ctx, abuffersink, "out", NULL, NULL, graph);
    if (ret < 0) {
        std::cerr << "Error creating buffer sink" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }
    // Configure buffersink to enforce same planar format(not change)
    enum AVSampleFormat out_sample_fmts[] = { (AVSampleFormat)_format, AV_SAMPLE_FMT_NONE };
    if (av_opt_set_int_list(sink_ctx, "sample_fmts", out_sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN) < 0) {
        std::cerr << "Error setting output format on buffer sink" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }
    const int64_t out_channel_layouts[] = { (int64_t)(_channelCount == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO), -1 };
    if (av_opt_set_int_list(sink_ctx, "channel_layouts", out_channel_layouts, -1,
        AV_OPT_SEARCH_CHILDREN) < 0) {
        std::cerr << "Error setting output format on buffer sink" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }
    const int out_sample_rates[] = { _sampleRate , -1 };
    if (av_opt_set_int_list(sink_ctx, "sample_rates", out_sample_rates, -1,
        AV_OPT_SEARCH_CHILDREN) < 0) {
        std::cerr << "Error setting output format on buffer sink" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }

    // Connect the output
    ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, sink_ctx, 0);
    if (ret < 0) {
        std::cerr << "Error linking buffer sink" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }

    ret = avfilter_graph_config(graph, NULL);
    if (ret < 0) {
        std::cerr << "Error configuring filter graph" << std::endl;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return -1;
    }

    *sources = source_ctxs;
    *sink = sink_ctx;
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return 0;
}

int FFAudioMixer::mixAudio(const std::vector<AudioPreMixData> &srcBuffers, uint8_t** dstBuffers, int bufferSize, int channels) {
  AVFilterGraph *filter_graph = avfilter_graph_alloc();
  int srcCount = (int)srcBuffers.size();
  //std::vector<AVFilterContext*> src_ctx(srcCount, nullptr);
  AVFilterContext** src_ctx = nullptr;
  AVFilterContext *sink_ctx = nullptr;
  int numSamples = bufferSize / av_get_bytes_per_sample(static_cast<AVSampleFormat>(_format));

  int ret = setupFilterGraph(srcBuffers, filter_graph, &src_ctx, &sink_ctx);
  if (ret < 0) {
      std::cerr << "failed to setup filter graph" << std::endl;
      return -1;
  }

  std::vector<AVFrame*> frames(srcCount, nullptr);
  for (int i = 0; i < srcCount; ++i) {
    frames[i] = av_frame_alloc();
    if (frames[i] == nullptr) {
      return -1;
    }
    frames[i]->nb_samples = numSamples;
    frames[i]->format = _format;
    av_channel_layout_default(&frames[i]->ch_layout, _channelCount);
    frames[i]->sample_rate = _sampleRate;

    if (av_frame_get_buffer(frames[i], 0) < 0) {
      return -1;
    }
    //memcpy(frames[i]->data[0], srcBuffers[i].buffer.get(), bufferSize);
    for(int j=0; j<_channelCount; j++) {
      memcpy(frames[i]->data[j], srcBuffers[i].buffers[j], bufferSize);
    }

    // Send frame to the source filter
    if (av_buffersrc_add_frame_flags(src_ctx[i], frames[i], AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
      return -1;
    }
  }

  // Retrieve and process mixed output from sink
  AVFrame *filt_frame = av_frame_alloc();
  while ((ret = av_buffersink_get_frame(sink_ctx, filt_frame)) >= 0) {
    // Process filt_frame (e.g., write to output)
    if (filt_frame->ch_layout.nb_channels == 1) {
      for (int i = 0; i < channels; i++) {
        memcpy(dstBuffers[i], filt_frame->data[0], std::min(filt_frame->linesize[0], bufferSize));
      }
    } else {
//      if (channels == 1) {
//        memcpy(dstBuffers[0], filt_frame->data[0], std::min(filt_frame->linesize[0], bufferSize));
//      } else {
//        for(int i = 0; i < filt_frame->ch_layout.nb_channels; i++) {
//          memcpy(dstBuffers[i], filt_frame->data[i], std::min(filt_frame->linesize[0], bufferSize));
//        }
//      }
        for(int i = 0; i < channels; i++) {
          memcpy(dstBuffers[i], filt_frame->data[i], std::min(filt_frame->linesize[0], bufferSize));
        }
    }
    
    //printf("Mixed frame with %d samples\n", filt_frame->nb_samples);
    av_frame_unref(filt_frame);
  }
  av_frame_free(&filt_frame);
    
//  if (ret == AVERROR(EAGAIN)) {
//    printf("EAGAIN \n");
//  }


  //finish
  for (auto frame : frames) {
    av_frame_unref(frame); 
    av_frame_free(&frame);
  }
  av_frame_free(&filt_frame);
  if (src_ctx) {
    for (int i = 0; i < srcCount; ++i) {
      avfilter_free(src_ctx[i]);
    }
    free(src_ctx);    //it is created in setupFilterGraph
  }
  if (sink_ctx) {
    avfilter_free(sink_ctx);
  }
  avfilter_graph_free(&filter_graph);
  return 0;
}


}    // namespace pag

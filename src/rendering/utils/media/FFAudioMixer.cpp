#include "FFAudioMixer.h"

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

FFAudioMixer::FFAudioMixer(int sampleRate, int channelCount, int format) :
    _sampleRate(sampleRate), _channelCount(channelCount), _format(format)
{

}

FFAudioMixer::~FFAudioMixer() {

}

int FFAudioMixer::mixAudio(const std::vector<uint8_t*> &srcBuffers, uint8_t* dstBuffer, int bufferSize) {
  AVFilterGraph *filter_graph = avfilter_graph_alloc();
  int srcCount = (int)srcBuffers.size();
  std::vector<AVFilterContext*> src_ctx(srcCount, nullptr);
  AVFilterContext *sink_ctx = nullptr;
  int numSamples = bufferSize / av_get_bytes_per_sample(static_cast<AVSampleFormat>(_format));

  const AVFilter *abuffer = avfilter_get_by_name("abuffer");
  const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
  const AVFilter *amix = avfilter_get_by_name("amix");
  if (!abuffer || !abuffersink || !amix) {
    fprintf(stderr, "Error finding required filters\n");
    return -1;
  }

  char amix_args[64];
  snprintf(amix_args, sizeof(amix_args), "inputs=%d", srcCount);
  // Create amix filter
  AVFilterContext *amix_ctx = nullptr;
  if (avfilter_graph_create_filter(&amix_ctx, amix, "amix", amix_args, nullptr, filter_graph) < 0) {
    fprintf(stderr, "Could not create amix filter\n");
    return -1;
  }

  // Create abuffer sources for each input stream
  for (int i = 0; i < srcCount; ++i) {
    char args[512];
    snprintf(args, sizeof(args),
                "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                _sampleRate, _sampleRate, av_get_sample_fmt_name((AVSampleFormat)_format), _channelCount == 1 ? "mono" : "stereo");

    if (avfilter_graph_create_filter(&src_ctx[i], abuffer, nullptr, args, nullptr, filter_graph) < 0) {
      fprintf(stderr, "Could not create buffer source for input %d\n", i);
      return -1;
    }

    // Link each abuffer to amix
    if (avfilter_link(src_ctx[i], 0, amix_ctx, i) < 0) {
      fprintf(stderr, "Error linking abuffer to amix\n");
      return -1;
    }
  }

  // Create buffer sink
  if (avfilter_graph_create_filter(&sink_ctx, abuffersink, "sink", nullptr, nullptr, filter_graph) < 0) {
    fprintf(stderr, "Could not create buffer sink\n");
    return -1;
  }

  // Link amix output to sink
  if (avfilter_link(amix_ctx, 0, sink_ctx, 0) < 0) {
    fprintf(stderr, "Error linking amix to sink\n");
    return -1;
  }

  // Configure the filter graph
  if (avfilter_graph_config(filter_graph, nullptr) < 0) {
    fprintf(stderr, "Error configuring the filter graph\n");
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
    memcpy(frames[i]->data[0], srcBuffers[i], bufferSize);

    // Send frame to the source filter
    if (av_buffersrc_add_frame_flags(src_ctx[i], frames[i], AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
      return -1;
    }
  }

  // Retrieve and process mixed output from sink
  AVFrame *filt_frame = av_frame_alloc();
  while (av_buffersink_get_frame(sink_ctx, filt_frame) >= 0) {
    // Process filt_frame (e.g., write to output)

    memcpy(dstBuffer, filt_frame->data[0], std::min(filt_frame->linesize[0], bufferSize));
    
    printf("Mixed frame with %d samples\n", filt_frame->nb_samples);
    //printf("Samples: %d,%d,%d,%d,%d,%d,%d,%d\n", dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3], dstBuffer[4], dstBuffer[5], dstBuffer[6], dstBuffer[7]);
    av_frame_unref(filt_frame);
  }


  //finish
  for (auto frame : frames) {
    av_frame_free(&frame);
  }
  av_frame_free(&filt_frame);
  avfilter_graph_free(&filter_graph);
  return 0;
}


}    // namespace pag

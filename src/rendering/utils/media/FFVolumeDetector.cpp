#include "FFVolumeDetector.h"
#include <regex>
#include <string>
#include <iostream>
#include <sstream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
    #include <libswresample/swresample.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
}

namespace pag {

// Custom log callback to capture volumedetect output
std::ostringstream log_stream;
#define UNUSED_VARIABLE(x) (void)(x)

void custom_log_callback(void *ptr, int level, const char *fmt, va_list vargs) {
    UNUSED_VARIABLE(ptr);
    if (level <= AV_LOG_INFO) { // Capture only INFO or higher priority logs
        char log_message[1024];
        vsnprintf(log_message, sizeof(log_message), fmt, vargs);
        printf("custom_log_callback1, log:%s\n", log_message);
        log_stream << log_message;
    }
}


FFVolumeDetector::FFVolumeDetector(const std::string& filePath) : _filePath(filePath) {
    init();
}

FFVolumeDetector::~FFVolumeDetector() {

}

int FFVolumeDetector::init() {
    _source.reset(new BestAudioSource(_filePath.c_str(), -1));
    _properties = _source->GetAudioProperties();
    
    // Set custom log callback
    av_log_set_callback(custom_log_callback);
    // Initialize FFmpeg
    avformat_network_init();
    return 0;
}

int FFVolumeDetector::getMaxVolumeForDuration(int64_t startMicroSec, int64_t durationMacroSec) {
    auto dstFormat = AV_SAMPLE_FMT_S16P;
    auto dstChannel = 1;   //hardcode mono channel
    auto dstSampleRate = _properties.SampleRate;
    auto srcFormat = _properties.Format;
    auto srcChannel = 1;
    auto srcSampleRate = _properties.SampleRate;
    //step 1: create filter graph
    int ret = -1;
    AVFilterGraph* filter_graph = avfilter_graph_alloc();
    AVFilterContext* buffer_src = nullptr;
    AVFilterContext* buffer_sink = nullptr;
    AVFilterContext* volumedetect = nullptr;
    // Initialize the buffer source and sink
    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    // Initialize the volume detect filter
    const AVFilter* vol_detect_filter = avfilter_get_by_name("volumedetect");
    // Create and configure the filter contexts
    char args[512];
    snprintf(args, sizeof(args),
                "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                dstSampleRate, dstSampleRate, av_get_sample_fmt_name((AVSampleFormat)dstFormat), dstChannel == 1 ? "mono" : "stereo");
    ret = avfilter_graph_create_filter(&buffer_src, abuffersrc, "in", args, nullptr, filter_graph);
    if (ret < 0) {
        printf("Could not create buffer of filter graph\n");
        return -1;
    }
    ret = avfilter_graph_create_filter(&buffer_sink, abuffersink, "out", nullptr, nullptr, filter_graph); 
    if (ret < 0) {
        printf("Could not create buffer of filter graph\n");
        return -1;
    }
    // Add the volumedetect filter
    ret = avfilter_graph_create_filter(&volumedetect, vol_detect_filter, "volumedetect", nullptr, nullptr, filter_graph);
    if (ret < 0) {
        printf("Could not create buffer of filter graph\n");
        return -1;
    }
    // Link the filters: buffer_src -> volumedetect -> buffer_sink
    ret = avfilter_link(buffer_src, 0, volumedetect, 0);
    if (ret < 0) {
        printf("Error connecting filters\n");
        return -1;
    }
    ret = avfilter_link(volumedetect, 0, buffer_sink, 0);
    if (ret < 0) {
        printf("Error connecting filters\n");
        return -1;
    }
    // Configure the filter graph
    ret = avfilter_graph_config(filter_graph, nullptr);
    if (ret < 0) {
        printf("Could not configure filter graph\n");
        return -1;
    }


    //step 2: read audio data
    int64_t startSample = startMicroSec * dstSampleRate / 1000000;
    int64_t durationSample = durationMacroSec * dstSampleRate / 1000000;
    AVFrame* frame = av_frame_alloc();
    frame->nb_samples = (int)durationSample;
    frame->format = _properties.Format;
    av_channel_layout_default(&frame->ch_layout, _properties.Channels);
    frame->sample_rate = _properties.SampleRate;
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        printf("Could not allocate frame data\n");
        return -1;
    }
    int64_t samples = _source->GetAudio(frame->data, startSample, durationSample);
    if (samples <= 0) {
        return -1;
    }


    //step 3: feed raw audio data to filter graph
    AVFrame* targetFrame = av_frame_alloc();
    //only handle mono audio
    if (_properties.Format == AV_SAMPLE_FMT_S16 || _properties.Format == AV_SAMPLE_FMT_S16P) {
        ret = av_frame_ref(targetFrame, frame);
        if (ret < 0) {
            printf("Failed to add reference to frame\n");
            return -1;
        }
        //memcpy(frame->data[0], buffer[0], frame->nb_samples * _properties.BytesPerSample);
    } else {
        targetFrame->nb_samples = (int)samples;
        targetFrame->format = dstFormat;
        av_channel_layout_default(&targetFrame->ch_layout, dstChannel);
        targetFrame->sample_rate = dstSampleRate;
        ret = av_frame_get_buffer(targetFrame, 0);
        if (ret < 0) {
            printf("Could not allocate frame data\n");
            return -1;
        }
        if (_resampler == nullptr) {
            _resampler = std::make_unique<FFAudioResampler>(dstSampleRate, dstChannel, dstFormat);
        }
        ret = _resampler->process(targetFrame->data[0], targetFrame->linesize[0], frame->data[0], frame->linesize[0], (int)samples,
            srcSampleRate, srcChannel, srcFormat);
        if (ret < 0) {
            printf("Failed to resample audio data\n");
            return -1;
        }
    }
    ret = av_buffersrc_add_frame_flags(buffer_src, targetFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        printf("Error while feeding frame to filter graph\n");
        return -1;
    }

    AVFrame* filtered_frame = av_frame_alloc();
    while (av_buffersink_get_frame(buffer_sink, filtered_frame) >= 0) {
//        AVDictionaryEntry* entry = NULL;
//        while ((entry = av_dict_get(filtered_frame->metadata, "", entry, AV_DICT_IGNORE_SUFFIX))) {
//            printf("%s: %s\n", entry->key, entry->value);
//        }
        av_frame_unref(filtered_frame);
    }
    av_frame_free(&filtered_frame);
    
    //step 4: release resources
    av_frame_unref(targetFrame);
    av_frame_free(&targetFrame);
    av_frame_free(&frame);
    avfilter_graph_free(&filter_graph);
    
    // Parse the captured log after free "filter_graph", it will flush all logs
    std::string log_output = log_stream.str();
    std::regex max_volume_regex("max_volume: (-?[0-9\\.]+)");
    std::regex mean_volume_regex("mean_volume: (-?[0-9\\.]+)");

    std::smatch match;
    if (std::regex_search(log_output, match, max_volume_regex)) {
        std::cout << "Max Volume: " << match[1] << " dB" << std::endl;
    }
    if (std::regex_search(log_output, match, mean_volume_regex)) {
        std::cout << "Mean Volume: " << match[1] << " dB" << std::endl;
    }
    
    return 0;
}


}

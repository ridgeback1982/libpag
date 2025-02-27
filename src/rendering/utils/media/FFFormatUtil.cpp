#include "FFFormatUtil.h"
#include <iostream>

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libavutil/avutil.h"
    #include "libavutil/channel_layout.h"
    #include "libavutil/audio_fifo.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
}

#include "FFError.h"

//zzy
namespace pag {

FFFormatUtil::FFFormatUtil(const std::string& url) {
    AVFormatContext *fmt_ctx = NULL;
    int video_stream_index = -1;

    // 初始化 FFmpeg 库
    avformat_network_init();

    // 打开输入文件
    if (avformat_open_input(&fmt_ctx, url.c_str(), NULL, NULL) < 0) {
      std::cerr << "Could not open input file:" << url << std::endl;
      avformat_free_context(fmt_ctx);
      return;
    }

    // 查找流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
      std::cerr << "Could not find stream information" << std::endl;
      avformat_close_input(&fmt_ctx);
      avformat_free_context(fmt_ctx);
      return;
    }

    int64_t duration = fmt_ctx->duration;
    if (duration != AV_NOPTS_VALUE) {
      _duration = duration / (double)AV_TIME_BASE;
    }

    // 查找视频流
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
      if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
        break;
      }
    }

    if (video_stream_index == -1) {
      std::cerr << "Could not find a video stream" << std::endl;
      avformat_close_input(&fmt_ctx);
      avformat_free_context(fmt_ctx);
      return;
    }

    // Get codec parameters for the video stream
    AVStream* video_stream = fmt_ctx->streams[video_stream_index];
    AVCodecParameters* codec_params = video_stream->codecpar;
    _width = codec_params->width;
    _height = codec_params->height;

    // Retrieve FPS
    AVRational frame_rate = video_stream->avg_frame_rate;
    _fps = (frame_rate.den && frame_rate.num) ? 
                 static_cast<double>(frame_rate.num) / frame_rate.den : 0;

    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
}

FFFormatUtil::~FFFormatUtil() {

}

} // namespace pag

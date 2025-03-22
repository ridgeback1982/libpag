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
#include "nas_config.h"
#include "utils/common_util.h"

//zzy
namespace pag {


FFFormatUtil::FFFormatUtil(const std::string& url) {
    int video_stream_index = -1;

    // 初始化 FFmpeg 库
    avformat_network_init();
    
    AVFormatContext *fmt_ctx = avformat_alloc_context();

    std::string new_url = url;
    if (starts_with(new_url, "http://192.168")) {
      std::string http = "http://";
      size_t pos = new_url.find(http);
      if (pos != std::string::npos) {
          new_url.insert(pos + http.length(), std::string(NAS_USERNAME) + ":" + std::string(NAS_PASSWORD) + "@");
//          std::cout << "append NAS username and password to url" << std::endl;
      }
    }
    // 打开输入文件
    if (avformat_open_input(&fmt_ctx, new_url.c_str(), NULL, NULL) < 0) {
      std::cerr << "Could not open input file:" << new_url << std::endl;
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

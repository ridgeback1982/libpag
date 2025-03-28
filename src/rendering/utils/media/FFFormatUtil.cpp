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
#include "utils/nas_config.h"

//zzy
namespace pag {


FFFormatUtil::FFFormatUtil(const std::string& url) {
    // 初始化 FFmpeg 库
    avformat_network_init();
    
    _fmt_ctx = avformat_alloc_context();
    std::string new_url = url;
    if (starts_with(new_url, NAS_HTTP_IP) || starts_with(new_url, NAS_HTTP_HOST)) {
      std::string http = "http://";
      size_t pos = new_url.find(http);
      if (pos != std::string::npos) {
          new_url.insert(pos + http.length(), std::string(NAS_USERNAME) + ":" + std::string(NAS_PASSWORD) + "@");
      }
    }
    // 打开输入文件
    if (avformat_open_input(&_fmt_ctx, new_url.c_str(), NULL, NULL) < 0) {
      std::cerr << "Could not open input file:" << new_url << std::endl;
      avformat_free_context(_fmt_ctx);
      return;
    }

    // 查找流信息
    if (avformat_find_stream_info(_fmt_ctx, NULL) < 0) {
      std::cerr << "Could not find stream information" << std::endl;
      avformat_close_input(&_fmt_ctx);
      avformat_free_context(_fmt_ctx);
      return;
    }

    int64_t duration = _fmt_ctx->duration;
    if (duration != AV_NOPTS_VALUE) {
      _duration = duration / (double)AV_TIME_BASE;
    }

    // 查找视频流
    for (unsigned i = 0; i < _fmt_ctx->nb_streams; i++) {
      if (_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        _video_stream_index = i;
        break;
      }
    }

    if (_video_stream_index == -1) {
//      std::cerr << "Could not find a video stream" << std::endl;
      avformat_close_input(&_fmt_ctx);
      avformat_free_context(_fmt_ctx);
      return;
    }

    // Get codec parameters for the video stream
    AVStream* video_stream = _fmt_ctx->streams[_video_stream_index];
    AVCodecParameters* codec_params = video_stream->codecpar;
    _width = codec_params->width;
    _height = codec_params->height;

    // Retrieve FPS
    AVRational frame_rate = video_stream->avg_frame_rate;
    _fps = (frame_rate.den && frame_rate.num) ? 
                 static_cast<double>(frame_rate.num) / frame_rate.den : 0;
}

FFFormatUtil::~FFFormatUtil() {
    avformat_close_input(&_fmt_ctx);
    avformat_free_context(_fmt_ctx);
}

void save_frame_as_png(AVFrame *frame, const char *filename) {
    const AVCodec *pngCodec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!pngCodec) {
        std::cerr << "PNG codec not found!" << std::endl;
        return;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(pngCodec);
    codecCtx->bit_rate = 400000;
    codecCtx->width = frame->width;
    codecCtx->height = frame->height;
    codecCtx->pix_fmt = AV_PIX_FMT_RGB24;
    codecCtx->time_base.num = 1;
    codecCtx->time_base.den = 25;

    if (avcodec_open2(codecCtx, pngCodec, nullptr) < 0) {
        std::cerr << "Could not open PNG encoder!" << std::endl;
        return;
    }

    // Allocate a packet
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate AVPacket" << std::endl;
        return;
    }

    if (avcodec_send_frame(codecCtx, frame) == 0) {
        if (avcodec_receive_packet(codecCtx, pkt) == 0) {
            FILE *file = fopen(filename, "wb");
            fwrite(pkt->data, 1, pkt->size, file);
            fclose(file);
            std::cout << "Saved PNG: " << filename << std::endl;
        }
    }

    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
}

int FFFormatUtil::writeVideoThumbnail(const std::string filePath) {
    if (_video_stream_index == -1) {
      return -1;
    }

    // Get codec parameters
    AVCodecParameters *pCodecParams = _fmt_ctx->streams[_video_stream_index]->codecpar;
    const AVCodec *pCodec = avcodec_find_decoder(pCodecParams->codec_id);
    if (!pCodec) {
        std::cerr << "Unsupported codec!" << std::endl;
        return -1;
    }

    // Open codec
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pCodecParams);
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return -1;
    }

    // Seek to 1-second position
    int64_t seek_target = _fmt_ctx->streams[_video_stream_index]->time_base.den / _fmt_ctx->streams[_video_stream_index]->time_base.num;
    av_seek_frame(_fmt_ctx, _video_stream_index, seek_target, AVSEEK_FLAG_BACKWARD);

    // Allocate frame buffers
    AVFrame *pFrame = av_frame_alloc();
    AVFrame *pFrameRGB = av_frame_alloc();
    if (!pFrame || !pFrameRGB) {
        std::cerr << "Could not allocate frame memory" << std::endl;
        return -1;
    }
    pFrameRGB->width = pCodecCtx->width;
    pFrameRGB->height = pCodecCtx->height;
    pFrameRGB->format = AV_PIX_FMT_RGB24;

    // Allocate buffer for RGB image
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

    // Convert YUV to RGB
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                                pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24,
                                                SWS_BILINEAR, nullptr, nullptr, nullptr);

    // Read frames
    AVPacket packet;
    while (av_read_frame(_fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == _video_stream_index) {
            avcodec_send_packet(pCodecCtx, &packet);
            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                          pFrameRGB->data, pFrameRGB->linesize);
                
                save_frame_as_png(pFrameRGB, filePath.c_str());
                av_packet_unref(&packet);
                break;  // Extract only the first frame
            }
        }
        av_packet_unref(&packet);
    }

    // Cleanup
    av_free(buffer);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    avcodec_close(pCodecCtx);
    sws_freeContext(sws_ctx);
    return 0;
}

} // namespace pag

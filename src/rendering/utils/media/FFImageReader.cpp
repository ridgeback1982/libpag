#include "FFImageReader.h"
#include <iostream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
    #include <libavfilter/avfilter.h>
    #include <libavutil/dict.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixfmt.h>
    #include <libavutil/pixdesc.h>
    #include <libavcodec/avcodec.h>
}

//zzy
namespace pag {

FFImageReader::FFImageReader(const std::string& filePath) {
    int video_stream_index = -1;
    avformat_network_init(); // 初始化 FFmpeg
    _formatContext = avformat_alloc_context();
    if (avformat_open_input(&_formatContext, filePath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Failed to open image file" << std::endl;
        return;
    }

    // 查找流信息
    if (avformat_find_stream_info(_formatContext, NULL) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&_formatContext);
        return;
    }

    // 查找视频流
    for (unsigned  i = 0; i < _formatContext->nb_streams; i++) {
        if (_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "Could not find a video stream" << std::endl;
        avformat_close_input(&_formatContext);
        return;
    }

    // 获取解码器
    const AVCodec* codec = avcodec_find_decoder(_formatContext->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Could not find codec for video stream" << std::endl;
        avformat_close_input(&_formatContext);
        return;
    }

    _codecContext = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(_codecContext, _formatContext->streams[video_stream_index]->codecpar);
    if (avcodec_open2(_codecContext, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        avcodec_free_context(&_codecContext);
        avformat_close_input(&_formatContext);
        return;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket pkt;
    while (av_read_frame(_formatContext, &pkt) >= 0) {
        if (pkt.stream_index == video_stream_index) {
            if (avcodec_send_packet(_codecContext, &pkt) == 0) {
                while (avcodec_receive_frame(_codecContext, frame) == 0) {
                    std::cout << "Got image frame" << std::endl;
                    _frame = av_frame_clone(frame);
                }
            }
        }
        av_packet_unref(&pkt);
    }
    av_frame_free(&frame);

    inited = true;
}

FFImageReader::~FFImageReader() {
    // 释放资源
    av_frame_free(&_frame);
    avcodec_free_context(&_codecContext);
    avformat_close_input(&_formatContext);
    avformat_network_deinit();
}

int FFImageReader::width() {
    if (inited == false) {
        return 0;
    }
    return _frame->width;
}

int FFImageReader::height() {
    if (inited == false) {
        return 0;
    }
    return _frame->height;
}

bool has_alpha_channel(AVPixelFormat pix_fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc) {
        std::cerr << "Unknown pixel format!" << std::endl;
        return false;
    }
    return desc->flags & AV_PIX_FMT_FLAG_ALPHA;
}

int get_bytes_per_pixel(AVPixelFormat pix_fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc) {
        std::cerr << "Invalid pixel format\n";
        return -1;
    }
    return desc->comp[0].step;  // Bytes per pixel per component
}

int FFImageReader::getColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (inited == false) {
        return UNKNOWN_ERROR;
    }
    if (x >= _frame->width || y >= _frame->height || x < 0 || y < 0) {
        return INVALID_ARGUMENT;
    }

    int bytes_per_pixel = get_bytes_per_pixel(_codecContext->pix_fmt);
    int index = y * _frame->linesize[0] + x * bytes_per_pixel;
    *r = _frame->data[0][index];
    *g = _frame->data[0][index + 1];
    *b = _frame->data[0][index + 2];
    if (has_alpha_channel(_codecContext->pix_fmt)) {
        if (a) {
            *a = _frame->data[0][index + 3];
        }
    }
    return SUCCESS;
}

}

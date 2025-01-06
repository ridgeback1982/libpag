
#include "FFAVCDecoder.h"
#include <cstdlib>

#ifdef PAG_USE_LIBAVC


namespace pag {

FFAVCDecoder::~FFAVCDecoder() {
  avcodec_free_context(&_codec_ctx);
  if (_yuv) {
    free(_yuv);
  }
}

bool FFAVCDecoder::onConfigure(const std::vector<HeaderData>& headers, std::string mime, int width, int height) {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "H.264 decoder not found\n");
        return false;
    }

    _codec_ctx = avcodec_alloc_context3(codec);
    if (!_codec_ctx) {
        fprintf(stderr, "Failed to allocate codec context\n");
        return false;
    }

    // Open codec
    if (avcodec_open2(_codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        return false;
    }

    size_t bufferLength = 0;
    for (auto& header : headers) {
        bufferLength += header.length;
    }
    size_t pos = 0;
    uint8_t *avbuffer = (uint8_t*)av_malloc(bufferLength);
    for (auto& header : headers) {
        memcpy(avbuffer + pos, header.data, header.length);
        pos += header.length;
    }
    AVPacket *packet = av_packet_alloc();
    packet->data = avbuffer;
    packet->size = bufferLength;

    if (avcodec_send_packet(_codec_ctx, packet) < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        av_packet_free(&packet);
        return false;
    }

    _yuvLength = width*height + width*height/2;
    _yuv = (uint8_t*)malloc(_yuvLength);
    if (_yuv == nullptr) {
        av_packet_free(&packet);
        return false;
    }
    memset(_yuv, 0, _yuvLength);

    _width = width;
    _height = height;

    av_packet_free(&packet);
    return true;
}

DecoderResult FFAVCDecoder::onSendBytes(void* bytes, size_t length, int64_t time)  {
    DecoderResult res = DecoderResult::Success;
    AVPacket *packet = av_packet_alloc();
    packet->data = (uint8_t*)bytes;
    packet->size = length;

    if (avcodec_send_packet(_codec_ctx, packet) < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        res = DecoderResult::Error;
    }

    av_packet_free(&packet);
    return res;
}

DecoderResult FFAVCDecoder::onEndOfStream() {
    DecoderResult res = DecoderResult::Success;
    if (avcodec_send_packet(_codec_ctx, nullptr) < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        res = DecoderResult::Error;
    }
    return res;
}

DecoderResult FFAVCDecoder::onDecodeFrame() {
    DecoderResult res = DecoderResult::Success;
    AVFrame *frame = av_frame_alloc();
    int ret = avcodec_receive_frame(_codec_ctx, frame);
    if (ret == 0) {
        printf("Decoded frame: %d x %d\n", frame->width, frame->height);
        uint8_t* ySrc = frame->data[0];
        uint8_t* yDst = _yuv;
        for (int i=0; i<frame->height; i++) {
            memcpy(yDst, ySrc, frame->width);
            ySrc += frame->linesize[0];
            yDst += _width;
        }
        uint8_t* uSrc = frame->data[1];
        uint8_t* uDst = _yuv + frame->width*frame->height;
        for (int i=0; i<frame->height/2; i++) {
            memcpy(uDst, uSrc, frame->width/2);
            uSrc += frame->linesize[1];
            uDst += _width/2;
        }
        uint8_t* vSrc = frame->data[2];
        uint8_t* vDst = _yuv + frame->width*frame->height + frame->width*frame->height/4;
        for (int i=0; i<frame->height/2; i++) {
            memcpy(vDst, vSrc, frame->width/2);
            vSrc += frame->linesize[2];
            vDst += _width/2;
        }
    } else if (ret == AVERROR(EAGAIN)) {
        res = DecoderResult::TryAgainLater;
    } else {
        res = DecoderResult::Error;
    }

    av_frame_free(&frame);
    return res;
}

std::unique_ptr<YUVBuffer> FFAVCDecoder::onRenderFrame() {
    auto output = std::make_unique<YUVBuffer>();
    output->data[0] = _yuv;
    output->data[1] = _yuv + _width*_height;
    output->data[2] = _yuv + _width*_height + _width*_height/4;
    output->lineSize[0] = _width;
    output->lineSize[1] = _width/2;
    output->lineSize[2] = _width/2;
    return output;
}

void FFAVCDecoder::onFlush() {
    // Reset the decoder (flush and reinitialize)
    avcodec_send_packet(_codec_ctx, NULL);  // Send NULL packet to flush
    AVFrame *frame = av_frame_alloc();
    while (avcodec_receive_frame(_codec_ctx, frame) == 0) {
        // Process any remaining frames after flushing
    }
    av_frame_free(&frame);
}

}


#endif
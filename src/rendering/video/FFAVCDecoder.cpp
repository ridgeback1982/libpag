
#include "FFAVCDecoder.h"
#include "base/utils/TimeUtil.h"
#include "platform/Platform.h"
#include "tgfx/utils/Clock.h"
#include <cstdlib>

#ifdef PAG_USE_FFAVC2

namespace pag {

FFAVCDecoder::~FFAVCDecoder() {
  avcodec_free_context(&_codec_ctx);
  if (_yuv) {
      free(_yuv);
      _yuv = nullptr;
  }
  if (_extraData) {
      free(_extraData);
      _extraData = nullptr;
  }
  if (_firstKeyFrame) {
      free(_firstKeyFrame);
      _firstKeyFrame = nullptr;
  }
}

bool FFAVCDecoder::onConfigure(const std::vector<HeaderData>& headers, std::string mime, int width, int height) {
    printf("FFAVCDecoder::onConfigure, mime:%s, width:%d, height:%d \n", mime.c_str(), width, height);
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
        avcodec_free_context(&_codec_ctx);
        return false;
    }

    size_t bufferLength = 0;
    for (auto& header : headers) {
        bufferLength += header.length;
    }
    if (_extraData) {
        free(_extraData);
    }
    _extraDataLength = (int)bufferLength;
    _extraData = (uint8_t*)malloc(_extraDataLength);
    if (!_extraData) {
        avcodec_free_context(&_codec_ctx);
        return false;
    }
    size_t pos = 0;
    for (auto& header : headers) {
        memcpy(_extraData + pos, header.data, header.length);
        pos += header.length;
    }

    _yuvLength = width*height + width*height/2;
    _yuv = (uint8_t*)malloc(_yuvLength);
    if (_yuv == nullptr) {
        return false;
    }
    memset(_yuv, 0, _yuvLength);

    _width = width;
    _height = height;

    return true;
}

int parseNalType(uint8_t* bitstream, int length) {
    int pos = 0;
    uint8_t nalType = 0;
    while (pos < length) {
        // Look for start code prefix: 0x000001 or 0x00000001
        if (pos + 3 < length &&
            bitstream[pos] == 0x00 && bitstream[pos + 1] == 0x00 && (bitstream[pos + 2] == 0x01 ||
             (bitstream[pos + 2] == 0x00 && pos + 4 < length && bitstream[pos + 3] == 0x01))) {

            // Adjust position to the NAL header
            pos += (bitstream[pos + 2] == 0x01) ? 3 : 4;

            if (pos < length) {
                uint8_t nalHeader = bitstream[pos];
                nalType = nalHeader & 0x1F; // Extract last 5 bits for NAL type
//                std::cout << "Found NAL Unit: Type = " << static_cast<int>(nalType) << std::endl;
                break;
            }
        }
        pos++;
    }
    return nalType;
}

DecoderResult FFAVCDecoder::onSendBytes(void* bytes, size_t length, [[maybe_unused]]int64_t time)  {
    DecoderResult res = DecoderResult::Success;
    uint8_t* input = (uint8_t*)bytes;
    int input_length = (int)length;
    if (!_firstKeyFrame) {
        if (parseNalType((uint8_t*)bytes, (int)length) == 5) {
            _firstKeyFrame = (uint8_t*)malloc(_extraDataLength + length);
            if (!_firstKeyFrame) {
                fprintf(stderr, "FFAVCDecoder::onSendBytes, first frame is not KEY frame\n");
                return DecoderResult::Error;
            }
            memcpy(_firstKeyFrame, _extraData, _extraDataLength);
            memcpy(_firstKeyFrame + _extraDataLength, bytes, length);
            input = _firstKeyFrame;
            input_length = _extraDataLength + (int)length;
        } else {
            //has to return Success. If return Error, decode will fail; if return TryAgainLater, the same nal will be input again
            return res;
        }
    }
    
    AVPacket *packet = av_packet_alloc();
    packet->data = input;
    packet->size = input_length;

    int ret = 0;
    // tgfx::Clock clock = {};
    if ((ret = avcodec_send_packet(_codec_ctx, packet)) < 0) {
        if (ret != AVERROR_EOF) {
            fprintf(stderr, "Error sending packet to decoder(onSendBytes), %d\n", ret);
            res = DecoderResult::Error;
        } else {
            //return Success if AVERROR_EOF(caused by onFlush)
            res = DecoderResult::Success;
        }
    }
    // auto sendTime = clock.elapsedTime();
    // static int logcount = 0;
    // if (logcount++ % 50 == 0) {
    //     printf("FFAVCDecoder::onSendBytes, cost:%d\n", sendTime/1000);
    // }

    av_packet_free(&packet);
    return res;
}

DecoderResult FFAVCDecoder::onEndOfStream() {
    DecoderResult res = DecoderResult::Success;
    int ret = 0;
    if ((ret = avcodec_send_packet(_codec_ctx, nullptr)) < 0) {
        fprintf(stderr, "Error sending packet to decoder(onEndOfStream), %d\n", ret);
        res = DecoderResult::Error;
    }
    _inputDrain = true;
    return res;
}

DecoderResult FFAVCDecoder::onDecodeFrame() {
    DecoderResult res = DecoderResult::Success;
    AVFrame *frame = av_frame_alloc();
    int ret = avcodec_receive_frame(_codec_ctx, frame);
    if (ret == 0) {
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
//        if (_decoded) {
//            printf("Decoded frame: %d x %d, overlap\n", frame->width, frame->height);
//        } else {
//            printf("Decoded frame: %d x %d, normal\n", frame->width, frame->height);
//        }
        _decoded = true;
    } else if (ret == AVERROR(EAGAIN)) {
        res = _inputDrain ? DecoderResult::EndOfStream : DecoderResult::TryAgainLater;
    } else {
        res = DecoderResult::Error;
    }

    av_frame_free(&frame);
    return res;
}

std::unique_ptr<YUVBuffer> FFAVCDecoder::onRenderFrame() {
    if (_decoded) {
        auto output = std::make_unique<YUVBuffer>();
        output->data[0] = _yuv;
        output->data[1] = _yuv + _width*_height;
        output->data[2] = _yuv + _width*_height + _width*_height/4;
        output->lineSize[0] = _width;
        output->lineSize[1] = _width/2;
        output->lineSize[2] = _width/2;
        
        _decoded = false;
        return output;
    } else {
        return nullptr;
    }
}

void FFAVCDecoder::onFlush() {
    // Just consume all frames(no need to reset decoder)
    if (_codec_ctx) {
//        printf("FFAVCDecoder::onFlush\n");
        //zzy, do not send packet with NULL, because it indicates stream EOF, and decoder will not output anything after that
//        avcodec_send_packet(_codec_ctx, NULL);  // Send NULL packet to flush
        AVFrame *frame = av_frame_alloc();
        while (avcodec_receive_frame(_codec_ctx, frame) == 0) {
            // Process any remaining frames after flushing
        }
        av_frame_free(&frame);

        if (_firstKeyFrame) {
            free(_firstKeyFrame);
            _firstKeyFrame = nullptr;
        }
        
        _inputDrain = false;
        _decoded = false;
    }
}

}


#endif

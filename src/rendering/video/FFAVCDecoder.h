#pragma once

#ifdef PAG_USE_FFAVC2

#include "base/utils/Log.h"
#include "pag/decoder.h"
#include "pag/types.h"
#include "tgfx/core/Data.h"
#include "tgfx/utils/Buffer.h"

extern "C" {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>

#pragma clang diagnostic pop
}

namespace pag {
/**
 * FFAVCDecoder is ffmpeg implemented, zzy
 */
class FFAVCDecoder : public SoftwareDecoder {
public:
  ~FFAVCDecoder() override;

  bool onConfigure(const std::vector<HeaderData>& headers, std::string mime, int width,
                   int height) override;

  DecoderResult onSendBytes(void* bytes, size_t length, int64_t frame) override;

  DecoderResult onDecodeFrame() override;

  DecoderResult onEndOfStream() override;

  void onFlush() override;

  std::unique_ptr<YUVBuffer> onRenderFrame() override;

private:
  AVCodecContext* _codec_ctx = nullptr;
  uint8_t* _extraData = nullptr;
  int _extraDataLength = 0;
  uint8_t* _firstKeyFrame = nullptr;
  uint8_t* _yuv = nullptr;
  int      _yuvLength = 0;   
  int   _width = 0;
  int   _height = 0;
  bool _decoded = false;
  bool _inputDrain = false;
  
};



}   //namespace
#endif

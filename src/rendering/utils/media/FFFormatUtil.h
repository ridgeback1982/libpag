#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
}

namespace pag {

class PAG_API FFFormatUtil {
public:
    FFFormatUtil(const std::string& url);
    ~FFFormatUtil();

    int width() { return _width; }
    int height() { return _height; }
    int fps() { return _fps; }
    int durationMS() { return std::floor(_duration * 1000); }

    int writeVideoThumbnail(int width, int height, const std::string filePath);

private:
    AVFormatContext* _fmt_ctx;
    int _video_stream_index = -1;
    int _width = 0;
    int _height = 0;
    int _fps = 0;
    float _duration = 0.0f;
};


}  // namespace pag

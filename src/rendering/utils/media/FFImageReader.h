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

#include "FFError.h"

//zzy
namespace pag {

class FFImageReader {
public:
    FFImageReader(const std::string& filePath);
    ~FFImageReader();

    int width();
    int height();
    int getColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a = nullptr);

protected:
    AVFormatContext* _formatContext = nullptr;
    AVCodecContext* _codecContext = nullptr;
    AVFrame* _frame = nullptr;
    bool inited = false;
};

}
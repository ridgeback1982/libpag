#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

extern "C" {
    #include <libavfilter/avfilter.h>
    #include <libavutil/audio_fifo.h>
}

#include "FFBufferedFilter.h"
#include "FFError.h"

//zzy
namespace pag {

class FFAudioAtempo : public FFBufferedFilter {
public:
    FFAudioAtempo(int sampleRate, int channels, int format);
    virtual ~FFAudioAtempo();
    int setupCoreFilter() override;

    void setSpeed(float speed) { _speed = speed; }

private:
    float _speed = 1.0f;
    AVFilterContext *_atempo_ctx = NULL;
};




}
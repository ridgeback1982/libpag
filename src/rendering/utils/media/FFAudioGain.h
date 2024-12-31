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


class PAG_API FFAudioGain : public FFBufferedFilter {
public:
    FFAudioGain(int sampleRate, int channels, int format);
    virtual ~FFAudioGain();
    int setupCoreFilter() override;

    void setGain(int gain) { _gain = gain; }

private:
    int _gain = 5;
    bool _setUp = false;
//    AVFilterContext *_gain_ctx = NULL;
};



}

#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

extern "C" {
    #include <libavfilter/avfilter.h>
    #include <libavutil/audio_fifo.h>
}

#include "FFError.h"

//zzy
namespace pag {

class FFBufferedFilter {
public:
    FFBufferedFilter(int sampleRate, int channels, int format);
    virtual ~FFBufferedFilter();

    void setOuputSamples(int samples) { _outputSamples = samples; }
    int process(AVFrame* input, AVFrame** output);
    int availableSamples() const;
    int setupBufferFilter(AVFrame* input);

    virtual int setupCoreFilter() = 0;

protected:
    int _dstSampleRate = 0;
    int _dstChannels = 0;
    int _dstFormat = 0;
    AVAudioFifo* _fifo = nullptr;
    int _outputSamples = 0;
    AVFilterGraph* _filterGraph = nullptr;
    AVFilterContext* _filter_ctx = nullptr;
    AVFilterContext* _buffersrc_ctx = NULL;
    AVFilterContext* _buffersink_ctx = NULL;
};




}

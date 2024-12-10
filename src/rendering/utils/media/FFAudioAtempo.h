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

class FFAudioAtempo {
public:
    FFAudioAtempo(int sampleRate, int channels, int format);
    ~FFAudioAtempo();

    void setSpeed(float speed) { _speed = speed; }
    void setOuputSamples(int samples) { _outputSamples = samples; }

    int process(const AVFrame* input, AVFrame** output);
    int availableSamples() const;

private:
    int _sampleRate = 0;
    int _channels = 0;
    int _format = 0;
    float _speed = 1.0f;
    AVAudioFifo* _fifo = nullptr;
    int _outputSamples = 0;
    AVFilterGraph* _filterGraph = nullptr;
    AVFilterContext *_buffersrc_ctx = NULL;
    AVFilterContext *_buffersink_ctx = NULL;
    AVFilterContext *_atempo_ctx = NULL;
};




}
#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

extern "C" {
    #include <libswresample/swresample.h>
    #include <libavutil/avutil.h>
    #include <libavutil/samplefmt.h>
    #include <libavutil/opt.h>
}

//zzy
namespace pag {


class PAG_API WRTCAudioGain {
public:
    WRTCAudioGain();
    virtual ~WRTCAudioGain();

    int processInplace(int64_t samples, uint8_t** buffers, int bufferSize, int sampleRate, int format, int channels);

private:
    int agcProcess(int16_t *buffer, uint32_t sampleRate, int samplesCount, int16_t agcMode);
    int convert_Float_to_S16(const uint8_t **float_data, uint8_t **s16_data, int nb_samples, int sample_rate, int channels);
    int convert_S16_to_Float(const uint8_t **s16_data, uint8_t **float_data, int nb_samples, int sample_rate, int channels);

    uint8_t** _s16Buffer = nullptr;
    int _s16BufferedSamples = 0;
    void* _agcInst = nullptr;
    
    FILE* _dumpFile = nullptr;
};



}

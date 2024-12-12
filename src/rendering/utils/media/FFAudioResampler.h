#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

//zzy
namespace pag {

class PAG_API FFAudioResampler {
public:
    FFAudioResampler(int dstSampleRate, int dstChannels, int dstFormat);
    ~FFAudioResampler();

    int process(uint8_t** dst, int dstLength, 
        const uint8_t** src, int srcLength, int srcSamples, int srcSampleRate, int srcChannels, int srcFormat);

private:
    int _dstSampleRate;
    int _dstChannels;
    int _dstFormat;
    void* _audioFifo = nullptr;
};


}  // namespace pag
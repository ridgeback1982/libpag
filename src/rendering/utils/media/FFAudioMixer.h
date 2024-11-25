#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

//zzy
namespace pag {

class FFAudioMixer {
public:
    FFAudioMixer(int sampleRate, int channelCount, int format);
    ~FFAudioMixer();

    int mixAudio(const std::vector<uint8_t*> &srcBuffers, uint8_t* dstBuffer, int bufferSize);

private:
    int _sampleRate = 0;
    int _channelCount = 0;
    int _format = 0;
};


}  // namespace pag
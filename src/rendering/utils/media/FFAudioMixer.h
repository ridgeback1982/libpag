#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

extern "C" {
    #include <libavfilter/avfilter.h>
}

//zzy
namespace pag {

struct AudioPreMixData {
    int volume = 0;
    //std::unique_ptr<uint8_t[]> buffer;
    uint8_t* buffer;
};

class PAG_API FFAudioMixer {
public:
    FFAudioMixer(int sampleRate, int channelCount, int format);
    ~FFAudioMixer();

    int mixAudio(const std::vector<AudioPreMixData> &srcBuffers, uint8_t* dstBuffer, int bufferSize);

private:
    int setupFilterGraph(const std::vector<AudioPreMixData> &srcBuffers, AVFilterGraph* graph, AVFilterContext*** sources, AVFilterContext** sink);

    int _sampleRate = 0;
    int _channelCount = 0;
    int _format = 0;
};


}  // namespace pag

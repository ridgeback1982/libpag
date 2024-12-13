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
    int channels = 0;
    uint8_t** buffers = nullptr;
};

class PAG_API FFAudioMixer {
public:
    FFAudioMixer(int sampleRate, int channelCount, int format);
    ~FFAudioMixer();

    int mixAudio(const std::vector<AudioPreMixData> &srcBuffers, uint8_t** dstBuffers, int bufferSize, int channels);

private:
    int setupFilterGraph(const std::vector<AudioPreMixData> &srcBuffers, AVFilterGraph* graph, AVFilterContext*** sources, AVFilterContext** sink);

    int _sampleRate = 0;
    int _channelCount = 0;
    int _format = 0;
};


}  // namespace pag

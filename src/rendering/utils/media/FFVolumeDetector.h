#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"
#include "bestsourceaudio/audiosource.h"
#include "FFAudioResampler.h"

//zzy
namespace pag {

#define MAX_VOLUME (20)
#define MIN_VOLUME (-60)

class PAG_API FFVolumeDetector {
public:
    FFVolumeDetector(const std::string& filePath);
    ~FFVolumeDetector();

    int getMaxVolumeForDuration(int64_t startMicroSec, int64_t durationMacroSec);
    
    // static int test(const std::string& filePath);

private:
    int init();
    
    std::string _filePath;
    std::unique_ptr<BestAudioSource> _source;
    AudioProperties _properties;
    std::unique_ptr<FFAudioResampler> _resampler;
};

}

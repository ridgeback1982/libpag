#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

#include "bestsourceaudio/audiosource.h"

//zzy
namespace pag {

class FFAudioReader {
public:
  FFAudioReader(const std::string& path);
  ~FFAudioReader();

  int getFormat();
  int getChannels();
  int64_t getSamplesPerChannelOfDuration(int64_t durationMicroSec);
  int getBytesPerSample();
  int readSamples(uint8_t** data, int sampleCount);

  static void test(const std::string inputPath, const std::string outputPath);

  void seek(int64_t timeMicroSec);
  //TBD: set cut from and cut to, seek must be in the middle

private:
  std::unique_ptr<BestAudioSource> _source;
  AudioProperties _properties;
  int64_t _start;
};

}  // namespace pag

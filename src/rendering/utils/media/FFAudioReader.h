#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

#include "bestsourceaudio/audiosource.h"
#include "FFVolumeDetector.h"
#include "FFAudioAtempo.h"

//zzy
namespace pag {

class  FFAudioReader {
public:
  FFAudioReader(const std::string& path);
  ~FFAudioReader();

  int getFormat();
  int getChannels();
  int getSampleRate();
  int64_t getSamplesPerChannelOfDuration(int64_t durationMicroSec);
  int getBytesPerSample();
  int readSamples(uint8_t** data, int channels, int sampleCount);
  int getMaxVolume();
  void setSpeed(float speed) { _speed = speed; }

  static void test(const std::string inputPath, const std::string outputPath);

  void setCutFrom(int64_t timeMicroSec);
  void setCutTo(int64_t timeMicroSec);
  //TBD: set cut from and cut to, seek must be in the middle

private:
  std::unique_ptr<BestAudioSource> _source;
  std::unique_ptr<FFVolumeDetector> _volumeDetector;
  std::unique_ptr<FFAudioAtempo> _atempo;
  AudioProperties _properties;
  int64_t _from;
  int64_t _to;
  float _speed;

};

}  // namespace pag


#include "FFAudioReader.h"

namespace pag {

FFAudioReader::FFAudioReader(const std::string& path) {
  _source.reset(new BestAudioSource(path.c_str(), -1));
  _properties = _source->GetAudioProperties();
  _start = 0;
}

FFAudioReader::~FFAudioReader() {}

int FFAudioReader::getFormat() {
  return _properties.Format;
}

int FFAudioReader::getChannels() {
  return _properties.Channels;
}

int64_t FFAudioReader::getSamplesPerChannelOfDuration(int64_t durationMicroSec) {
  return durationMicroSec * _properties.SampleRate / 1000000;
}

int FFAudioReader::getBytesPerSample() {
  return _properties.BytesPerSample;
}

void FFAudioReader::seek(int64_t timeMicroSec) {
  _start = timeMicroSec * _properties.SampleRate / 1000000;
}

int FFAudioReader::readSamples(uint8_t** data, int sampleCount) {
//  printf("FFAudioReader::readSamples, sample count:%d, data:%p\n", sampleCount, (void*)data);
  int res = _source->GetAudio(data, _start, sampleCount);
  _start += res;
  return res;
}

void FFAudioReader::test(const std::string inputPath, const std::string outputPath) {
    std::string outputFilePath = outputPath + "/voice2.pcm";
    FILE* outputFile = NULL;
    outputFile = fopen(outputFilePath.c_str(), "wb");
    if (outputFile == NULL)
        return;
    
    FFAudioReader reader(inputPath.c_str());
    int channels = reader.getChannels();
    uint8_t** data = new uint8_t*[channels];
    if (data == NULL)
        return;
    int wantedSamples = (int)reader.getSamplesPerChannelOfDuration(20*1000);
    for (int i = 0; i < channels; i++) {
        data[i] = new uint8_t[reader.getBytesPerSample() * wantedSamples];
        if (data[i] == NULL)
            return;
    }
    int samples = wantedSamples;
    do {
        samples = reader.readSamples(data, wantedSamples);
        if (samples > 0) {
            fwrite(data[0], reader.getBytesPerSample(), samples, outputFile);
        }
    } while (samples > 0);
    fclose(outputFile);
}

}  // namespace pag

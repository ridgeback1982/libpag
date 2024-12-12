
#include "FFAudioReader.h"

namespace pag {

FFAudioReader::FFAudioReader(const std::string& path) {
  _source.reset(new BestAudioSource(path.c_str(), -1));
  _properties = _source->GetAudioProperties();
  _from = 0;
  _to = -1;
  // _volumeDetector.reset(new FFVolumeDetector(path));   //no need for volume detection
  _speed = 1.0f;
}

FFAudioReader::~FFAudioReader() {}

int FFAudioReader::getFormat() {
  return _properties.Format;
}

int FFAudioReader::getChannels() {
  return _properties.Channels;
}

int FFAudioReader::getSampleRate() {
  return _properties.SampleRate;
}

int64_t FFAudioReader::getSamplesPerChannelOfDuration(int64_t durationMicroSec) {
  return durationMicroSec * _properties.SampleRate / 1000000;
}

int FFAudioReader::getBytesPerSample() {
  return _properties.BytesPerSample;
}

void FFAudioReader::setCutFrom(int64_t timeMicroSec) {
  _from = timeMicroSec * _properties.SampleRate / 1000000;
}

void FFAudioReader::setCutTo(int64_t timeMicroSec) {
  _to = timeMicroSec * _properties.SampleRate / 1000000;
}

//returns sample count
int FFAudioReader::readSamples(uint8_t** data, int channels, int sampleCount) {
//  printf("FFAudioReader::readSamples, sample count:%d, data:%p\n", sampleCount, (void*)data);
  int processedCount = 0;
  int res = 0;

  if (_speed != 1.0f) {
    if (_atempo == nullptr) {
      _atempo = std::make_unique<FFAudioAtempo>(_properties.SampleRate, _properties.Channels, _properties.Format);
      _atempo->setSpeed(_speed);
    }
    _atempo->setOuputSamples(sampleCount);
    AVFrame* input = nullptr;
    if (_atempo->availableSamples() >= sampleCount) {
      AVFrame* output = nullptr;
      res = _atempo->process(nullptr, &output);
      if (res == ErrorCode::SUCCESS) {
        if (output->ch_layout.nb_channels == 1) {
          for (int i = 0; i < channels; i++) {
            memcpy(data[i], output->data[0], _properties.BytesPerSample * sampleCount);
          }
        } else {
          if (channels == 1) {
            memcpy(data[0], output->data[0], _properties.BytesPerSample * sampleCount);
          } else {
            for(int i = 0; i < output->ch_layout.nb_channels; i++) {
              memcpy(data[i], output->data[i], _properties.BytesPerSample * sampleCount);
            }
          }
        }
        // printf("FFAudioAtempo::process OK(enough available), output samples:%d\n", output->nb_samples);
        processedCount += output->nb_samples;
      } else {
        printf("FFAudioAtempo::process failed, res:%d\n", res);
      } 
    } else {
      do {
        input = av_frame_alloc();
        input->nb_samples = sampleCount;
        input->format = _properties.Format;
        av_channel_layout_default(&input->ch_layout, _properties.Channels);
        input->sample_rate = _properties.SampleRate;
        if (av_frame_get_buffer(input, 0) < 0) {
          printf("Failed to allocate the input frame\n");
          av_frame_free(&input);
          return 0;
        }

        int count = (int)_source->GetAudio(input->data, _from, sampleCount);
        if (count == 0) {
          printf("FFAudioReader::readSamples, audio source drains\n");
          av_frame_free(&input);
        }
        _from += count;
        AVFrame* output = nullptr;
        res = _atempo->process(input, &output);
        if (res == ErrorCode::SUCCESS) {
          if (output->ch_layout.nb_channels == 1) {
            for (int i = 0; i < channels; i++) {
              memcpy(data[i], output->data[0], _properties.BytesPerSample * sampleCount);
            }
          } else {
            if (channels == 1) {
              memcpy(data[0], output->data[0], _properties.BytesPerSample * sampleCount);
            } else {
              for(int i = 0; i < output->ch_layout.nb_channels; i++) {
                memcpy(data[i], output->data[i], _properties.BytesPerSample * sampleCount);
              }
            }
          }
          // printf("FFAudioAtempo::process OK, output samples:%d\n", output->nb_samples);
          processedCount += output->nb_samples;
        } else {
          if (res == ErrorCode::AGAIN) {
            // printf("FFAudioAtempo::process AGAIN, need more input samples\n");
          } else {
            printf("FFAudioAtempo::process failed, res:%d\n", res);
          }
        }        
        av_frame_free(&input);
        av_frame_free(&output);
      } while (res == ErrorCode::AGAIN);
    }
  } else {
    int count = (int)_source->GetAudio(data, _from, sampleCount);
    _from += count;
    processedCount += count;
  }

  return processedCount;
}

int FFAudioReader::getMaxVolume() {
  if (_volumeDetector == nullptr) {
    return 0;
  }
  return _volumeDetector->getMaxVolumeForDuration(1000000*5, 1000000*10);
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
        samples = reader.readSamples(data, channels, wantedSamples);
        if (samples > 0) {
            fwrite(data[0], reader.getBytesPerSample(), samples, outputFile);
        }
    } while (samples > 0);
    fclose(outputFile);
}

}  // namespace pag

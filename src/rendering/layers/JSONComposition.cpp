/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "base/keyframes/SingleEaseKeyframe.h"
#include "pag/file.h"
#include "pag/pag.h"
#include "rendering/utils/LockGuard.h"
#include "rendering/utils/ScopedLock.h"

#include <sstream>
#include "codec/tags/VideoSequence.h"
#include "rendering/utils/media/FFAudioReader.h"
#include "rendering/utils/media/FFAudioMixer.h"

//ffmpeg
extern "C" {
  #include "libavcodec/avcodec.h"
  #include "libavformat/avformat.h"
  #include "libavutil/avutil.h"
  #include "libavutil/channel_layout.h"
  #include "libavutil/audio_fifo.h"
  #include "libswscale/swscale.h"
  #include "libavutil/imgutils.h"
}
#include "MovieObject.h"
#include <cmath>


//NOTE:
//1. 关注下各个类的析构函数

//zzy, work alone
namespace pag {

#define TEST_DURATION 300
#define TEST_FPS 30
#define TEST_COMPOSITION_WIDTH 1080
#define TEST_COMPOSITION_HEIGHT 1920
#define TEST_IMAGE_WIDTH 512
#define TEST_IMAGE_HEIGHT 512

int TimeToFrame(int time, float fps) {
    return (int)std::floor(time / 1000.0f * fps);
}

int LifetimeToFrameDuration(const movie::LifeTime& lifetime, float fps) {
    return TimeToFrame(lifetime.end_time, fps) - TimeToFrame(lifetime.begin_time, fps);
}

PreComposeLayer* createVideoLayer(movie::VideoTrack* track, const movie::MovieSpec& spec) {
    int visual_width = spec.width * track->content.location.w;   //visual width, not video og width
    int visual_height = spec.height * track->content.location.h;
    int video_width = track->content.width();
    int video_height = track->content.height();
    int video_fps = track->content.fps();
    auto vidComposition = new VideoComposition();
    vidComposition->id = UniqueID::Next();
    vidComposition->width = video_width;
    vidComposition->height = video_height;
    vidComposition->frameRate = video_fps * track->content.speed;
    vidComposition->duration = LifetimeToFrameDuration(track->lifetime, vidComposition->frameRate);
    vidComposition->backgroundColor = {0, 0, 0};     //hard code

    int ogCutFrom = track->content.cutFrom * track->content.speed;
    int ogDuration = (track->lifetime.end_time - track->lifetime.begin_time) * track->content.speed;
    auto videoSequence = ReadVideoSequenceFromFile(track->content.path, 
      TimeToFrame(ogCutFrom, video_fps),
      TimeToFrame(ogCutFrom + ogDuration, video_fps),
      (int)vidComposition->duration);
    videoSequence->frameRate = vidComposition->frameRate;   //modify fps of video sequence
    videoSequence->composition = vidComposition;
    vidComposition->sequences.push_back(videoSequence);

    auto vidPreComposeLayer = new PreComposeLayer();
    vidPreComposeLayer->id = UniqueID::Next();
    vidPreComposeLayer->startTime = TimeToFrame(track->lifetime.begin_time, vidComposition->frameRate);
    //zzy, must set compositionStartTime same as startTime, or it will be zero, and cause video start play from some middle point
    vidPreComposeLayer->compositionStartTime = vidPreComposeLayer->startTime;
    vidPreComposeLayer->duration = LifetimeToFrameDuration(track->lifetime, vidComposition->frameRate);
    vidPreComposeLayer->transform = Transform2D::MakeDefault().release();
    //set transform
    vidPreComposeLayer->transform->anchorPoint->value.set(video_width/2, video_height/2);
    vidPreComposeLayer->transform->position->value.set(spec.width*track->content.location.center_x, spec.height*track->content.location.center_y);
    float scale_x = (float)visual_width/video_width;
    float scale_y = (float)visual_height/video_height;
    vidPreComposeLayer->transform->scale->value.set(scale_x, scale_y);
    vidPreComposeLayer->timeRemap = new Property<float>(0);      //hard code
    vidPreComposeLayer->composition = vidComposition;

    return vidPreComposeLayer;
}

#define CREATE_AUDIO_SOURCE(typedTrack, spec) \
    auto audioSource = std::make_shared<PAGAudioSource>(typedTrack->content.path.c_str()); \
    audioSource->setStartFrame(TimeToFrame(typedTrack->lifetime.begin_time, spec.fps)); \
    audioSource->setDuration(LifetimeToFrameDuration(typedTrack->lifetime, spec.fps)); \
    audioSource->setSpeed(typedTrack->content.speed); \
    audioSource->setCutFrom(1000*(int64_t)(typedTrack->content.cutFrom * typedTrack->content.speed)); \
    audioSource->setVolumeForMix(typedTrack->content.mixVolume); \
    return audioSource; 

std::shared_ptr<PAGAudioSource> createAudioSource(const std::string& type, movie::Track* track, const movie::MovieSpec& spec) {
  if (type == "music") {
    auto typedTrack = static_cast<movie::MusicTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
  } else if (type == "voice") {
    auto typedTrack = static_cast<movie::VoiceTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
  } else if (type == "video") {
    auto typedTrack = static_cast<movie::VideoTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
  }
  return nullptr;
}

std::shared_ptr<JSONComposition> JSONComposition::Load(const std::string& json_str) {
    json nmjson = json::parse(json_str);
    movie::Movie movie = nmjson.get<movie::Movie>();
    movie::Story story = movie.video.stories[0];

    auto vecComposition = new VectorComposition();
    vecComposition->id = UniqueID::Next();
    vecComposition->width = movie.video.width;
    vecComposition->height = movie.video.height;
    vecComposition->duration = TimeToFrame(story.duration, movie.video.fps);   //set by json
    vecComposition->frameRate = movie.video.fps;           //set by json
    vecComposition->backgroundColor = {0, 0, 0};     //hard code

    //create JSONComposition
    auto preComposeLayer = PreComposeLayer::Wrap(vecComposition).release();
    auto jsonComposition = std::shared_ptr<JSONComposition>(new JSONComposition(preComposeLayer));
    jsonComposition->rootLocker = std::make_shared<std::mutex>();
    
    //sort tracks by zorder
    std::sort(story.tracks.begin(), story.tracks.end(), [](const auto& a, const auto& b) {
        return a->zorder < b->zorder;
    });
    
    //add track to PAGLayer one by one
    for (auto& t : story.tracks) {
        if (t->type == "video") {
            //create video PAGComposition and add to JSONComposition
            auto track = static_cast<movie::VideoTrack*>(t);
            track->content.init();
            printf("video track, path:%s\n", track->content.path.c_str());
            auto vidPreComposeLayer = createVideoLayer(track, movie.video);
            vecComposition->layers.push_back(vidPreComposeLayer);
            auto pagVideoLayer = std::make_shared<PAGComposition>(nullptr, vidPreComposeLayer);
            jsonComposition->addLayer(pagVideoLayer);

            //add audio source
            if (track->content.mixVolume > MIN_VOLUME) {
              auto audioSource = createAudioSource(t->type, track , movie.video);
              jsonComposition->addAudioSource(audioSource);
            }
        } else if (t->type == "gif") {
            auto track = static_cast<movie::GifTrack*>(t);
            printf("gif track, path:%s\n", track->content.path.c_str());
        } else if (t->type == "voice") {
            auto track = static_cast<movie::VoiceTrack*>(t);
            printf("voice track, path:%s\n", track->content.path.c_str());
            //add audio source
            if (track->content.mixVolume > MIN_VOLUME) {
              auto audioSource = createAudioSource(t->type, track , movie.video);
              jsonComposition->addAudioSource(audioSource);
            }
        } else if (t->type == "music") {
            auto track = static_cast<movie::MusicTrack*>(t);
            printf("music track, path:%s\n", track->content.path.c_str());
            //add audio source
            if (track->content.mixVolume > MIN_VOLUME) {
              auto audioSource = createAudioSource(t->type, track , movie.video);
              jsonComposition->addAudioSource(audioSource);
            }
        } else if (t->type == "image") {
            auto track = static_cast<movie::ImageTrack*>(t);
            printf("image track, path:%s\n", track->content.path.c_str());
        } else if (t->type == "Title") {
            auto track = static_cast<movie::TitleTrack*>(t);
            printf("title track, text:%s\n", track->content.text.c_str());
        } else if (t->type == "Subtitle") {
            auto track = static_cast<movie::SubtitleTrack*>(t);
            printf("subtitle track, count:%zu\n", track->content.sentences.size());
        }
    }

    //update static time range, I am not sure if it is necessary
    if (!vecComposition->staticTimeRangeUpdated) {
      vecComposition->updateStaticTimeRanges();
      vecComposition->staticTimeRangeUpdated = true;
    }

    return jsonComposition;
}

std::vector<std::string> splitStringBy(const std::string &s, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::stringstream ss(s);

  // Split the string by the given delimiter
  while (std::getline(ss, token, delimiter)) {
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

std::shared_ptr<JSONComposition> JSONComposition::LoadTest(const std::string& json) {
  //pass the process of parsing json file
  printf("JSONComposition::LoadTest, json:%s", json.c_str());

  
  const std::vector<std::string> tokens = splitStringBy(json, ';');
//  FFAudioReader::test(tokens[0], tokens[3]);

 
  //example 1: one image
  std::string imagePath = tokens[1].c_str();   //set by json
  auto imageWidth = TEST_IMAGE_WIDTH;                 //set by json
  auto imageHeight = TEST_IMAGE_HEIGHT;                //set by json
  auto imageLayer = new ImageLayer();
  imageLayer->id = UniqueID::Next();
  imageLayer->name = "image1";           //set by json
  imageLayer->startTime = 0;              //set by json
  imageLayer->duration = TEST_DURATION;   //set by json
  imageLayer->transform = Transform2D::MakeDefault().release();
  imageLayer->transform->anchorPoint->value.set(imageWidth/2, imageHeight/2);   //set by json
  imageLayer->transform->position->value.set(TEST_COMPOSITION_WIDTH/2, TEST_COMPOSITION_HEIGHT/2 + 100);    //set by json
    {
      //hard code animation: scale
      auto keyFrame1 = new SingleEaseKeyframe<pag::Point>();
      keyFrame1->startTime = 0;            //set by json 
      keyFrame1->endTime = TEST_DURATION;   //set by json
      keyFrame1->startValue = pag::Point::Make(1.0f, 1.0f);   //set by json
      keyFrame1->endValue = pag::Point::Make(0.3f, 0.3f);   //set by json
      keyFrame1->interpolationType = KeyframeInterpolationType::Linear;  //hard code
      std::vector<Keyframe<pag::Point>*> keyframes = {};
      keyframes.push_back(keyFrame1);
      //release former scale property
      if (imageLayer->transform->scale) {
        delete imageLayer->transform->scale;
      }
      imageLayer->transform->scale = new AnimatableProperty<pag::Point>(keyframes);
    }
    {
      //hard code animation: transparency
      auto keyFrame2 = new SingleEaseKeyframe<pag::Opacity>();
      keyFrame2->startTime = 0;            //set by json 
      keyFrame2->endTime = TEST_DURATION;   //set by json
      keyFrame2->startValue = 128;   //set by json
      keyFrame2->endValue = 255;   //set by json
      keyFrame2->interpolationType = KeyframeInterpolationType::Linear;  //hard code
      std::vector<Keyframe<pag::Opacity>*> keyframes = {};
      keyframes.push_back(keyFrame2);
      //release former scale property
      if (imageLayer->transform->opacity) {
        delete imageLayer->transform->opacity;
      }
      imageLayer->transform->opacity = new AnimatableProperty<pag::Opacity>(keyframes);
    }
  imageLayer->timeRemap = new Property<float>(0);      //hard code
  imageLayer->imageBytes = new ImageBytes();
  imageLayer->imageBytes->id = UniqueID::Next();
  imageLayer->imageBytes->width = imageWidth;
  imageLayer->imageBytes->height = imageHeight;
  imageLayer->imageBytes->fileBytes = ByteData::FromPath(imagePath).release();
  assert(imageLayer->imageBytes->fileBytes != nullptr);

  //example 2: one text
  auto textLayer = new TextLayer();
  textLayer->id = UniqueID::Next();
  textLayer->startTime = 0;              //set by json
  textLayer->duration = TEST_DURATION;   //set by json
  textLayer->transform = Transform2D::MakeDefault().release();
  textLayer->transform->position->value.set(TEST_COMPOSITION_WIDTH/2, TEST_COMPOSITION_HEIGHT/2);    //set by json
  textLayer->timeRemap = new Property<float>(0);      //hard code
  auto textData = new TextDocument();
  textData->text = "d你好呀，世界";     //set by json
//  textData->fontFamily = "Heiti SC";     //set by json
//  textData->fontStyle = "Regular";     //set by json
  textData->fontSize = 100;     //set by json
  textData->fillColor = pag::Red;   //set by json
  textData->applyStroke = true;   //hard code
  textData->strokeColor = pag::Green;   //set by json
  textData->strokeWidth = 1;     //set by json
  textData->justification = pag::ParagraphJustification::CenterJustify;   //hard code
  textLayer->sourceText = new Property<TextDocumentHandle>(pag::TextDocumentHandle(textData));
    {
      // hard code animation
      TextAnimator* animator = new TextAnimator();
      //create TextRangeSelector
      auto selector1 = new TextRangeSelector();
      auto keyFrame1 = new SingleEaseKeyframe<pag::Percent>();
      keyFrame1->startTime = 0;            //set by json 
      keyFrame1->endTime = TEST_DURATION;   //set by json
      keyFrame1->startValue = 0.0f;       //set by json
      keyFrame1->endValue = 1.0f;       //set by json
      keyFrame1->interpolationType = KeyframeInterpolationType::Linear;  //hard code
      std::vector<Keyframe<pag::Percent>*> keyframes = {};
      keyframes.push_back(keyFrame1);
      selector1->start = new AnimatableProperty<pag::Percent>(keyframes);
      selector1->end = new Property<pag::Percent>(1.0f);   //hard code
      selector1->offset = new Property<float>(0.0f);   //hard code
      selector1->units = pag::TextRangeSelectorUnits::Percentage;   //hard code
      selector1->basedOn = pag::TextSelectorBasedOn::Characters;   //hard code
      selector1->shape = pag::TextRangeSelectorShape::Square;   //hard code
      selector1->mode = new Property<pag::Enum>(1);   //hard code, 1 is pag::TextSelectorMode::Add
      selector1->amount = new Property<pag::Percent>(1.0f);   //hard code
      selector1->smoothness = new Property<pag::Percent>(1.0f);   //hard code
      selector1->easeHigh = new Property<pag::Percent>(0.0f);   //hard code
      selector1->easeLow = new Property<pag::Percent>(0.0f);   //hard code
      selector1->randomizeOrder = false;   //hard code
      selector1->randomSeed = new Property<uint16_t>(0);   //hard code
      animator->selectors.push_back(selector1);
      //create TextAnimatorTypographyProperties
      auto typographyProperties = new TextAnimatorTypographyProperties();
      typographyProperties->scale = new Property<pag::Point>(pag::Point::Make(2.0f, 2.0f));   //set by json
      typographyProperties->opacity = new Property<pag::Opacity>(0);    //set by json, 0 is total transparent
      animator->typographyProperties = typographyProperties;
      //add animator to textLayer
      textLayer->animators.push_back(animator);
    }

  //example 3: one video
  auto vidComposition = new VideoComposition();
  vidComposition->id = UniqueID::Next();
  vidComposition->width = TEST_COMPOSITION_WIDTH;                    //set by json
  vidComposition->height = TEST_COMPOSITION_HEIGHT;                   //set by json
  vidComposition->duration = TEST_DURATION;       //set by json
  vidComposition->frameRate = TEST_FPS;           //set by json
  vidComposition->backgroundColor = {0, 0, 0};     //hard code
  auto videoSequence = ReadVideoSequenceFromFile(tokens[2], 0, TEST_DURATION, TEST_DURATION);
  // TimeRange range = {0, TEST_DURATION - 1};      //set by json, the range is probably not same as video composition
  // videoSequence->staticTimeRanges.push_back(range);
  videoSequence->composition = vidComposition;
  vidComposition->sequences.push_back(videoSequence);
  // if (!vidComposition->staticTimeRangeUpdated) {
  //   vidComposition->updateStaticTimeRanges();
  //   vidComposition->staticTimeRangeUpdated = true;
  // }
  auto vidPreComposeLayer = new PreComposeLayer();
  vidPreComposeLayer->id = UniqueID::Next();
  vidPreComposeLayer->startTime = 30;              //set by json
  vidPreComposeLayer->compositionStartTime = vidPreComposeLayer->startTime;
  vidPreComposeLayer->duration = TEST_DURATION;   //set by json
  vidPreComposeLayer->transform = Transform2D::MakeDefault().release();
//  auto videoWidth = vidComposition->sequences[0]->getVideoWidth();
//  auto videoHeight = vidComposition->sequences[0]->getVideoHeight();
  vidPreComposeLayer->transform->anchorPoint->value.set(TEST_COMPOSITION_WIDTH/2, TEST_COMPOSITION_HEIGHT/2);   //set by json
  vidPreComposeLayer->transform->position->value.set(TEST_COMPOSITION_WIDTH/2, TEST_COMPOSITION_HEIGHT/2);    //set by json
  vidPreComposeLayer->timeRemap = new Property<float>(0);      //hard code
  vidPreComposeLayer->composition = vidComposition;
    
  
  //hard code the composition part
  auto vecComposition = new VectorComposition();
  vecComposition->id = UniqueID::Next();
  vecComposition->width = TEST_COMPOSITION_WIDTH;                    //set by json
  vecComposition->height = TEST_COMPOSITION_HEIGHT;                   //set by json
  vecComposition->duration = TEST_DURATION;       //set by json
  vecComposition->frameRate = TEST_FPS;           //set by json
  vecComposition->backgroundColor = {0, 0, 0};     //hard code

  //add layer to vectorComposition
  vecComposition->layers.push_back(vidPreComposeLayer);
  vecComposition->layers.push_back(imageLayer);
  vecComposition->layers.push_back(textLayer);

  //update static time range, I am not sure if it is necessary
  if (!vecComposition->staticTimeRangeUpdated) {
    vecComposition->updateStaticTimeRanges();
    vecComposition->staticTimeRangeUpdated = true;
  }
  

  //create corresponding pag layer
  auto pagImageLayer = std::make_shared<PAGImageLayer>(nullptr, imageLayer);
  pagImageLayer->setFrameRate(TEST_FPS);  //zzy, must set frame rate in case of null PAGFile
  auto pagTextLayer = std::make_shared<PAGTextLayer>(nullptr, textLayer);
  pagTextLayer->setFrameRate(TEST_FPS);  //zzy, must set frame rate in case of null PAGFile
  auto pagVideoLayer = std::make_shared<PAGComposition>(nullptr, vidPreComposeLayer);


  //create JSONComposition
  auto preComposeLayer = PreComposeLayer::Wrap(vecComposition).release();
  auto jsonComposition = std::shared_ptr<JSONComposition>(new JSONComposition(preComposeLayer));
  jsonComposition->rootLocker = std::make_shared<std::mutex>();

  //add layer to pag composition
  jsonComposition->addLayer(pagVideoLayer);
  jsonComposition->addLayer(pagImageLayer);
  jsonComposition->addLayer(pagTextLayer);

  //add audio source
  auto audioSource = std::make_shared<PAGAudioSource>(tokens[2]);
  audioSource->setStartFrame(0);
  audioSource->setDuration(TEST_DURATION);
  jsonComposition->addAudioSource(audioSource);

  auto musicSource = std::make_shared<PAGAudioSource>(tokens[0]);
  musicSource->setStartFrame(0);
  musicSource->setDuration(TEST_DURATION);
  jsonComposition->addAudioSource(musicSource);

  return jsonComposition;
}


JSONComposition::JSONComposition(PreComposeLayer* layer)
  : PAGComposition(nullptr, layer) {

}


}  // namespace pag


namespace movie {


int VideoContent::init() {
  AVFormatContext *fmt_ctx = NULL;
  int video_stream_index = -1;

  // 初始化 FFmpeg 库
  avformat_network_init();

  // 打开输入文件
  if (avformat_open_input(&fmt_ctx, path.c_str(), NULL, NULL) < 0) {
    printf("Could not open input file: %s\n", path.c_str());
    return -1;
  }

  // 查找流信息
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    printf("Could not find stream information.\n");
    avformat_close_input(&fmt_ctx);
    return -1;
  }

  // 查找视频流
  for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
      break;
    }
  }

  if (video_stream_index == -1) {
    printf("Could not find a video stream.\n");
    avformat_close_input(&fmt_ctx);
    return -1;
  }

    // Get codec parameters for the video stream
    AVStream* video_stream = fmt_ctx->streams[video_stream_index];
    AVCodecParameters* codec_params = video_stream->codecpar;
    _width = codec_params->width;
    _height = codec_params->height;

    // Retrieve FPS
    AVRational frame_rate = video_stream->avg_frame_rate;
    _fps = (frame_rate.den && frame_rate.num) ? 
                 static_cast<double>(frame_rate.num) / frame_rate.den : 0;

    return 0;
}



}  // namespace movie

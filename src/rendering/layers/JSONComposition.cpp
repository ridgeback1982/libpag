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
#include <fstream>
#include <sstream>
#include <curl/curl.h>
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
#include "file_util.h"
#include <set>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <regex>
#include <filesystem>



#if defined(__linux__)
#include <sys/stat.h>
#include <sys/types.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/stat.h>
#include <sys/types.h>
#endif


//NOTE:
//1. 关注下各个类的析构函数

//zzy, work alone

namespace movie {

bool starts_with(const std::string& str, const std::string& prefix) {
    return str.compare(0, prefix.size(), prefix) == 0;
}

std::string getFileNameFromUrl(const std::string& url) {
    size_t pos = url.find_last_of('/');
    if (pos != std::string::npos && pos + 1 < url.size()) {
        return url.substr(pos + 1);
    }
    return ""; // 没有后缀名时返回空字符串
}

size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* out = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    out->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int curlDownload(const std::string& url, const std::string& localPath) {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::ofstream file(localPath);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000L); // Timeout after 120 seconds
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // Timeout after 10 seconds for connection
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        file.close();
        if (res != CURLE_OK) {
            printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return -1;
        }
        // printf("Download %s to %s success.\n", url.c_str(), localPath.c_str());
    }
    return 0;
}

int VideoContent::init(const std::string& tmpDir) {
  AVFormatContext *fmt_ctx = NULL;
  int video_stream_index = -1;

  bool remote = starts_with(path, "http://") || starts_with(path, "https://");
  // printf("VideoContent::init, remote:%d\n", remote);
  if (remote) {
      //create local path
      _localPath = tmpDir + "/" + getFileNameFromUrl(path);
      
      //download to local path
      printf("VideoContent::init, will download %s to %s\n", path.c_str(), _localPath.c_str());
      auto tick1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      if (curlDownload(path, _localPath) < 0) {
          avformat_free_context(fmt_ctx);
          return -1;
      }
      auto tick2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      printf("VideoContent::init, download done, cost: %ds\n", (int)(tick2-tick1).count()/1000);
  } else {
    _localPath = path;
  }

  // 初始化 FFmpeg 库
  avformat_network_init();

  // 打开输入文件
  if (avformat_open_input(&fmt_ctx, _localPath.c_str(), NULL, NULL) < 0) {
    std::cerr << "Could not open input file:" << _localPath << std::endl;
    avformat_free_context(fmt_ctx);
    return -1;
  }

  // 查找流信息
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    std::cerr << "Could not find stream information" << std::endl;
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
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
    std::cerr << "Could not find a video stream" << std::endl;
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
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

    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 0;
}

int AudioContent::init(const std::string& tmpDir) {
  bool remote = starts_with(path, "http://") || starts_with(path, "https://");
  // printf("AudioContent::init, remote:%d\n", remote);
  if (remote) {
      //create local path
      _localPath = tmpDir + "/" + getFileNameFromUrl(path);
      
      //download to local path
      printf("AudioContent::init, will download %s to %s\n", path.c_str(), _localPath.c_str());
      auto tick1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      if (curlDownload(path, _localPath) < 0) {
          return -1;
      }
      auto tick2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      printf("AudioContent::init, download done, cost: %ds\n", (int)(tick2-tick1).count()/1000);
  } else {
    _localPath = path;
  }
  return 0;
}

int ImageContent::init(const std::string& tmpDir) {
  AVFormatContext *fmt_ctx = NULL;
  int video_stream_index = -1;

  bool remote = starts_with(path, "http://") || starts_with(path, "https://");
  // printf("VideoContent::init, remote:%d\n", remote);
  if (remote) {
      //create local path
      _localPath = tmpDir + "/" + getFileNameFromUrl(path);
      
      //download to local path
      printf("ImageContent::init, will download %s to %s\n", path.c_str(), _localPath.c_str());
      auto tick1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      if (curlDownload(path, _localPath) < 0) {
          avformat_free_context(fmt_ctx);
          return -1;
      }
      auto tick2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      printf("ImageContent::init, download done, cost: %d s\n", (int)(tick2-tick1).count()/1000);
  } else {
    _localPath = path;
  }

  // 初始化 FFmpeg 库
  avformat_network_init();

  // 打开输入文件
  if (avformat_open_input(&fmt_ctx, _localPath.c_str(), NULL, NULL) < 0) {
    std::cerr << "Could not open input file:" << _localPath << std::endl;
    return -1;
  }

  // 查找流信息
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    std::cerr << "Could not find stream information" << std::endl;
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
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
    std::cerr << "Could not find a video stream" << std::endl;
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return -1;
  }

  // Get codec parameters for the video stream
  AVStream* video_stream = fmt_ctx->streams[video_stream_index];
  AVCodecParameters* codec_params = video_stream->codecpar;
  _width = codec_params->width;
  _height = codec_params->height;

  avformat_close_input(&fmt_ctx);
  avformat_free_context(fmt_ctx);
  return 0;
}

}  // namespace movie


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


typedef std::set<std::pair<float, float>> FloatPairSet;
FloatPairSet strokeWidthSet = {
  //{fontsize, strokeWidth}
    {0.03,      0},
    {0.05,      4},
    {0.07,      5},
    {0.1,       6}
};

float findValueFromFloatPairSet(const FloatPairSet &fps, float key) {
    for (auto it = fps.begin(); it!= fps.end(); ++it) {
        if (key < it->first) {
            if (it == fps.begin()) {
                return it->second;
            } else {
                auto prev = it;
                --prev;
                float slope = (it->second - prev->second) / (it->first - prev->first);
                return prev->second + slope * (key - prev->first);
            }
        } else {
            continue;
        }
    }
    //return last value
    return fps.rbegin()->second;
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
    auto videoSequence = ReadVideoSequenceFromFile(track->content.localPath(), 
      TimeToFrame(ogCutFrom, video_fps),
      TimeToFrame(ogCutFrom + ogDuration, video_fps),
      (int)vidComposition->duration);
    if (videoSequence == nullptr) {
        std::cerr << "Error reading video file" << std::endl;
        return nullptr;
    }
    videoSequence->frameRate = vidComposition->frameRate;   //modify fps of video sequence
    videoSequence->composition = vidComposition;
    vidComposition->sequences.push_back(videoSequence);

    auto vidPreComposeLayer = new PreComposeLayer();
    vidPreComposeLayer->id = UniqueID::Next();
    vidPreComposeLayer->startTime = TimeToFrame(track->lifetime.begin_time, spec.fps);
    //zzy, must set compositionStartTime same as startTime, or it will be zero, and cause video start play from some middle point
    vidPreComposeLayer->compositionStartTime = vidPreComposeLayer->startTime;
    vidPreComposeLayer->duration = LifetimeToFrameDuration(track->lifetime, spec.fps);
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

//rgb(216, 27, 67) or rgba(255,255,255,1) or #FFF
Color translateColor(const std::string& color_string) {
  Color c = pag::White;
//  float alpha = 1.0f;
  if (movie::starts_with(color_string, "rgba")) {
    std::regex regex_pattern(R"(rgba\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d+(\.\d+)?)\s*\))");
    std::smatch match;
    if (std::regex_match(color_string, match, regex_pattern)) {
        // 提取 RGB 分量
        c.red = std::stoi(match[1].str());
        c.green = std::stoi(match[2].str());
        c.blue = std::stoi(match[3].str());
//        alpha = std::stof(match[4].str());
    } else {
        std::cout << "输入字符串不匹配 RGBA 格式" << std::endl;
    }
  } else if (movie::starts_with(color_string, "rgb")) {
    std::regex regex_pattern(R"(rgb\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\))");
    std::smatch match;
    if (std::regex_match(color_string, match, regex_pattern)) {
        // 提取 RGB 分量
        c.red = std::stoi(match[1].str());
        c.green = std::stoi(match[2].str());
        c.blue = std::stoi(match[3].str());
    } else {
        std::cout << "输入字符串不匹配 RGBA 格式" << std::endl;
    }
  } else if (movie::starts_with(color_string, "#")) {
    // 定义正则表达式
    std::regex regex_pattern("^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$");
    std::smatch match;
    // 检查是否匹配
    if (std::regex_match(color_string, match, regex_pattern)) {
        std::string hex = match[1]; // 提取颜色部分
        if (hex.size() == 3) {
            // 如果是简写形式，例如 #FFF
            c.red = std::stoi(std::string(2, hex[0]), nullptr, 16);
            c.green = std::stoi(std::string(2, hex[1]), nullptr, 16);
            c.blue = std::stoi(std::string(2, hex[2]), nullptr, 16);
        } else if (hex.size() == 6) {
            // 如果是标准形式，例如 #FFFFFF
            c.red = std::stoi(hex.substr(0, 2), nullptr, 16);
            c.green = std::stoi(hex.substr(2, 2), nullptr, 16);
            c.blue = std::stoi(hex.substr(4, 2), nullptr, 16);
        } else {
            std::cerr << "Invalid color code format." << std::endl;
        }
    } else {
        std::cerr << "Invalid color code: " << color_string << std::endl;
    }
  }
  return c;
}

std::string getFileNameWithoutExtension(const std::string& filePath) {
    // 使用 std::filesystem 提取文件名
    std::filesystem::path path(filePath);
    return path.stem().string(); // stem() 返回不带扩展名的文件名
}

TextLayer* createTextLayer(const std::string& text, movie::TitileContent* content, const movie::LifeTime& lifetime, const movie::MovieSpec& spec) {
  int visual_width = std::min(spec.width, spec.height) * content->fontSize;
  int visual_height = visual_width;
  
  auto textLayer = new TextLayer();
  textLayer->id = UniqueID::Next();
  auto textData = new TextDocument();
  textData->fontSize = 100;     //hardcode, in pixel
  textData->text = text;
  if (!content->fontFamilyName.empty()) {
    textData->fontFamily = findEnglishFontName(getFileNameWithoutExtension(content->fontFamilyName));     //set by json
  }
  if (!content->textColor.empty()) {
    textData->fillColor = translateColor(content->textColor);
  }
  if (!content->stroke.empty()) {
    textData->applyStroke = true;
    textData->strokeColor = translateColor(content->stroke);
    textData->strokeWidth = findValueFromFloatPairSet(strokeWidthSet, content->fontSize);
  }
  textData->justification = pag::ParagraphJustification::CenterJustify;   //hard code
  textLayer->sourceText = new Property<TextDocumentHandle>(pag::TextDocumentHandle(textData));
  textLayer->startTime = TimeToFrame(lifetime.begin_time, spec.fps);
  textLayer->duration = LifetimeToFrameDuration(lifetime, spec.fps);
  textLayer->transform = Transform2D::MakeDefault().release();

  textLayer->transform->anchorPoint->value.set(0, -30); //hard code
  textLayer->transform->position->value.set(spec.width*content->location.center_x, spec.height*content->location.center_y);
  float scale_x = (float)visual_width/textData->fontSize;
  float scale_y = (float)visual_height/textData->fontSize;
  textLayer->transform->scale->value.set(scale_x, scale_y);
  textLayer->timeRemap = new Property<float>(0);      //hard code

//  std::cout << "createTextLayer, fontFamily:" << textData->fontFamily 
//      << ", color:" << (int)textData->fillColor.red << "|" << (int)textData->fillColor.green << "|" << (int)textData->fillColor.blue
//      << ", stroke:" << (int)textData->strokeColor.red << "|" << (int)textData->strokeColor.green << "|" << (int)textData->strokeColor.blue << ", width:" << textData->strokeWidth
//      << ", start:" << textLayer->startTime << ", duration:" << textLayer->duration << std::endl;
  return textLayer;
}

std::vector<TextLayer*> createTextLayers(movie::Track* track, const movie::MovieSpec& spec) {
    std::vector<TextLayer*> textLayers;
    movie::TitileContent* content = nullptr;
    if (track->type == "title") {
      content = &static_cast<movie::TitleTrack*>(track)->content;
      auto textLayer = createTextLayer(content->text, content, track->lifetime, spec);
      textLayers.push_back(textLayer);
    } else if (track->type == "subtitle") {
      content = &static_cast<movie::SubtitleTrack*>(track)->content;
      for (auto sentence : content->sentences) {
        movie::LifeTime lifetime;
        lifetime.begin_time = track->lifetime.begin_time + sentence.begin_time;
        if (lifetime.begin_time >= track->lifetime.end_time) {
          break;
        }
        lifetime.end_time = std::min(track->lifetime.begin_time + sentence.end_time, track->lifetime.end_time);
        auto textLayer = createTextLayer(sentence.text, content, lifetime, spec);
        textLayers.push_back(textLayer);
      }
    }
    return textLayers;
}

ImageLayer* CreateImageLayer(movie::Track* track, const movie::MovieSpec& spec) {
  movie::ImageContent* content = &static_cast<movie::ImageTrack*>(track)->content;
  int visual_width = spec.width * content->location.w;   //visual width, not image og width
  int visual_height = spec.height * content->location.h;

  auto imageWidth = content->width();
  auto imageHeight = content->height();
  auto imageLayer = new ImageLayer();
  imageLayer->id = UniqueID::Next();
  imageLayer->name = "image_xxx";           //hardcode
  imageLayer->startTime = TimeToFrame(track->lifetime.begin_time, spec.fps);
  imageLayer->duration = LifetimeToFrameDuration(track->lifetime, spec.fps);
  imageLayer->transform = Transform2D::MakeDefault().release();
  imageLayer->transform->anchorPoint->value.set(imageWidth/2, imageHeight/2);
  imageLayer->transform->position->value.set(spec.width*content->location.center_x, spec.height*content->location.center_y);
  float scale_x = (float)visual_width/imageWidth;
  float scale_y = (float)visual_height/imageHeight;
  imageLayer->transform->scale->value.set(scale_x, scale_y);

  imageLayer->timeRemap = new Property<float>(0);      //hard code
  imageLayer->imageBytes = new ImageBytes();
  imageLayer->imageBytes->id = UniqueID::Next();
  imageLayer->imageBytes->width = imageWidth;
  imageLayer->imageBytes->height = imageHeight;
  imageLayer->imageBytes->fileBytes = ByteData::FromPath(content->localPath()).release();

  return imageLayer;
}

#define CREATE_AUDIO_SOURCE(typedTrack, spec) \
    auto audioSource = std::make_shared<PAGAudioSource>(typedTrack->content.localPath().c_str()); \
    audioSource->setStartFrame(TimeToFrame(typedTrack->lifetime.begin_time, spec.fps)); \
    audioSource->setDuration(LifetimeToFrameDuration(typedTrack->lifetime, spec.fps)); \
    audioSource->setSpeed(typedTrack->content.speed); \
    audioSource->setCutFrom(1000*(int64_t)(typedTrack->content.cutFrom * typedTrack->content.speed)); \
    audioSource->setVolumeForMix(typedTrack->content.mixVolume); \
    audioSource->setLoop(typedTrack->content.loop); \
    if (typedTrack->type == "voice") \
      audioSource->setType(AudioSourceType::Voice); \
    else \
      audioSource->setType(AudioSourceType::Bgm); \
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

void prepareAllTracks(movie::Story& story) {
  printf("prepareAllTracks, duration:%d\n", story.duration);
  //add water mark
  auto waterMark = new movie::TitleTrack();
  waterMark->type = "title";
  waterMark->lifetime.begin_time = 0;
  waterMark->lifetime.end_time = story.duration;
  waterMark->zorder = 1000;
  waterMark->content.text = ".";
  waterMark->content.location.center_x = 0.0f;
  waterMark->content.location.center_y = 0.95f;
  waterMark->content.fontSize = 0.1f;
  waterMark->content.textColor = "#777777";
  story.tracks.push_back(waterMark);

  //check duration of all tracks
  for (auto& t : story.tracks) {
    t->lifetime.end_time = std::min(t->lifetime.end_time, story.duration);
  }

  //remove tracks with zero duration
  story.tracks.erase(std::remove_if(story.tracks.begin(), story.tracks.end(), [](movie::Track* t) { return t->lifetime.end_time <= t->lifetime.begin_time; }), story.tracks.end());

  //sort tracks by zorder
  std::sort(story.tracks.begin(), story.tracks.end(), [](const auto& a, const auto& b) {
      return a->zorder < b->zorder;
  });
}

std::shared_ptr<JSONComposition> JSONComposition::Load(const std::string& json_str, std::string tmp_dir) {
    json nmjson = json::parse(json_str);
    movie::Movie movie = nmjson.get<movie::Movie>();
    movie::Story story = movie.video.stories[0];
    printf("JSONComposition::Load, json to movie\n");

    std::string tmpDir;
    if (tmp_dir.empty()) {
      std::string tmpDir1 = pag::getPlatformTemporaryDirectory();
      printf("JSONComposition::Load, platform tmp dir:%s\n", tmpDir1.c_str());
      char tempTemplate[] = "tmp_XXXXXX";
      std::string tempFolder(mkdtemp(tempTemplate));
      tmpDir = tmpDir1 + "/" + tempFolder;
      printf("JSONComposition::Load, tmp dir:%s\n", tmpDir.c_str());
  #if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
      if (mkdir(tmpDir.c_str(), 0755) == 0) {
          printf("Temp Directory created: %s\n", tmpDir.c_str());
      } else {
          std::cerr << "Error creating directory" << std::endl;
      }
  #else
      #error "Unsupported platform"
  #endif
    } else {
      tmpDir = tmp_dir;
      printf("customer tmp dir: %s\n", tmpDir.c_str());
    }

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
    
    //prepare all tracks for rendering
    prepareAllTracks(story);
    
    //add track to PAGLayer one by one
    for (auto& t : story.tracks) {
        if (t->type == "video") {
            //create video PAGComposition and add to JSONComposition
            auto track = static_cast<movie::VideoTrack*>(t);
            track->content.init(tmpDir);
            printf("video track, path:%s\n", track->content.path.c_str());
            auto vidPreComposeLayer = createVideoLayer(track, movie.video);
            if (vidPreComposeLayer != nullptr) {
              vecComposition->layers.push_back(vidPreComposeLayer);
              //zzy, must set "containingComposition" for fps sampler
              vidPreComposeLayer->containingComposition = vecComposition;
              auto pagVideoLayer = std::make_shared<PAGComposition>(nullptr, vidPreComposeLayer);
              //zzy, must set frame rate in case of null PAGFile, but it is different from "setFrameRate"
              pagVideoLayer->setVideoFrameRate(movie.video.fps);
              jsonComposition->addLayer(pagVideoLayer);

              //add audio source
              if (track->content.mixVolume > MIN_VOLUME) {
                auto audioSource = createAudioSource(t->type, track , movie.video);
                jsonComposition->addAudioSource(audioSource);
              }
            } else {
              std::cerr << "Error creating video layer, maybe codec not support" << std::endl;
            }
        } else if (t->type == "gif") {
            auto track = static_cast<movie::GifTrack*>(t);
            printf("gif track, path:%s\n", track->content.path.c_str());
        } else if (t->type == "voice") {
            auto track = static_cast<movie::VoiceTrack*>(t);
            track->content.init(tmpDir);
            printf("voice track, path:%s\n", track->content.path.c_str());
            //add audio source
            if (track->content.mixVolume > MIN_VOLUME) {
              auto audioSource = createAudioSource(t->type, track , movie.video);
              jsonComposition->addAudioSource(audioSource);
            }
        } else if (t->type == "music") {
            auto track = static_cast<movie::MusicTrack*>(t);
            track->content.init(tmpDir);
            printf("music track, path:%s\n", track->content.path.c_str());
            //add audio source
            if (track->content.mixVolume > MIN_VOLUME) {
              auto audioSource = createAudioSource(t->type, track , movie.video);
              jsonComposition->addAudioSource(audioSource);
            }
        } else if (t->type == "image") {
            auto track = static_cast<movie::ImageTrack*>(t);
            track->content.init(tmpDir);
            printf("image track, path:%s\n", track->content.path.c_str());
            auto layer = CreateImageLayer(track, movie.video);
            vecComposition->layers.push_back(layer);
            auto pagImageLayer = std::make_shared<PAGImageLayer>(nullptr, layer);
            //zzy, must set frame rate in case of null PAGFile
            pagImageLayer->setFrameRate(movie.video.fps); 
            jsonComposition->addLayer(pagImageLayer);
        } else if (t->type == "title") {
            auto track = static_cast<movie::TitleTrack*>(t);
            printf("title track, text:%s\n", track->content.text.c_str());
            auto textLayers = createTextLayers(track, movie.video);
            for (auto layer : textLayers) {
              vecComposition->layers.push_back(layer);
              auto pagTextLayer = std::make_shared<PAGTextLayer>(nullptr, layer);
              //zzy, must set frame rate in case of null PAGFile
              pagTextLayer->setFrameRate(movie.video.fps); 
              jsonComposition->addLayer(pagTextLayer);
            }
        } else if (t->type == "subtitle") {
            auto track = static_cast<movie::SubtitleTrack*>(t);
            printf("subtitle track, count:%zu\n", track->content.sentences.size());
            auto textLayers = createTextLayers(track, movie.video);
            for (auto layer : textLayers) {
              vecComposition->layers.push_back(layer);
              auto pagTextLayer = std::make_shared<PAGTextLayer>(nullptr, layer);
              //zzy, must set frame rate in case of null PAGFile
              pagTextLayer->setFrameRate(movie.video.fps); 
              jsonComposition->addLayer(pagTextLayer);
            }
        }
    }

    //update static time range, I am not sure if it is necessary
    if (!vecComposition->staticTimeRangeUpdated) {
      vecComposition->updateStaticTimeRanges();
      vecComposition->staticTimeRangeUpdated = true;
    }
    
    //zzy, must not do this in destructor of story, because the destructor is called by nlohmann::json ahead of time
    for (auto& track : story.tracks) {
        delete track;
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




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
#include "rendering/utils/media/FFImageReader.h"
#include "rendering/graphics/Glyph.h"

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
#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>
#include <unordered_set>


#if defined(__linux__)
#include <sys/stat.h>
#include <sys/types.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/stat.h>
#include <sys/types.h>
#endif


namespace fs = std::filesystem;

//NOTE:
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
    // Check if file already exists
    if (fs::exists(localPath)) {
        std::cout << "curlDownload, localPath already exists: " << localPath << std::endl;
        return 0;
    }
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

int pickColorFromImage(const std::string& image_path, uint8_t* r, uint8_t* g, uint8_t* b) {
    pag::FFImageReader imgReeader(image_path);
    int width = imgReeader.width();
    int height = imgReeader.height();
    if (width <= 0 || height <= 0) {
      return -1;
    }
    float offsetRatio = 0.1f;
    int samples[4][2] = {
        {(int)std::round(width * offsetRatio),             (int)std::round(height * offsetRatio)},
        {width - (int)std::round(width * offsetRatio),     (int)std::round(height * offsetRatio)},
        {(int)std::round(width * offsetRatio),             height - (int)std::round(height * offsetRatio)},
        {width - (int)std::round(width * offsetRatio),     height - (int)std::round(height * offsetRatio)}
    };
    int rows = sizeof(samples) / sizeof(samples[0]);
    int tr = 0, tg = 0, tb = 0;
    for (int row = 0; row < rows; row++) {
        int x = samples[row][0];
        int y = samples[row][1];
        uint8_t r, g, b;
        if (imgReeader.getColor(x, y, &r, &g, &b) < 0) {
            std::cerr << "Failed to get color at (" << x << ", " << y << ")" << std::endl;
            tr = 0;
            tg = 0;
            tb = 0;
            break;
        }
        tr += r;
        tg += g;
        tb += b;
    }
    *r = tr / rows;
    *g = tg / rows;
    *b = tb / rows;
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

int ArticleContent::init(const std::string& tmpDir) {
  if (backgroundColor.empty()) {
    if (!backgroundColorFromImage.empty()) {
      bool remote = starts_with(backgroundColorFromImage, "http://") || starts_with(backgroundColorFromImage, "https://");
      if (remote) {
          //create local path
          _bgcImagePath = tmpDir + "/" + getFileNameFromUrl(backgroundColorFromImage);
          
          //download to local path
          printf("ArticleContent::init, will download %s to %s\n", backgroundColorFromImage.c_str(), _bgcImagePath.c_str());
          auto tick1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
          if (curlDownload(backgroundColorFromImage, _bgcImagePath) < 0) {
              return -1;
          }
          auto tick2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
          printf("ArticleContent::init, download done, cost: %ds\n", (int)(tick2-tick1).count()/1000);
      } else {
        _bgcImagePath = backgroundColorFromImage;
      }
      uint8_t r, g, b;
      if (pickColorFromImage(_bgcImagePath, &r, &g, &b) == 0) {
        float compensate = 0.8;
        r = r + std::ceil((255 - r) * compensate);
        g = g + std::ceil((255 - g) * compensate);
        b = b + std::ceil((255 - b) * compensate);
        uint8_t max = std::max(std::max(r, g), b);
        r += 255 - max;
        g += 255 - max;
        b += 255 - max;
        std::stringstream ss;
        ss << "rgb(" << (int)r << "," << (int)g << "," << int(b) << ")";
        backgroundColor = ss.str();
      }
    }
  }
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

// 定义一些常量
#define MAX_CHARS_PER_LINE 16
#define FIT_CHARS_PER_LINE 12

int TimeToFrame(int time, float fps) {
    return (int)std::floor(time / 1000.0f * fps);
}

int LifetimeToFrameDuration(const movie::LifeTime& lifetime, float fps) {
    return TimeToFrame(lifetime.end_time, fps) - TimeToFrame(lifetime.begin_time, fps);
}


typedef std::set<std::pair<float, float>> FloatPairSet;
FloatPairSet strokeWidthSet = {
    //{fontsize, strokeWidth}
    //中心描边
    // {0.03,      0},
    // {0.05,      4},
    // {0.07,      5},
    // {0.1,       6}
    //外描边
    {0.03,      0},
    {0.05,      3*2},
    {0.07,      4*2},
    {0.1,       5*2}
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static bool isChineseChar(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF); // 中文字符范围
}

static bool isEnglishChar(char32_t c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')); // 英文字母范围
}

void countChars(const std::u32string& unicodeStr, size_t& chineseCount, size_t& englishCount) {
    chineseCount = 0;
    englishCount = 0;

    // 遍历 Unicode 字符串
    for (char32_t c : unicodeStr) {
        if (isChineseChar(c)) {
            ++chineseCount;
        } else if (isEnglishChar(c)) {
            ++englishCount;
        }
    }
}

static std::vector<std::string> splitStringByNewline(const std::string& input) {
    std::vector<std::string> result;
    std::stringstream ss(input);
    std::string line;
    while (std::getline(ss, line, '\n')) {
        result.push_back(line);
    }
    return result;
}

static std::string getFileNameWithoutExtension(const std::string& filePath) {
    // 使用 std::filesystem 提取文件名
    std::filesystem::path path(filePath);
    return path.stem().string(); // stem() 返回不带扩展名的文件名
}

//static bool isLineBreak(char32_t unicode) {
//    return unicode == U'\n' ||   // Line Feed
//           unicode == U'\r' ||   // Carriage Return
//           unicode == U'\u2028' || // Line Separator
//           unicode == U'\u2029';  // Paragraph Separator
//}

//static bool isUnicodeSpace(char32_t ch) {
//    static const std::unordered_set<char32_t> unicode_spaces = {
//        U' ',  // U+0020 空格
//        U'\t', U'\n', U'\r', U'\f', U'\v',  // 控制字符
//        U'\u00A0',  // 不间断空格 (No-Break Space)
//        U'\u1680',  // Ogham Space Mark
//        U'\u2000', U'\u2001', U'\u2002', U'\u2003', U'\u2004', U'\u2005',
//        U'\u2006', U'\u2007', U'\u2008', U'\u2009', U'\u200A',  // 多种空格
//        U'\u202F',  // Narrow No-Break Space
//        U'\u205F',  // Medium Mathematical Space
//        U'\u3000'   // Ideographic Space (全角空格)
//    };
//    return unicode_spaces.find(ch) != unicode_spaces.end();
//}

//static bool isEnglishPunctuation(char32_t ch) {
//    static const std::unordered_set<char32_t> englishPunctuationSet = {
//        U'.', U',', U';', U':', U'?', U'!', U'\'', U'"',
//        U'(', U')', U'[', U']', U'{', U'}', U'-', U'_',
//        U'/', U'\\', U'@', U'#', U'&', U'*', U'%', U'+', U'=', U' ',
//        U'“', U'”', U'‘', U'’', U'—', U'…', U'·'    //中文，但是只占半个字符
//    };
//    return englishPunctuationSet.find(ch) != englishPunctuationSet.end();
//}
//
//static bool isChinesePunctuation(char32_t ch) {
//    static const std::unordered_set<char32_t> chinesePunctuationSet = {
//        U'。', U'，', U'、', U'？', U'！', U'：', U'；',
//        U'（', U'）', U'【', U'】', U'《', U'》', U'〈', U'〉', U'～', U'\u3000'
//    };
//    return chinesePunctuationSet.find(ch) != chinesePunctuationSet.end();
//}

TextLayer* createTextLayer(const std::string& text, movie::TitileContent* content, const movie::LifeTime& lifetime, const movie::MovieSpec& spec) {
  int visual_width = std::min(spec.width, spec.height) * content->fontSize;
  int visual_height = visual_width;
  
  auto textLayer = new TextLayer();
  textLayer->id = UniqueID::Next();
  textLayer->startTime = TimeToFrame(lifetime.begin_time, spec.fps);
  textLayer->duration = LifetimeToFrameDuration(lifetime, spec.fps);
  textLayer->transform = Transform2D::MakeDefault().release();
  textLayer->transform->anchorPoint->value.set(0, -30); //hard code
  textLayer->transform->position->value.set(spec.width*content->location.center_x, spec.height*content->location.center_y);
  textLayer->timeRemap = new Property<float>(0);      //hard code

  auto textData = new TextDocument();
  textData->fontSize = std::max(visual_width, visual_height);     //in pixel
  // textData->fontStyle = "bold"; //hardcode, SHOULD NOT BE USED if the font family does not support the style
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
    textData->strokeOverFill = false; //先描边再填充，这样可以实现外描边效果
    textData->tracking = textData->strokeWidth * 1000 * 0.7 / textData->fontSize;   //横向间距，如果使用了外描边
  }
  textData->justification = pag::ParagraphJustification::CenterJustify;   //hard code

  textLayer->sourceText = new Property<TextDocumentHandle>(pag::TextDocumentHandle(textData));
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
        
        // 将 UTF-8 字符串转换为 Unicode 字符串（char32_t）
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
        std::u32string unicodeStr = converter.from_bytes(sentence.text);
        size_t chineseCount = 0, englishCount = 0;
        countChars(unicodeStr, chineseCount, englishCount);
        int charCount = (int)(chineseCount + englishCount);
        if (charCount > MAX_CHARS_PER_LINE) {
          int lineCount = (int)std::round((float)charCount / FIT_CHARS_PER_LINE);
          int charPerLine = (int)std::round((float)charCount / lineCount);
          int durPerLine = (int)std::round((float)(lifetime.end_time - lifetime.begin_time) / lineCount);
          std::cout << "sentence text too long, text:" << sentence.text << ", length:" << charCount << ", lineCount:" << lineCount << ", charPerLine:" << charPerLine << ", durPerLine:" << durPerLine << std::endl;
          for (int i=0; i<lineCount; i++) {
            movie::LifeTime lineLifetime;
            lineLifetime.begin_time = lifetime.begin_time + i * durPerLine;
            lineLifetime.end_time = std::min(lineLifetime.begin_time + durPerLine, lifetime.end_time);

            std::u32string subUnicodeText = (i == lineCount - 1) ? unicodeStr.substr(i*charPerLine) : unicodeStr.substr(i*charPerLine, charPerLine);
            std::string utf8SubText = converter.to_bytes(subUnicodeText);

            auto textLayer = createTextLayer(utf8SubText, content, lineLifetime, spec);
            textLayers.push_back(textLayer);
          }
        } else {
          auto textLayer = createTextLayer(sentence.text, content, lifetime, spec);
          textLayers.push_back(textLayer);  
        }
      }
    }
    return textLayers;
}

//use "GetLines" in TextRenderer.cpp to get the exact text lines
std::pair<std::vector<std::vector<GlyphHandle>>, tgfx::Rect> GetLines(
    const TextDocument* textDocument, const TextPathOptions* pathOptions);

int testTheTextLines(const std::string& fontFamilyName, const std::string& text, int fontSize, int leading, int tracking, int boxWidth) {
  //test to get the exact text lines
  auto textData = new TextDocument();
  textData->fontSize = fontSize;
  textData->text = text;
  if (!fontFamilyName.empty()) {
    textData->fontFamily = findEnglishFontName(getFileNameWithoutExtension(fontFamilyName));     //set by json
  }
  textData->justification = pag::ParagraphJustification::LeftJustify;   //hard code
  textData->leading = leading;
  textData->tracking = std::max(std::min((int)std::round(tracking * 1000.0f / fontSize), 1000), 0);   //横向间距，protect tracking
  //// box text will affect the real anchor point, so we can hard code it to the top-center of a paragraph(x=w/2, y=0)
  textData->boxText = true;     //hard code
  textData->boxTextSize = pag::Point::Zero();
  // set the boxTextSize exactly same as the paragraph size, so that the real anchor point is the top-center
  textData->boxTextSize.x = boxWidth;
  textData->boxTextSize.y = 1920*1000;   //very big value, enough to carry the long text
  textData->boxTextPos = pag::Point::Make((int)(textData->boxTextSize.x*(-0.5)), 0);    //y is top and x is minus center
  textData->firstBaseLine = 0;   //hard code
  textData->avoidFirstPunctuation = true;
  auto [lines, bounds] = GetLines(textData, nullptr);
  delete textData;
  return (int)lines.size();
}

movie::ArticleParagraph formatParagraph(const std::string& fontFamilyName, const std::string& text, int fontSize, 
      int leading/*纵向*/, int tracking/*横向*/, int boxWidth, bool indented) {
  std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
  std::u32string unicodeStr = converter.from_bytes(text);
  if (indented) {
    // std::u32string indent1(1, U'\u3000');  // `\u3000` 为全角空格，更适合中文缩进
    //但是有的字体不能处理全角空格，比如“阿里巴巴普惠体-常规.ttf”，所以都用半角
    std::u32string indent(6, U' ');
    unicodeStr = indent + unicodeStr;
  }

  movie::ArticleParagraph p;
  p.text = converter.to_bytes(unicodeStr);
  //test to get the exact text lines
  int lines = testTheTextLines(fontFamilyName, p.text, fontSize, leading, tracking, boxWidth);
  p.heightInP = lines * leading;
  return p;
}

std::vector<Layer*> createArticleRelatedLayers(movie::ArticleTrack* articleTrack, const movie::MovieSpec& spec, int duration) {
  std::vector<Layer*> layers;
  movie::ArticleContent* content = &articleTrack->content;
  int width = spec.width;
  int height = spec.height;
  //tricky: 可以只考虑文章的长度，不用考虑第一段从哪个位置开始，最后一段在哪个位置结束。
  //因为一般情况下第一段是从中间位置开始，所以结束也在中间位置结束
  int fontSize = std::round(std::min(width, height) * content->fontSize);
  int leadingInP = std::ceil(content->verticalSpacing * fontSize) + fontSize;
  int trackingInP = std::ceil(content->horizontalSpacing * fontSize);
  int boxWidth = std::round(width * (content->horizontalVisibleScope.right - content->horizontalVisibleScope.left));
  boxWidth -= std::round(content->horizontalVisibleScope.indent * fontSize) * 2;
  boxWidth = boxWidth - boxWidth % (fontSize+trackingInP);
  float speedInP = height * content->speed;
  float movePerFrame = speedInP / spec.fps;
  int startPosition = std::round(content->startPosition * height);
  int startPositionFrame = std::round((content->startPosition * height) / movePerFrame);
  //how many frames of the "begin freeze" will occupy
  int framesOfBeginFreeze = std::round(content->freezeBeginTime / 1000.0f * spec.fps);
  int framesOfEndFreeze = std::round(content->freezeEndTime / 1000.0f * spec.fps);
  int totalFrames = std::round(duration / 1000.0f * spec.fps);
  //the frame point before the "end freeze" starts
  int framesBeforeEndFreeze = totalFrames - framesOfEndFreeze;
  int visibleWidth = std::round(width * (content->horizontalVisibleScope.right - content->horizontalVisibleScope.left));
  int visibleHeight = std::round(height * (content->verticalVisibleScope.bottom - content->verticalVisibleScope.top));
  int visibleMiddleX = std::round(width * (content->horizontalVisibleScope.left + content->horizontalVisibleScope.right) / 2);
  int visibleMiddleY = std::round(height * (content->verticalVisibleScope.top + content->verticalVisibleScope.bottom) / 2);

  //step 1: create bgc layer(background color)
  if (!content->backgroundColor.empty()) {
    auto bgColor = translateColor(content->backgroundColor);
    auto solidLayer = new SolidLayer();
    solidLayer->id = UniqueID::Next();
    solidLayer->startTime = 0;      //hard code
    solidLayer->duration = TimeToFrame(spec.stories[0].duration, spec.fps);
    solidLayer->transform = Transform2D::MakeDefault().release();
    solidLayer->transform->anchorPoint->value.set(visibleMiddleX, visibleMiddleY);
    solidLayer->transform->position->value.set(visibleMiddleX, visibleMiddleY);
    solidLayer->timeRemap = new Property<float>(0);      //hard code
    solidLayer->name = "纯色背景";
    //暂时忽略蒙板masks，来年再做
    auto maskData = new MaskData();
    maskData->id = UniqueID::Next();
    auto pathData = std::make_shared<PathData>();
    auto top = visibleMiddleY - visibleHeight/2;
    auto bottom = visibleMiddleY + visibleHeight/2;
    auto left = visibleMiddleX - visibleWidth/2;
    auto right = visibleMiddleX + visibleWidth/2;
    if (content->backgroundShape == "rectangle") {
      pathData->moveTo(left, top);
      pathData->lineTo(right, top);
      pathData->lineTo(right, bottom);
      pathData->lineTo(left, bottom);
      pathData->close();  //close will make it a closed path
    } else if (content->backgroundShape == "round-rectangle") {

    } else if (content->backgroundShape == "hexagon") {
      int radius = std::round(std::min(visibleWidth, visibleHeight) / 20.0f);
      pathData->moveTo(left + radius, top);
      pathData->lineTo(right - radius, top);
      pathData->lineTo(right, top + radius);
      pathData->lineTo(right, bottom - radius);
      pathData->lineTo(right - radius, bottom);
      pathData->lineTo(left + radius, bottom);
      pathData->lineTo(left, bottom - radius);
      pathData->lineTo(left, top + radius);
      pathData->close();  //close will make it a closed path
    } else {
      std::cerr << "Invalid background shape: " << content->backgroundShape << std::endl;
    }
    
    maskData->maskPath = new Property<PathHandle>(pathData);
    maskData->maskOpacity = new Property<Opacity>(255);      //hard code
    maskData->maskExpansion = new Property<float>(0);      //hard code
    solidLayer->masks.push_back(maskData);

    solidLayer->solidColor = bgColor;
    solidLayer->width = width;
    solidLayer->height = height;
      
    layers.push_back(solidLayer);
  }

  //step 2: create shape layer for "遮罩"
  auto shapeLayer = new ShapeLayer();
  shapeLayer->id = UniqueID::Next();
  shapeLayer->startTime = 0;      //hard code
  shapeLayer->duration = TimeToFrame(spec.stories[0].duration, spec.fps);
  shapeLayer->transform = Transform2D::MakeDefault().release();
  shapeLayer->transform->anchorPoint->value.set(0, 0);                //hard code
  shapeLayer->transform->position->value.set(visibleMiddleX, visibleMiddleY);
  shapeLayer->timeRemap = new Property<float>(0);                     //hard code
  shapeLayer->name = "文字遮罩";
  shapeLayer->isActive = false;      //de-active by default

  auto shapeElement = new ShapeGroupElement();
  shapeElement->transform = new ShapeTransform();
  shapeElement->transform->anchorPoint = new Property<Point>(pag::Point::Make(0, 0));      //hard code
  shapeElement->transform->position = new Property<Point>(pag::Point::Make(0, 0));         //hard code
  shapeElement->transform->scale = new Property<Point>(pag::Point::Make(1, 1));            //hard code
  shapeElement->transform->skew = new Property<float>(0);             //hard code
  shapeElement->transform->skewAxis = new Property<float>(0);         //hard code
  shapeElement->transform->rotation = new Property<float>(0);         //hard code
  shapeElement->transform->opacity = new Property<Opacity>(255);      //hard code

  auto rectElement = new RectangleElement();
  rectElement->size = new Property<Point>(pag::Point::Make(visibleWidth, visibleHeight - int(content->verticalVisibleScope.indent*fontSize*2)));
  rectElement->position = new Property<Point>(pag::Point::Make(0, 0));        //hard code
  rectElement->roundness = new Property<float>(0);                            //hard code
  shapeElement->elements.push_back(rectElement);
  auto strokeElement = new StrokeElement();
  strokeElement->color = new Property<pag::Color>(pag::White);                //hard code
  strokeElement->opacity = new Property<Opacity>(255);                        //hard code
  strokeElement->strokeWidth = new Property<float>(0);                        //hard code
  strokeElement->miterLimit = new Property<float>(4);                         //hard code
  shapeElement->elements.push_back(strokeElement);
  auto fillElement = new FillElement();
  fillElement->color = new Property<pag::Color>(pag::White);                  //hard code
  fillElement->opacity = new Property<Opacity>(255);                          //hard code
  shapeElement->elements.push_back(fillElement);
  
  shapeLayer->contents.push_back(shapeElement);
  //no need to push shapeLayer to layers, because it is invisible
  //layers.push_back(shapeLayer);

  //step 2: create text layers
  int frame = 0;
  int spaceInP = std::round(content->paragraphSpacing * fontSize);    //vertial space in pixels
  for (auto& p : content->paragraphs) {
    // get paragraph height in pixels
    auto heightInP = p.heightInP;
    // step 1: create text data
    auto textData = new TextDocument();
    textData->fontSize = fontSize;
    textData->text = p.text;
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
      textData->strokeOverFill = false; //先描边再填充，这样可以实现外描边效果
    }
    textData->justification = pag::ParagraphJustification::LeftJustify;   //hard code
    textData->leading = leadingInP;
    textData->tracking = std::max(std::min((int)std::round(trackingInP * 1000.0f / fontSize), 1000), 0);   //横向间距，protect tracking
    //// box text will affect the real anchor point, so we can hard code it to the top-center of a paragraph(x=w/2, y=0)
    textData->boxText = true;     //hard code
    textData->boxTextSize = pag::Point::Zero();
    // set the boxTextSize exactly same as the paragraph size, so that the real anchor point is the top-center
    textData->boxTextSize.x = boxWidth;
    textData->boxTextSize.y = heightInP;
    textData->boxTextPos = pag::Point::Make((int)(textData->boxTextSize.x*(-0.5)), 0);    //y is top and x is minus center
    textData->firstBaseLine = 0;   //hardcode
    textData->avoidFirstPunctuation = true;
    
    //step 2: create text layer
    auto textLayer = new TextLayer();
    textLayer->id = UniqueID::Next();
    textLayer->transform = Transform2D::MakeDefault().release();
    textLayer->transform->anchorPoint->value.set(0, 0); //hard code
    textLayer->transform->position->value.set(width/2, height/2);    //hard code
    textLayer->timeRemap = new Property<float>(0);      //hard code
    textLayer->sourceText = new Property<TextDocumentHandle>(pag::TextDocumentHandle(textData));
    textLayer->trackMatteType = TrackMatteType::Alpha;
    textLayer->trackMatteLayer = shapeLayer;  //set shaper layer to text layer as matte(遮罩)
      
    //step 3: create key frames for movement
    std::vector<Keyframe<pag::Point>*> keyframes = {};
    if (frame < startPositionFrame) {
      textLayer->startTime = 0;
      textLayer->duration = std::round((heightInP + spaceInP + height) / movePerFrame) - startPositionFrame + frame + framesOfBeginFreeze;
      int staticX = visibleMiddleX;
      int staticY = height - startPosition + std::round(frame * movePerFrame);
      //if the key frame's start time is bigger than the layer's start time, the layer is still until the key frame's start time
      //this is perfect for the "still and move" case
      auto keyFrame = new SingleEaseKeyframe<pag::Point>();
      keyFrame->startTime = textLayer->startTime + framesOfBeginFreeze;
      keyFrame->endTime = textLayer->startTime + textLayer->duration;
      keyFrame->startValue = pag::Point::Make(staticX, staticY);                       //from bottom 
      keyFrame->endValue = pag::Point::Make(staticX, - heightInP - spaceInP);          //to top
      keyFrame->interpolationType = KeyframeInterpolationType::Linear;  //hard code
      keyframes.push_back(keyFrame);
    } else {
      textLayer->startTime = frame - startPositionFrame + framesOfBeginFreeze;
      textLayer->duration = std::round((heightInP + spaceInP + height) / movePerFrame);
      
      //cut the duration if it is beyond the "end freeze" point
      if (textLayer->startTime + textLayer->duration > framesBeforeEndFreeze) {
        Frame cutDuration = framesBeforeEndFreeze - textLayer->startTime;
        //this text's lifetime go beyond the "end freeze" point and we could not let it move after the "end freeze" point 
        //so cut it by recalculating the key frame, and make it stop at the very "end freeze" point 
        //and also make the duration longer so it will be still after key frame ends and keep still to the end of the article
        auto keyFrame = new SingleEaseKeyframe<pag::Point>();
        keyFrame->startTime = textLayer->startTime;
        keyFrame->endTime = textLayer->startTime + cutDuration;
        keyFrame->startValue = pag::Point::Make(visibleMiddleX, height);                                //from bottom
        keyFrame->endValue = pag::Point::Make(visibleMiddleX, height - int(cutDuration * movePerFrame));      //to top
        keyFrame->interpolationType = KeyframeInterpolationType::Linear;  //hard code
        keyframes.push_back(keyFrame);

        //set the duration of the remaining text to the end of article
        textLayer->duration = totalFrames - textLayer->startTime;
      } else {
        //the text's lifetime doesn't go beyond the "end freeze" point
        //so no need to cut it, and let it go
        auto keyFrame = new SingleEaseKeyframe<pag::Point>();
        keyFrame->startTime = textLayer->startTime;
        keyFrame->endTime = textLayer->startTime + textLayer->duration;
        keyFrame->startValue = pag::Point::Make(visibleMiddleX, height);                    //from bottom
        keyFrame->endValue = pag::Point::Make(visibleMiddleX, - heightInP - spaceInP);      //to top
        keyFrame->interpolationType = KeyframeInterpolationType::Linear;  //hard code
        keyframes.push_back(keyFrame);
      }
      
    }
    if (textLayer->transform->position) {
      delete textLayer->transform->position;
    }
    textLayer->transform->position = new AnimatableProperty<pag::Point>(keyframes);
    //add one text layer to layers
    layers.push_back(textLayer);
    //IMPORTANT: update frame for the next paragraph
    frame += std::round((heightInP + spaceInP) / movePerFrame);
    // std::cout << "paragraph height:" << heightInP << ", space:" << spaceInP 
    //   << ", start:" << textLayer->startTime << ", duration:" << textLayer->duration
    //   << ", frame:" << frame << ", movePerFrame:" << movePerFrame << std::endl;
  }

  return layers;
}

#pragma clang diagnostic pop

ImageLayer* createImageLayer(movie::Track* track, const movie::MovieSpec& spec) {
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

  //test code
  // {
  //     //hard code animation: scale
  //     auto keyFrame1 = new SingleEaseKeyframe<pag::Point>();
  //     keyFrame1->startTime = -200;
  //     keyFrame1->endTime = imageLayer->duration;
  //     keyFrame1->startValue = pag::Point::Make(spec.width*content->location.center_x, 2400.0);   //set by json
  //     keyFrame1->endValue = pag::Point::Make(spec.width*content->location.center_x, 0.0f);   //set by json
  //     keyFrame1->interpolationType = KeyframeInterpolationType::Linear;  //hard code
  //     std::vector<Keyframe<pag::Point>*> keyframes = {};
  //     keyframes.push_back(keyFrame1);
  //     //release former scale property
  //     if (imageLayer->transform->position) {
  //       delete imageLayer->transform->position;
  //     }
  //     imageLayer->transform->position = new AnimatableProperty<pag::Point>(keyframes);
  //   }

  return imageLayer;
}

#define CREATE_AUDIO_SOURCE(typedTrack, spec) \
    audioSource = std::make_shared<PAGAudioSource>(typedTrack->content.localPath().c_str(), typedTrack->type == "voice" ? AudioSourceType::Voice : AudioSourceType::Bgm); \
    audioSource->setStartFrame(TimeToFrame(typedTrack->lifetime.begin_time, spec.fps)); \
    audioSource->setDuration(LifetimeToFrameDuration(typedTrack->lifetime, spec.fps)); \
    audioSource->setSpeed(typedTrack->content.speed); \
    audioSource->setCutFrom(1000*(int64_t)(typedTrack->content.cutFrom * typedTrack->content.speed)); \
    audioSource->setVolumeForMix(typedTrack->content.mixVolume); \
    audioSource->setLoop(typedTrack->content.loop);

std::shared_ptr<PAGAudioSource> createAudioSource(const std::string& type, movie::Track* track, const movie::MovieSpec& spec) {
  std::shared_ptr<PAGAudioSource> audioSource;
  if (type == "music") {
    auto typedTrack = static_cast<movie::MusicTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
    if (typedTrack->content.enhance) {
      audioSource->setSuppress(true);   //"enhance" 响度统一
    }
  } else if (type == "voice") {
    auto typedTrack = static_cast<movie::VoiceTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
  } else if (type == "video") {
    auto typedTrack = static_cast<movie::VideoTrack*>(track);
    CREATE_AUDIO_SOURCE(typedTrack, spec);
  }
  return audioSource;
}

int getArticleDuration(movie::ArticleTrack* articleTrack, int width, int height) {
  //tricky: 可以只考虑文章的长度，不用考虑第一段从哪个位置开始，最后一段在哪个位置结束。
  //因为一般情况下第一段是从中间位置开始，所以结束也在中间位置结束
  int articleHeight = 0;
  int fontSize = std::round(std::min(width, height) * articleTrack->content.fontSize);
  //vertial space in pixels
  int spaceInP = std::round(articleTrack->content.paragraphSpacing * fontSize);
  for (auto& p : articleTrack->content.paragraphs) {
    articleHeight += p.heightInP + spaceInP;
  }
  float speedInP = height * articleTrack->content.speed;
  int duration = std::round((float)articleHeight * 1000 / speedInP);
  duration += articleTrack->content.freezeBeginTime;
  duration += articleTrack->content.freezeEndTime;
  return duration;
}

void preProcessArticleText(movie::ArticleTrack* articleTrack) {
  std::string new_text = articleTrack->content.text;
  //删除英文空格
  new_text.erase(std::remove(new_text.begin(), new_text.end(), ' '), new_text.end());

  //todo: 

  articleTrack->content.text = new_text;
}

void prepareArticleTrack(movie::ArticleTrack* articleTrack, int width, int height) {
  preProcessArticleText(articleTrack);
  auto texts = splitStringByNewline(articleTrack->content.text);
  int fontSize = std::round(std::min(width, height) * articleTrack->content.fontSize);
  int leading = std::ceil(articleTrack->content.verticalSpacing * fontSize) + fontSize;
  int tracking = std::ceil(articleTrack->content.horizontalSpacing * fontSize);
  int boxWidth = std::round(width * (articleTrack->content.horizontalVisibleScope.right - articleTrack->content.horizontalVisibleScope.left));
  boxWidth -= std::round(articleTrack->content.horizontalVisibleScope.indent * fontSize) * 2;
  boxWidth = boxWidth - boxWidth % (fontSize+tracking);
  for (auto& t : texts) {
    auto p = formatParagraph(articleTrack->content.fontFamilyName, t, fontSize, leading/*纵向*/, tracking/*横向*/, boxWidth, articleTrack->content.indented);
    articleTrack->content.paragraphs.push_back(p);
  }
}

void prepareAllTracks(movie::Story* story, int width, int height, [[maybe_unused]]float fps) {
  printf("prepareAllTracks, duration:%d\n", story->duration);

  //check if article track exists
  int articleDuration = 0;
  for (auto& track : story->tracks) {
    if (track->type == "article") {
      movie::ArticleTrack* articleTrack = static_cast<movie::ArticleTrack*>(track);
      prepareArticleTrack(articleTrack, width, height);
      articleDuration = getArticleDuration(articleTrack, width, height);
      printf("article duration:%d\n", articleDuration);
      break;
    }
  }
  //modify lifetime and duration
  if (articleDuration > 0) {
    for (auto& track : story->tracks) {
      track->lifetime.end_time = articleDuration;
    }
    story->duration = articleDuration;
  }

  //add water mark
  auto waterMark = new movie::TitleTrack();
  waterMark->type = "title";
  waterMark->lifetime.begin_time = 0;
  waterMark->lifetime.end_time = story->duration;
  waterMark->zorder = 1000;
  waterMark->content.text = ".";
  waterMark->content.location.center_x = 0.1f;
  waterMark->content.location.center_y = 0.9f;
  waterMark->content.fontSize = 0.05f;
  waterMark->content.textColor = "#777777";
  story->tracks.push_back(waterMark);

  //check duration of all tracks
  for (auto& t : story->tracks) {
    t->lifetime.end_time = std::min(t->lifetime.end_time, story->duration);
  }

  //remove tracks with zero duration
  story->tracks.erase(std::remove_if(story->tracks.begin(), story->tracks.end(), [](movie::Track* t) { return t->lifetime.end_time <= t->lifetime.begin_time; }), story->tracks.end());

  //sort tracks by zorder
  std::sort(story->tracks.begin(), story->tracks.end(), [](const auto& a, const auto& b) {
      return a->zorder < b->zorder;
  });
}

std::shared_ptr<JSONComposition> JSONComposition::Load(const std::string& json_str, std::string tmp_dir, const std::function<void(int)>& progressCB) {
    json nmjson = json::parse(json_str);
    movie::Movie movie = nmjson.get<movie::Movie>();
    if (movie.video.stories.size() != 1) {
      std::cerr << "Error: only support one story" << std::endl;
      return nullptr;
    }
    movie::Story* story = &movie.video.stories[0];

    //prepare all tracks for rendering(including article tracks and water mark)
    prepareAllTracks(story, movie.video.width, movie.video.height, movie.video.fps);

    //last check
    if (story->duration == 0) {
      std::cerr << "Error: story duration is zero" << std::endl;
      return nullptr;
    }
    printf("JSONComposition::Load, json to movie\n");
    if (progressCB) {
      progressCB(0);
    }

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
    vecComposition->duration = TimeToFrame(story->duration, movie.video.fps);   //set by json
    vecComposition->frameRate = movie.video.fps;           //set by json
    vecComposition->backgroundColor = {0, 0, 0};     //hard code

    //create JSONComposition
    auto preComposeLayer = PreComposeLayer::Wrap(vecComposition).release();
    auto jsonComposition = std::shared_ptr<JSONComposition>(new JSONComposition(preComposeLayer));
    jsonComposition->rootLocker = std::make_shared<std::mutex>();
    jsonComposition->_videoEncodeBitrateKPBS = movie.video.fileSizeLimit > 0 ? movie.video.fileSizeLimit*8 / story->duration : 0;   //set by json
    
    //add track to PAGLayer one by one
    int tCount = 0;
    for (auto& t : story->tracks) {
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
            auto layer = createImageLayer(track, movie.video);
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
        } else if (t->type == "article") {
            auto track = static_cast<movie::ArticleTrack*>(t);
            track->content.init(tmpDir);
            printf("article track, count:%zu\n", track->content.text.size());
            auto layers = createArticleRelatedLayers(track, movie.video, story->duration);
            for (auto layer : layers) {
              vecComposition->layers.push_back(layer);
              if (layer->type() == LayerType::Text) {
                auto pagTextLayer = std::make_shared<PAGTextLayer>(nullptr, (TextLayer*)layer);
                //zzy, must set frame rate in case of null PAGFile
                pagTextLayer->setFrameRate(movie.video.fps);
                if (layer->trackMatteLayer) {
                  //shape layer as track matte
                  pagTextLayer->_trackMatteLayer = std::make_shared<PAGShapeLayer>(nullptr, (ShapeLayer*)layer->trackMatteLayer);
                  pagTextLayer->_trackMatteLayer->weakThis = pagTextLayer->_trackMatteLayer;
                  pagTextLayer->_trackMatteLayer->trackMatteOwner = pagTextLayer.get();
                  pagTextLayer->_trackMatteLayer->setFrameRate(movie.video.fps);
                }
                jsonComposition->addLayer(pagTextLayer);
              } else if (layer->type() == LayerType::Solid) {
                auto pagSolidLayer = std::make_shared<PAGSolidLayer>(nullptr, (SolidLayer*)layer);
                //zzy, must set frame rate in case of null PAGFile
                pagSolidLayer->setFrameRate(movie.video.fps); 
                jsonComposition->addLayer(pagSolidLayer);
              } else if (layer->type() == LayerType::Shape) {
                auto pagShapeLayer = std::make_shared<PAGShapeLayer>(nullptr, (ShapeLayer*)layer);
                //zzy, must set frame rate in case of null PAGFile
                pagShapeLayer->setFrameRate(movie.video.fps);
                jsonComposition->addLayer(pagShapeLayer);
              }
            }
        }
        if (progressCB) {
          progressCB(tCount++ * 100 / story->tracks.size());
        }
    }

    //update static time range, I am not sure if it is necessary
    if (!vecComposition->staticTimeRangeUpdated) {
      vecComposition->updateStaticTimeRanges();
      vecComposition->staticTimeRangeUpdated = true;
    }
    
    //zzy, must not do this in destructor of story, because the destructor is called by nlohmann::json ahead of time
    for (auto& track : story->tracks) {
        delete track;
    }

    if (progressCB) {
      progressCB(100);
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
  auto audioSource = std::make_shared<PAGAudioSource>(tokens[2], AudioSourceType::Voice);
  audioSource->setStartFrame(0);
  audioSource->setDuration(TEST_DURATION);
  jsonComposition->addAudioSource(audioSource);

  auto musicSource = std::make_shared<PAGAudioSource>(tokens[0], AudioSourceType::Bgm);
  musicSource->setStartFrame(0);
  musicSource->setDuration(TEST_DURATION);
  jsonComposition->addAudioSource(musicSource);

  return jsonComposition;
}


JSONComposition::JSONComposition(PreComposeLayer* layer)
  : PAGComposition(nullptr, layer) {

}

int JSONComposition::videoEncodeBitrateKPBS() const {
  return _videoEncodeBitrateKPBS;
}


}  // namespace pag




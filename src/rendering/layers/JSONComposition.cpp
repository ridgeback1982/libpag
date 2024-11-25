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
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavutil/avutil.h>
  #include <libavutil/channel_layout.h>
  #include <libavutil/audio_fifo.h>
  #include <libswscale/swscale.h>
  #include <libavutil/imgutils.h>
}

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

void JSONComposition::WriteToFile(const std::string& path, std::shared_ptr<JSONComposition> jsonComposition) {
  printf("%s \n", path.c_str());
  if (jsonComposition != nullptr) {
    auto width = jsonComposition->width();
    auto height = jsonComposition->height();
    auto fps = jsonComposition->frameRate();
    auto pagSurface = pag::PAGSurface::MakeOffscreen(width, height);
    if (pagSurface == nullptr) {
      return;
    }
    auto pagPlayer = new pag::PAGPlayer();
    pagPlayer->setSurface(pagSurface);
    pagPlayer->setComposition(std::shared_ptr<pag::PAGComposition>(jsonComposition));

    auto totalFrames = TimeToFrame(jsonComposition->duration(), fps);
    auto currentVideoFrame = 0;
    auto currentAudioFrame = 0;
    int bytesLength = width * height * 4;
    auto rgba_data = new uint8_t[bytesLength];
      
    std::chrono::milliseconds click1;
    std::chrono::milliseconds click2;

    int ret = 0;
    bool audio_read_drained = false;
    uint8_t** audioChunks = nullptr;
    uint8_t* audioChunk = nullptr;    //zzy, currently support single channel
    int audioChunkSize = 0;
    AVAudioFifo* audioFifo = nullptr;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    const AVCodec *video_codec = NULL;
    const AVCodec *audio_codec = NULL;
    AVFormatContext* format_context = nullptr;
    const float audio_frame_duration = 0.02f;
    const int audio_target_samplerate = 44100;
    const int audio_target_channels = 1;

    //初始化ffmpeg
    avformat_network_init();
    // 创建输出文件上下文
    avformat_alloc_output_context2(&format_context, nullptr, nullptr, path.c_str());
    if (!format_context) {
      fprintf(stderr, "无法分配输出文件上下文\n");
      return;
    }

    // video part
    {
      // 查找 H.264 编码器
      video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
      // 设置编码器上下文
      video_codec_ctx = avcodec_alloc_context3(video_codec);
      video_codec_ctx->codec_id = video_codec->id;
      video_codec_ctx->bit_rate = 2000000;
      video_codec_ctx->width = width;
      video_codec_ctx->height = height;
      video_codec_ctx->time_base.num = 1;
      video_codec_ctx->time_base.den = (int)fps;
      video_codec_ctx->framerate.num = (int)fps;
      video_codec_ctx->framerate.den = 1;
      video_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
      // 打开编码器
      AVDictionary *opt = nullptr;
      av_dict_set(&opt, "tune", "zerolatency", 0);
      av_dict_set(&opt, "profile", "baseline", 0);
      if (avcodec_open2(video_codec_ctx, video_codec, &opt) < 0) {
        fprintf(stderr, "无法打开编码器\n");
        return;
      }
      // 创建新的视频流
      video_stream = avformat_new_stream(format_context, nullptr);
      if (!video_stream) {
        fprintf(stderr, "无法创建视频流\n");
        return;
      }
      ret = avcodec_parameters_from_context(video_stream->codecpar, video_codec_ctx);
      if (ret < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        return;
      }
      // 设置流的时间基
      video_stream->time_base = video_codec_ctx->time_base;
    }
    
    //audio part
    {
      // 设置编码器上下文
      audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
      audio_codec_ctx = avcodec_alloc_context3(audio_codec);
      audio_codec_ctx->codec_id = AV_CODEC_ID_AAC;
      audio_codec_ctx->sample_rate = audio_target_samplerate;
      av_channel_layout_default(&audio_codec_ctx->ch_layout, audio_target_channels);
      audio_codec_ctx->bit_rate = 64000;
      audio_codec_ctx->sample_fmt = audio_codec->sample_fmts[0];
      audio_codec_ctx->time_base.num = 1;
      audio_codec_ctx->time_base.den = (int)audio_codec_ctx->sample_rate;
      if (avcodec_open2(audio_codec_ctx, audio_codec, NULL) < 0) {
          fprintf(stderr, "Could not open audio codec\n");
          return;
      }
      // 创建新的音频流
      audio_stream = avformat_new_stream(format_context, nullptr);
      ret = avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_ctx);
      if (ret < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        return;
      }
      audio_stream->time_base = audio_codec_ctx->time_base;

      //创建音频混合器
      auto audio_mixer = std::make_shared<FFAudioMixer> (audio_codec_ctx->sample_rate, audio_codec_ctx->ch_layout.nb_channels, audio_codec_ctx->sample_fmt);
      jsonComposition->setAudioMixer(audio_mixer);
    }

    // 打开输出文件
    if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&format_context->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "无法打开输出文件\n");
        return;
      }
    }

    // 写文件头
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "movflags", "faststart", 0);
    ret = avformat_write_header(format_context, &opts);
    if (ret < 0) {
      fprintf(stderr, "写入文件头信息失败, ret:%d\n", ret);
      return;
    }
    // 初始化 RGB 到 YUV420P 的转换上下文
    SwsContext* sws_context = sws_getContext(
      width, height, AV_PIX_FMT_RGBA, 
      width, height, AV_PIX_FMT_YUV420P, 
      SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    // 创建视频帧
    AVFrame* video_frame = av_frame_alloc();
    if (!video_frame) {
      fprintf(stderr, "frame类型分配失败\n");
      return;
    }
    video_frame->format = AV_PIX_FMT_YUV420P;
    video_frame->width = width;
    video_frame->height = height;
    if (av_image_alloc(video_frame->data, video_frame->linesize, width, height, AV_PIX_FMT_YUV420P, 32) < 0) {
      fprintf(stderr, "图像内存分配失败\n");
      return;
    }
    // 创建音频帧
    AVFrame *audio_frame = av_frame_alloc();
    audio_frame->format = audio_codec_ctx->sample_fmt;
    audio_frame->ch_layout = audio_codec_ctx->ch_layout;
    audio_frame->sample_rate = audio_codec_ctx->sample_rate;
    printf("audio codec context, frame_size:%d\n", audio_codec_ctx->frame_size);
    audio_frame->nb_samples = audio_codec_ctx->frame_size;
    if (av_frame_get_buffer(audio_frame, 0) < 0) {
      fprintf(stderr, "音频内存分配失败\n");
      return;
    }
    int audioSamplesPerChunk = (int)(audio_codec_ctx->sample_rate * audio_frame_duration);
    audioChunkSize = av_get_bytes_per_sample(audio_codec_ctx->sample_fmt) * audioSamplesPerChunk;
    audioChunk = new uint8_t[audioChunkSize];
    if (audioChunk == nullptr) {
      fprintf(stderr, "音频内存分配失败\n");
      return;
    }
    audioChunks = new uint8_t*[1];
    audioChunks[0] = audioChunk;
    audioFifo = av_audio_fifo_alloc(audio_codec_ctx->sample_fmt, audio_codec_ctx->ch_layout.nb_channels, audioSamplesPerChunk);

    // 获取当前时间点
    click1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    while (currentVideoFrame <= totalFrames) {
      pagPlayer->setProgress((currentVideoFrame + 0.1) * 1.0 / totalFrames);  //tricky: to avoid frame skip
      auto status = pagPlayer->flush();

      printf("---currentVideoFrame:%d, flushStatus:%d \n", currentVideoFrame, status);

      auto res = pagSurface->readPixels(pag::ColorType::RGBA_8888, pag::AlphaType::Premultiplied, rgba_data,
                            width * 4);
      printf("---readPixels, res:%d \n", res);

      //write to file(encode, container, etc.)
      // 视频编码
      {
        // 转换 RGBA 到 YUV420P
        uint8_t* inData[1] = { rgba_data };
        int inLinesize[1] = { 4 * width };
        sws_scale(sws_context, inData, inLinesize, 0, height, video_frame->data,video_frame->linesize);
        // 设置 PTS
        video_frame->pts = currentVideoFrame;
        // 编码帧
        AVPacket* packet = av_packet_alloc();
        if (!packet) { 
          printf("Could not allocate packet.\n");
          goto end;
        }
        // 发送帧到编码器
        ret = avcodec_send_frame(video_codec_ctx, video_frame);
        if (ret < 0) {
          fprintf(stderr, "错误编码帧 %d\n", ret);
          goto end;
        }
        // 从编码器接收 AVPacket
        while (ret >= 0) {
          ret = avcodec_receive_packet(video_codec_ctx, packet);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            fprintf(stderr, "编码错误\n");
            goto end;
          }
          packet->stream_index = video_stream->index;
          av_packet_rescale_ts(packet, video_codec_ctx->time_base, video_stream->time_base);
          ret = av_interleaved_write_frame(format_context, packet);
          if (ret < 0) {
            fprintf(stderr, "写入文件失败\n");
            goto end;
          }
          printf("write video to file, pts:%lld, frames:%d\n", (long long)video_frame->pts, currentVideoFrame);
          av_packet_unref(packet);
        }
      }

      //音频编码
      {
        while(jsonComposition->getAudioFrameNumber(audio_codec_ctx->sample_rate) < currentVideoFrame) {
          // 从文件里读取一个音频帧
          if (jsonComposition->readAudioBySamples(audioSamplesPerChunk, audioChunk, audioChunkSize, audio_frame->sample_rate, audio_frame->format, audio_frame->ch_layout.nb_channels) > 0) {
            if (av_audio_fifo_realloc(audioFifo, av_audio_fifo_size(audioFifo) + audioSamplesPerChunk) < 0) {
              printf("cannot reallocate audio fifo\n");
              goto end;
            }
            if (av_audio_fifo_write(audioFifo, (void**)audioChunks, audioSamplesPerChunk) < 0) {
              printf("failed to write to audio fifo\n");
              goto end;
            }
          } else {
            printf("failed to read audio by frame %d.\n", currentVideoFrame);
            audio_read_drained = true;
          }
          
          while (av_audio_fifo_size(audioFifo) > 0) {
            if (!audio_read_drained) {
              if (av_audio_fifo_size(audioFifo) < audio_frame->nb_samples) {
                break;
              }
            }
            
            //memset(audio_frame->data[0], 0, audio_frame->linesize[0]);
            int targetSamples = std::min(av_audio_fifo_size(audioFifo), audio_frame->nb_samples);
            if (av_audio_fifo_read(audioFifo, (void**)audio_frame->data, targetSamples) < 0) {
              printf("failed to write to audio fifo\n");
              goto end;
            }
            audio_frame->pts = currentAudioFrame * targetSamples;
            // 编码帧
            AVPacket* packet = av_packet_alloc();
            if (!packet) {
              printf("Could not allocate packet.\n");
              goto end;
            }

            // 发送帧到编码器
  //         float *audio_data = reinterpret_cast<float*>(audio_frame->data[0]);
  //          for (int i = 0; i < audio_frame->nb_samples * av_get_bytes_per_sample((AVSampleFormat)audio_frame->format); ++i) {
  //              if (std::isnan(audio_data[i])) {
  //                  fprintf(stderr, "Invalid value detected: %f at index %d\n", audio_data[i], i);
  //              }
  //              if (std::isinf(audio_data[i])) {
  //                  fprintf(stderr, "Invalid value detected: %f at index %d\n", audio_data[i], i);
  //              }
  //          }


            ret = avcodec_send_frame(audio_codec_ctx, audio_frame);
            if (ret < 0) {
              fprintf(stderr, "错误编码帧 %d\n", ret);
              goto end;
            }
            // 从编码器接收 AVPacket
            while (ret >= 0) {
              ret = avcodec_receive_packet(audio_codec_ctx, packet);
              if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
              } else if (ret < 0) {
                fprintf(stderr, "编码错误\n");
                goto end;
              }
              packet->stream_index = audio_stream->index;
              av_packet_rescale_ts(packet, audio_codec_ctx->time_base, audio_stream->time_base);
              ret = av_interleaved_write_frame(format_context, packet);
              if (ret < 0) {
                fprintf(stderr, "写入文件失败\n");
                goto end;
              }
              printf("write audio to file, pts:%lld, frames:%d\n", (long long)audio_frame->pts, currentAudioFrame);
              currentAudioFrame ++;
              av_packet_unref(packet);
            }
          }
        }
      }
      
      currentVideoFrame++;
    }
    click2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    printf("reading %lld frames, cost %lld ms \n", (long long)totalFrames, (click2 - click1).count());

    // 写入文件尾
    if (av_write_trailer(format_context) < 0) {
      fprintf(stderr, "写入文件尾失败\n");
      goto end;
    }

end:
    // 清理资源
    avcodec_free_context(&video_codec_ctx);
    avio_closep(&format_context->pb);
    avformat_free_context(format_context);
    av_frame_free(&video_frame);
    sws_freeContext(sws_context);
    av_audio_fifo_free(audioFifo);

    delete audioChunk;
    delete[] audioChunks;
    delete[] rgba_data;
    delete pagPlayer;
  }
    
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

std::shared_ptr<JSONComposition> JSONComposition::Load(const std::string& json) {
  //pass the process of parsing json file
  printf("JSONComposition::Load, json:%s", json.c_str());

  
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
  auto videoSequence = ReadVideoSequenceFromFile(tokens[2]);
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
  vidPreComposeLayer->startTime = 0;              //set by json
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
  auto pagTextLayer = std::make_shared<PAGTextLayer>(nullptr, textLayer);
  auto pagVideoLayer = std::make_shared<PAGComposition>(nullptr, vidPreComposeLayer);


  //create JSONComposition
  auto preComposeLayer = PreComposeLayer::Wrap(vecComposition).release();
  auto jsonComposition = std::shared_ptr<JSONComposition>(new JSONComposition(preComposeLayer));
  jsonComposition->rootLocker = std::make_shared<std::mutex>();

  //add layer to pag composition
//  jsonComposition->addLayer(pagVideoLayer);
//  jsonComposition->addLayer(pagImageLayer);
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

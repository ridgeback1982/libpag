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

#include "FileReporter.h"
#include "rendering/caches/RenderCache.h"
#include "rendering/layers/PAGStage.h"

namespace pag {

int64_t GetAverage(int64_t totalData, int count) {
  if (totalData == 0 || count == 0) {
    return 0;
  }
  return totalData / count;
}

std::unique_ptr<FileReporter> FileReporter::Make(std::shared_ptr<PAGLayer> pagLayer) {
  FileReporter* reporter = nullptr;
  while (pagLayer) {
    auto file = pagLayer->getFile();
    if (file) {
      reporter = new FileReporter(file.get());
      break;
    }
    if (pagLayer->layerType() != LayerType::PreCompose ||
        std::static_pointer_cast<PAGComposition>(pagLayer)->layers.empty()) {
      break;
    }
    pagLayer = std::static_pointer_cast<PAGComposition>(pagLayer)->layers.front();
  }
  if (reporter) {
    return std::unique_ptr<FileReporter>(reporter);
  } else {
    return std::make_unique<FileReporter>(nullptr);
  }
}

FileReporter::FileReporter(File* file) {
  if (file) {
    setFileInfo(file);
  }
}

FileReporter::~FileReporter() {
  reportData();
}

void FileReporter::setFileInfo(File* file) {
  auto frameRate = static_cast<int>(file->frameRate());
  std::string separator = "|";
  pagInfoString =
      file->path + separator + std::to_string(frameRate) + separator +
      std::to_string(file->duration()) + separator + std::to_string(file->width()) + separator +
      std::to_string(file->height()) + separator + std::to_string(file->numLayers()) + separator +
      std::to_string(file->numVideos()) + separator + std::to_string(file->tagLevel()) + separator;
}

void FileReporter::reportData() {
  //std::map<std::string, std::string> reportInfos;
  std::vector<std::pair<std::string, std::string>> reportInfos;
  reportInfos.push_back(
      std::make_pair("presentFirstFrameTime", std::to_string(presentFirstFrameTime/1000)));
  reportInfos.push_back(std::make_pair("presentTotalTime", std::to_string(presentTotalTime/1000)));
  reportInfos.push_back(std::make_pair("presentAverageTime",
                                    std::to_string(GetAverage(presentTotalTime, flushCount - 1)/1000)));
  reportInfos.push_back(std::make_pair("renderFirstFrameTime", std::to_string(renderFirstFrameTime/1000)));
  reportInfos.push_back(std::make_pair("renderTotalTime", std::to_string(renderTotalTime/1000)));
  reportInfos.push_back(std::make_pair("renderAverageTime",
                                    std::to_string(GetAverage(renderTotalTime, flushCount - 1)/1000)));
  // reportInfos.push_back(std::make_pair("flushFirstFrameTime", std::to_string(flushFirstFrameTime/1000)));
  // reportInfos.push_back(std::make_pair("flushMaxTime", std::to_string(flushMaxTime/1000)));
  // reportInfos.push_back(std::make_pair("flushAverageTime",
  //                                   std::to_string(GetAverage(flushTotalTime, flushCount - 1)/1000)));

  //zzy, debug
  reportInfos.push_back(std::make_pair("jobMaxTime", std::to_string(jobMaxTime/1000)));
  reportInfos.push_back(std::make_pair("jobTotalTime", std::to_string(jobTotalTime/1000)));
  reportInfos.push_back(std::make_pair("jobAverageTime",
                                    std::to_string(GetAverage(jobTotalTime, flushCount - 1)/1000)));

  reportInfos.push_back(std::make_pair("imageDecodingMaxTime", std::to_string(imageDecodingMaxTime/1000)));
  reportInfos.push_back(
      std::make_pair("hardwareDecodingInitialTime", std::to_string(hardwareDecodingInitialTime/1000)));
  reportInfos.push_back(
      std::make_pair("hardwareDecodingTotalTime", std::to_string(hardwareDecodingTotalTime/1000)));
  reportInfos.push_back(
      std::make_pair("hardwareDecodingAverageTime",
                     std::to_string(GetAverage(hardwareDecodingTotalTime, hardwareDecodingCount)/1000)));
  reportInfos.push_back(std::make_pair("softwareDecodingCount", std::to_string(softwareDecodingCount)));
  reportInfos.push_back(
      std::make_pair("softwareDecodingInitialTime", std::to_string(softwareDecodingInitialTime/1000)));
  reportInfos.push_back(
      std::make_pair("softwareDecodingTotalTime", std::to_string(softwareDecodingTotalTime/1000)));
  reportInfos.push_back(
      std::make_pair("softwareDecodingAverageTime",
                     std::to_string(GetAverage(softwareDecodingTotalTime, softwareDecodingCount)/1000)));
  reportInfos.push_back(std::make_pair("softwareDecodingCount", std::to_string(softwareDecodingCount)));
  reportInfos.push_back(std::make_pair("graphicsMemoryMax", std::to_string(graphicsMemoryMax)));
  reportInfos.push_back(std::make_pair("graphicsMemoryAverage",
                                    std::to_string(GetAverage(graphicsMemoryTotal, flushCount))));
  reportInfos.push_back(std::make_pair("flushCount", std::to_string(flushCount)));
  reportInfos.push_back(std::make_pair("pagInfo", pagInfoString));
  reportInfos.push_back(std::make_pair("event", "pag_monitor"));
  reportInfos.push_back(std::make_pair("version", PAG::SDKVersion()));
  reportInfos.push_back(std::make_pair("usability", "1"));

  //zzy, print report
  for (auto info : reportInfos) {
    std::cout << "[report] " << info.first << " = " << info.second << std::endl;
  }
}

static int64_t GetOldRenderTime(RenderCache* cache) {
  // 性能统计增加了数据字段，部分含义发生了改变，这里拼装出原先的
  // renderingTime，确保数据上报结果跟之前一致。
  return cache->totalTime - cache->presentingTime - cache->imageDecodingTime;
}

void FileReporter::recordPerformance(RenderCache* cache) {
  flushCount++;

  if (presentFirstFrameTime == 0) {
    presentFirstFrameTime = cache->presentingTime;
  } else {
    presentMaxTime = std::max(presentMaxTime, cache->presentingTime);
    presentTotalTime += cache->presentingTime;
  }

  // if (renderFirstFrameTime == 0) {
  //   renderFirstFrameTime = GetOldRenderTime(cache);
  // } else {
  //   renderMaxTime = std::max(renderMaxTime, GetOldRenderTime(cache));
  //   renderTotalTime += GetOldRenderTime(cache);
  // }

  if (renderFirstFrameTime == 0) {
    renderFirstFrameTime = cache->renderingTime;
  } else {
    renderMaxTime = std::max(renderMaxTime, cache->renderingTime);
    renderTotalTime += cache->renderingTime;
  }

  // if (flushFirstFrameTime == 0) {
  //   flushFirstFrameTime = presentFirstFrameTime + renderFirstFrameTime;
  // } else {
  //   flushMaxTime = std::max(flushMaxTime, cache->presentingTime + GetOldRenderTime(cache));
  //   flushTotalTime = presentTotalTime + renderTotalTime;
  // }

  //zzy
  jobMaxTime = std::max(jobMaxTime, cache->totalTime);
  jobTotalTime += cache->totalTime;

  if (cache->softwareDecodingTime > 0) {
    softwareDecodingMaxTime = std::max(softwareDecodingMaxTime, cache->softwareDecodingTime);
    softwareDecodingTotalTime += cache->softwareDecodingTime;
    softwareDecodingCount++;
  }

  if (cache->hardwareDecodingTime > 0) {
    hardwareDecodingMaxTime = std::max(hardwareDecodingMaxTime, cache->hardwareDecodingTime);
    hardwareDecodingTotalTime += cache->hardwareDecodingTime;
    hardwareDecodingCount++;
  }

  imageDecodingMaxTime = std::max(imageDecodingMaxTime, cache->imageDecodingTime);
  graphicsMemoryMax = std::max(graphicsMemoryMax, cache->memoryUsage());
  graphicsMemoryTotal += cache->memoryUsage();
  softwareDecodingInitialTime =
      std::max(softwareDecodingInitialTime, cache->softwareDecodingInitialTime);
  hardwareDecodingInitialTime =
      std::max(hardwareDecodingInitialTime, cache->hardwareDecodingInitialTime);
}

}  // namespace pag

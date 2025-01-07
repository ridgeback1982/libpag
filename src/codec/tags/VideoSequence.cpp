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

#include <iostream>
#include "VideoSequence.h"
#include "codec/utils/NALUReader.h"
//zzy
#include "platform/Platform.h"
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
}

namespace pag {
VideoSequence* ReadVideoSequence(DecodeStream* stream, bool hasAlpha) {
  auto sequence = new VideoSequence();
  sequence->width = stream->readEncodedInt32();
  sequence->height = stream->readEncodedInt32();
  sequence->frameRate = stream->readFloat();

  if (hasAlpha) {
    sequence->alphaStartX = stream->readEncodedInt32();
    sequence->alphaStartY = stream->readEncodedInt32();
  }

  auto sps = ReadByteDataWithStartCode(stream);
  auto pps = ReadByteDataWithStartCode(stream);
  sequence->headers.push_back(sps.release());
  sequence->headers.push_back(pps.release());

  auto count = stream->readEncodedUint32();
  for (uint32_t i = 0; i < count; i++) {
    auto videoFrame = new VideoFrame();
    sequence->frames.push_back(videoFrame);
    videoFrame->isKeyframe = stream->readBitBoolean();
  }
  for (uint32_t i = 0; i < count; i++) {
    auto videoFrame = sequence->frames[i];
    videoFrame->frame = ReadTime(stream);
    videoFrame->fileBytes = ReadByteDataWithStartCode(stream).release();
  }

  if (stream->bytesAvailable() > 0) {
    count = stream->readEncodedUint32();
    for (uint32_t i = 0; i < count; i++) {
      TimeRange staticTimeRange = {};
      staticTimeRange.start = ReadTime(stream);
      staticTimeRange.end = ReadTime(stream);
      sequence->staticTimeRanges.push_back(staticTimeRange);
    }
  }

  return sequence;
}

//zzy
std::unique_ptr<ByteData> ConvertToByteDataWithStartCode(const uint8_t* buf, const int length) {
  auto data = new (std::nothrow) uint8_t[length + 4];
  if (data == nullptr) {
    return nullptr;
  }
  memcpy(data + 4, buf, length);
  if (Platform::Current()->naluType() == NALUType::AVCC) {
    // AVCC
    data[0] = static_cast<uint8_t>((length >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((length >> 16) & 0x00FF);
    data[2] = static_cast<uint8_t>((length >> 8) & 0x0000FF);
    data[3] = static_cast<uint8_t>(length & 0x000000FF);
  } else {
    // Annex B Prefix
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 1;
  }
  return ByteData::MakeAdopted(data, length + 4);
}

//zzy
void parse_h264_extradata(uint8_t *extradata, int extradata_size, VideoSequence* sequence) {
    if (extradata_size < 7) {
        std::cerr << "Extradata too small" << std::endl;
        return;
    }

    // AVCDecoderConfigurationRecord structure:
    // First byte: configurationVersion (should be 1)
    // Second byte: AVCProfileIndication
    // Third byte: profile_compatibility
    // Fourth byte: AVCLevelIndication
    // Fifth byte: lengthSizeMinusOne (NAL length field size - 1)
    // Sixth byte: numOfSPS (5 bits) + reserved (3 bits)
    
//    int length_size = (extradata[4] & 0x03) + 1; // lengthSizeMinusOne is stored here, +1 to get real size
//    printf("Get length:%d", length_size);

    // Read SPS
    int num_of_sps = extradata[5] & 0x1F; // Number of SPS NAL units (5 bits)
    int offset = 6;

    for (int i = 0; i < num_of_sps; i++) {
        int sps_length = (extradata[offset] << 8) | extradata[offset + 1]; // 2 bytes for SPS length
        offset += 2;
        uint8_t *sps = extradata + offset; // SPS NAL unit starts here

        // std::cout << "Found SPS of length " << sps_length << ", sps:" << sps[0] << std::endl;   //without start code
        ByteData* sps_byte = ConvertToByteDataWithStartCode(sps, sps_length).release();
        sequence->headers.push_back(sps_byte);

        // You can process or store the SPS here
        offset += sps_length;
    }

    // Read PPS
    int num_of_pps = extradata[offset]; // Number of PPS NAL units
    offset += 1;

    for (int i = 0; i < num_of_pps; i++) {
        int pps_length = (extradata[offset] << 8) | extradata[offset + 1]; // 2 bytes for PPS length
        offset += 2;
        uint8_t *pps = extradata + offset; // PPS NAL unit starts here

        // std::cout << "Found PPS of length " << pps_length << ", pps:" << pps[0] << std::endl;    //widthout startcode
        ByteData* pps_byte = ConvertToByteDataWithStartCode(pps, pps_length).release();
        sequence->headers.push_back(pps_byte);

        // You can process or store the PPS here
        offset += pps_length;
    }
}

//zzy
void parse_h265_extradata(uint8_t *extradata, int extradata_size, VideoSequence* sequence) {
    // Parse VPS, SPS, PPS from extradata
    if (extradata_size < 23) {
        std::cerr << "Invalid hvcC size" << std::endl;
        return;
    }

    // Skip the first 22 bytes of the hvcC header
    size_t pos = 22;

    uint8_t numOfArrays = extradata[pos++];
    for (uint8_t i = 0; i < numOfArrays; ++i) {
        uint8_t nal_unit_type = extradata[pos] & 0x3F; // Get the NAL unit type
        pos++;

        uint16_t numNalus = (extradata[pos] << 8) | extradata[pos + 1];
        pos += 2;

        std::cout << "NAL Unit Type:" << nal_unit_type << ", Number of NALUs:" << numNalus << std::endl;

        for (uint16_t j = 0; j < numNalus; ++j) {
            uint16_t nal_unit_size = (extradata[pos] << 8) | extradata[pos + 1];
            pos += 2;
            // printf("%d NALU, size:%d\n", j+1, nal_unit_size);

            // Here, extradata + pos points to the NALU data
            if (nal_unit_type != 39/*SEI*/) {
                ByteData* header_byte = ConvertToByteDataWithStartCode(extradata + pos, nal_unit_size).release();
                sequence->headers.push_back(header_byte);
            }
            pos += nal_unit_size;
        }
    }
}

//zzy
VideoSequence* ReadVideoSequenceFromFile(const std::string& filePath, const int cutFrom, const int cutTo, const int targetFrames) {
  printf("ReadVideoSequenceFromFile: %s, cutFrom:%d, cutTo:%d, targetFrames:%d\n", filePath.c_str(), cutFrom, cutTo, targetFrames);
  AVFormatContext *fmt_ctx = NULL;
  AVCodecContext *codec_ctx = NULL;
  const AVCodec *codec = NULL;
  AVPacket *pkt = NULL;
  int video_stream_index = -1;
  int64_t frame_interval = 0;
  int accuSrcFrames = 0;
  int64_t startPTS = -1;

  auto sequence = new VideoSequence();

  // 初始化 FFmpeg 库
  avformat_network_init();


  // 打开输入文件
  if (avformat_open_input(&fmt_ctx, filePath.c_str(), NULL, NULL) < 0) {
    std::cerr << "Could not open input file:" << filePath << std::endl;
    return nullptr;
  }

  // 查找流信息
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    std::cerr << "Could not find stream information" << std::endl;
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  // 查找视频流
  for (unsigned  i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
      break;
    }
  }

  if (video_stream_index == -1) {
    std::cerr << "Could not find a video stream" << std::endl;
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  // 获取解码器
  codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
  if (!codec) {
    std::cerr << "Could not find codec for video stream" << std::endl;
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  // 初始化解码器上下文
  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    std::cerr << "Could not allocate codec context" << std::endl;
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  if (avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar) < 0) {
    std::cerr << "Could not copy codec parameters to codec context" << std::endl;
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  // 打开解码器
  if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
    std::cerr << "Could not open codec" << std::endl;
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  sequence->width = fmt_ctx->streams[video_stream_index]->codecpar->width;
  sequence->height = fmt_ctx->streams[video_stream_index]->codecpar->height;
  sequence->frameRate = fmt_ctx->streams[video_stream_index]->avg_frame_rate.num / fmt_ctx->streams[video_stream_index]->avg_frame_rate.den;

  if (fmt_ctx->streams[video_stream_index]->nb_frames > 0 && fmt_ctx->streams[video_stream_index]->duration > 0) {
    frame_interval = (fmt_ctx->streams[video_stream_index]->duration / fmt_ctx->streams[video_stream_index]->nb_frames);
  }

  //seek to start frame and flush frames before
  float seekTarget = cutFrom/sequence->frameRate;
//  int64_t seekTargetStreamTime = av_rescale_q(seekTarget * AV_TIME_BASE, AV_TIME_BASE_Q, fmt_ctx->streams[video_stream_index]->time_base);
//  if (av_seek_frame(fmt_ctx, video_stream_index, seekTargetStreamTime, AVSEEK_FLAG_BACKWARD) < 0) {
//    printf("Seeking failed\n");
//    return nullptr;
//  }
  if (av_seek_frame(fmt_ctx, -1, seekTarget*AV_TIME_BASE, AVSEEK_FLAG_BACKWARD) < 0) {
    std::cerr << "Seeking failed" << std::endl;
    return nullptr;
  }
  startPTS = -1;
  //avcodec_flush_buffers(codec_ctx);

  // 分配 AVPacket
  pkt = av_packet_alloc();
  if (!pkt) {
    std::cerr << "Could not allocate packet" << std::endl;
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return nullptr;
  }

  //read sps/pps
  bool is_hevc = fmt_ctx->streams[video_stream_index]->codecpar->codec_id == AV_CODEC_ID_HEVC;
  bool is_avc = fmt_ctx->streams[video_stream_index]->codecpar->codec_id == AV_CODEC_ID_H264;
  //TBD: support other codecs, like hevc
  //hevc: parse to nal is supported, but pag decoder does not support it yet
  //vpx: not supported totally
  if (is_avc == false) {
    std::cerr << "Unsupported codec" << std::endl;
    return nullptr;
  }

  uint8_t* extradata = fmt_ctx->streams[video_stream_index]->codecpar->extradata;
  const int extradata_size = fmt_ctx->streams[video_stream_index]->codecpar->extradata_size;
  if (is_avc) {
    parse_h264_extradata(extradata, extradata_size, sequence);
  } else if (is_hevc) {
    parse_h265_extradata(extradata, extradata_size, sequence);
  } else {
    std::cerr << "Unsupported codec" << std::endl;
    return nullptr;
  }

  //check sps/pps result
  if (sequence->headers.size() < 2) {
    std::cerr << "No SPS/PPS/VPS found" << std::endl;
    return nullptr;
  }
  

  while (accuSrcFrames < cutTo - cutFrom) {
    if (av_read_frame(fmt_ctx, pkt) < 0) {
        std::cerr << "Failed to read frame, drains, accuSrcFrames:" << accuSrcFrames << ", cutFrom:" << cutFrom << ", cutTo:" << cutTo << std::endl;
        break;
    }
    if (pkt->stream_index == video_stream_index) {
      if (startPTS == -1) {
        startPTS = pkt->pts;
      }
      accuSrcFrames ++;

      // 分析 NAL 单元
      // printf("NAL Unit, flags:%d, size:%d, dts:%lld, pts:%lld\n", pkt->flags, pkt->size, pkt->dts, pkt->pts);
      int start_bit = 0;
      int nal_length = 0;

      while (/*got == false &&*/ nal_length + start_bit < pkt->size) {
        start_bit += nal_length;
        nal_length = pkt->data[start_bit] << 24 | pkt->data[start_bit + 1] << 16 | pkt->data[start_bit + 2] << 8 | pkt->data[start_bit + 3];
        start_bit += 4;
        const uint8_t* nal = pkt->data + start_bit;
        int nal_type = 0;
        if (is_avc) {
          nal_type = nal[0] & 0x1F; // Get the NAL unit type
        } else if (is_hevc) {
          nal_type = (nal[0] & 0x7E) >> 1; // Get the NAL unit type
        }
        //push to frames
        if (is_avc && (nal_type == 6/*SEI*/ || nal_type == 7/*SPS*/ || nal_type == 8/*PPS*/ || nal_type == 9/*AUD*/)) {
        } else if (is_hevc && (nal_type == 32/*VPS*/ || nal_type == 33/*SPS*/ || nal_type == 34/*PPS*/ || nal_type == 39/*SEI*/)) {
        } else {
          //if hevc, 19 is I frame, 1 is p frame
          //if avc, 5 is I frame, 1 is p frame
          auto videoFrame = new VideoFrame();
          videoFrame->isKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
          videoFrame->frame = frame_interval > 0 ? (pkt->pts - startPTS) / frame_interval : 0;
          videoFrame->fileBytes = ConvertToByteDataWithStartCode(nal, nal_length).release();
          sequence->frames.push_back(videoFrame);
        }
      }
    }
    av_packet_unref(pkt);
  }



  // 清理资源
  av_packet_free(&pkt);
  avcodec_free_context(&codec_ctx);
  avformat_close_input(&fmt_ctx);
  avformat_network_deinit();

  return sequence;
}

static void WriteByteDataWithoutStartCode(EncodeStream* stream, ByteData* byteData) {
  auto length = static_cast<uint32_t>(byteData->length());
  if (length < 4) {
    length = 0;
  } else {
    length -= 4;
  }
  stream->writeEncodedUint32(length);
  // Skip Annex B Prefix
  stream->writeBytes(byteData->data() + 4, length);
}

TagCode WriteVideoSequence(EncodeStream* stream, std::pair<VideoSequence*, bool>* parameter) {
  auto sequence = parameter->first;
  auto hasAlpha = parameter->second;
  stream->writeEncodedInt32(sequence->width);
  stream->writeEncodedInt32(sequence->height);
  stream->writeFloat(sequence->frameRate);

  if (hasAlpha) {
    stream->writeEncodedInt32(sequence->alphaStartX);
    stream->writeEncodedInt32(sequence->alphaStartY);
  }

  WriteByteDataWithoutStartCode(stream, sequence->headers[0]);  // sps
  WriteByteDataWithoutStartCode(stream, sequence->headers[1]);  // pps

  auto count = static_cast<uint32_t>(sequence->frames.size());
  stream->writeEncodedUint32(count);
  for (uint32_t i = 0; i < count; i++) {
    stream->writeBitBoolean(sequence->frames[i]->isKeyframe);
  }
  for (uint32_t i = 0; i < count; i++) {
    auto videoFrame = sequence->frames[i];
    WriteTime(stream, videoFrame->frame);
    WriteByteDataWithoutStartCode(stream, videoFrame->fileBytes);
  }

  stream->writeEncodedUint32(static_cast<uint32_t>(sequence->staticTimeRanges.size()));
  for (auto staticTimeRange : sequence->staticTimeRanges) {
    WriteTime(stream, staticTimeRange.start);
    WriteTime(stream, staticTimeRange.end);
  }

  return TagCode::VideoSequence;
}

ByteData* ReadMp4Header(DecodeStream* stream) {
  auto length = stream->readEncodedUint32();
  auto bytes = stream->readBytes(length);
  // must check whether the bytes is valid. otherwise memcpy will crash.
  if (length == 0 || length > bytes.length() || stream->context->hasException()) {
    return nullptr;
  }
  auto data = new (std::nothrow) uint8_t[length];
  if (data == nullptr) {
    return nullptr;
  }
  memcpy(data, bytes.data(), length);
  return ByteData::MakeAdopted(data, length).release();
}

TagCode WriteMp4Header(EncodeStream* stream, ByteData* byteData) {
  stream->writeEncodedUint32(static_cast<uint32_t>(byteData->length()));
  stream->writeBytes(byteData->data(), static_cast<uint32_t>(byteData->length()));
  return TagCode::Mp4Header;
}
}  // namespace pag

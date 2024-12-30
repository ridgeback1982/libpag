//  Copyright (c) 2020-2021 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "audiosource.h"
#include <iostream>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <algorithm>

bool LWAudioDecoder::ReadPacket(AVPacket *Packet) {
    while (av_read_frame(FormatContext, Packet) >= 0) {
        if (Packet->stream_index == TrackNumber)
            return true;
        av_packet_unref(Packet);
    }
    return false;
}

bool LWAudioDecoder::DecodeNextAVFrame() {
    if (!DecodeFrame) {
        DecodeFrame = av_frame_alloc();
        if (!DecodeFrame) {
            fprintf(stderr, "Couldn't allocate frame\n");
            return false;
        }
    }

    while (true) {
        int Ret = avcodec_receive_frame(CodecContext, DecodeFrame);
        if (Ret == 0) {
            return true;
        } else if (Ret == AVERROR(EAGAIN)) {
            if (!ReadPacket(Packet))
                return false;
            avcodec_send_packet(CodecContext, Packet);
            av_packet_unref(Packet);
        } else {
            break; // Probably EOF or some unrecoverable error so stop here
        }
    }

    return false;
}

void LWAudioDecoder::OpenFile(const char *SourceFile, int Track, const FFmpegOptions &Options) {
    TrackNumber = Track;

    AVDictionary *Dict = nullptr;
    av_dict_set_int(&Dict, "enable_drefs", Options.enable_drefs, 0);
    av_dict_set_int(&Dict, "use_absolute_path", Options.use_absolute_path, 0);

    if (avformat_open_input(&FormatContext, SourceFile, nullptr, &Dict) != 0) {
        std::cerr << "Couldn't open file" << std::endl;
        return;
    }

    av_dict_free(&Dict);

    if (avformat_find_stream_info(FormatContext, nullptr) < 0) {
        avformat_close_input(&FormatContext);
        FormatContext = nullptr;
        std::cerr << "Couldn't find stream information" << std::endl;
        return;
    }

    if (TrackNumber < 0) {
        for (int i = 0; i < static_cast<int>(FormatContext->nb_streams); i++) {
            if (FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                if (TrackNumber == -1) {
                    TrackNumber = i;
                    break;
                } else {
                    TrackNumber++;
                }
            }
        }
    }

    if (TrackNumber < 0 || TrackNumber >= static_cast<int>(FormatContext->nb_streams)) {
        std::cerr << "No audio track found" << std::endl;
        return;
    }
        

    if (FormatContext->streams[TrackNumber]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        std::cerr << "Not an audio track" << std::endl;
        return;
    }

    for (int i = 0; i < static_cast<int>(FormatContext->nb_streams); i++)
        if (i != TrackNumber)
            FormatContext->streams[i]->discard = AVDISCARD_ALL;

    const AVCodec *Codec = avcodec_find_decoder(FormatContext->streams[TrackNumber]->codecpar->codec_id);
    if (Codec == nullptr) {
        std::cerr << "Audio codec not found" << std::endl;
        return;
    }

    CodecContext = avcodec_alloc_context3(Codec);
    if (CodecContext == nullptr) {
        std::cerr << "Could not allocate audio decoding context" << std::endl;
        return;
    }

    if (avcodec_parameters_to_context(CodecContext, FormatContext->streams[TrackNumber]->codecpar) < 0) {
        std::cerr << "Could not copy audio parameters to decoding context" << std::endl;
        return;
    }

    // Probably guard against mid-stream format changes
    CodecContext->flags |= AV_CODEC_FLAG_DROPCHANGED;

    if (Options.drc_scale < 0) {
        std::cerr << "Invalid drc_scale value" << std::endl;
        return;
    }

    AVDictionary *CodecDict = nullptr;
    if (Codec->id == AV_CODEC_ID_AC3 || Codec->id == AV_CODEC_ID_EAC3)
        av_dict_set(&CodecDict, "drc_scale", std::to_string(Options.drc_scale).c_str(), 0);

    if (avcodec_open2(CodecContext, Codec, &CodecDict) < 0) {
        std::cerr << "Could not open audio codec" << std::endl;
        return;
    }

    av_dict_free(&CodecDict);
}

LWAudioDecoder::LWAudioDecoder(const char *SourceFile, int Track, const FFmpegOptions &Options) {
    
        Packet = av_packet_alloc();
        OpenFile(SourceFile, Track, Options);

        DecodeSuccess = DecodeNextAVFrame();
        
        if (!DecodeSuccess) {
            std::cerr << "Couldn't decode initial frame" << std::endl;
        }

        AP.IsFloat = (DecodeFrame->format == AV_SAMPLE_FMT_FLTP || DecodeFrame->format == AV_SAMPLE_FMT_FLT || DecodeFrame->format == AV_SAMPLE_FMT_DBLP || DecodeFrame->format == AV_SAMPLE_FMT_DBL);
        AP.Format = DecodeFrame->format;
        AP.BytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(DecodeFrame->format));
        AP.BitsPerSample = CodecContext->bits_per_raw_sample ? (CodecContext->bits_per_raw_sample) : (AP.BytesPerSample * 8); // assume all bits are relevant if not specified
        AP.SampleRate = DecodeFrame->sample_rate;
        AP.Channels = DecodeFrame->ch_layout.nb_channels;
//        AP.ChannelLayout = DecodeFrame->channel_layout ? DecodeFrame->channel_layout : av_get_default_channel_layout(DecodeFrame->channels);  
        AP.NumSamples = (FormatContext->duration * DecodeFrame->sample_rate) / AV_TIME_BASE - FormatContext->streams[TrackNumber]->codecpar->initial_padding;
        AP.StartTime = (DecodeFrame->best_effort_timestamp * FormatContext->streams[TrackNumber]->time_base.num * AP.SampleRate) / FormatContext->streams[TrackNumber]->time_base.den + FormatContext->streams[TrackNumber]->codecpar->initial_padding;

        if (AP.BytesPerSample <= 0) {
            std::cerr << "Codec returned zero size audio" << std::endl;
        }

}

void LWAudioDecoder::Free() {
    av_packet_free(&Packet);
    av_frame_free(&DecodeFrame);
    avcodec_free_context(&CodecContext);
    avformat_close_input(&FormatContext);
}

LWAudioDecoder::~LWAudioDecoder() {
    Free();
}

int64_t LWAudioDecoder::GetRelativeStartTime(int Track) const {
    if (Track < 0 || Track >= static_cast<int>(FormatContext->nb_streams))
        return INT64_MIN;
    return 0;
}

int64_t LWAudioDecoder::GetSamplePosition() const {
    return CurrentPosition;
}

int64_t LWAudioDecoder::GetSampleLength() const {
    if (DecodeFrame)
        return DecodeFrame->nb_samples;
    else
        return 0;
}

int64_t LWAudioDecoder::GetFrameNumber() const {
    return CurrentFrame;
}

const AudioProperties &LWAudioDecoder::GetAudioProperties() const { 
    return AP;
}

AVFrame *LWAudioDecoder::GetNextAVFrame() {
    if (DecodeSuccess) {
        CurrentPosition += DecodeFrame->nb_samples;
        CurrentFrame++;
        AVFrame *Tmp = DecodeFrame;
        DecodeFrame = nullptr;
        DecodeSuccess = DecodeNextAVFrame();
        return Tmp;
    } 
    return nullptr;
}

bool LWAudioDecoder::SkipNextAVFrame() {
    if (DecodeSuccess) {
        CurrentPosition += DecodeFrame->nb_samples;
        CurrentFrame++;
        DecodeSuccess = DecodeNextAVFrame();    
    } 
    return DecodeSuccess;
}

bool LWAudioDecoder::HasMoreFrames() const {
    return DecodeSuccess;
}

template<typename T>
static void UnpackChannels(const uint8_t *Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const T *S = reinterpret_cast<const T *>(Src);
    T *D = reinterpret_cast<T *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[Length * c] = S[c];
        S += Channels;
        D += 1;
    }
}

BestAudioSource::CacheBlock::CacheBlock(int64_t FrameNumber, int64_t Start, AVFrame *Frame) : FrameNumber(FrameNumber), Start(Start), Length(Frame->nb_samples) {
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(Frame->format))) {
        InternalFrame = Frame;
    } else {
        int BytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(Frame->format));
        LineSize = Length * BytesPerSample;
        Storage.resize(LineSize * Frame->ch_layout.nb_channels);

        if (BytesPerSample == 1)
            UnpackChannels<uint8_t>(Frame->data[0], Storage.data(), Length, Frame->ch_layout.nb_channels);
        else if (BytesPerSample == 2)
            UnpackChannels<uint16_t>(Frame->data[0], Storage.data(), Length, Frame->ch_layout.nb_channels);
        else if (BytesPerSample == 4)
            UnpackChannels<uint32_t>(Frame->data[0], Storage.data(), Length, Frame->ch_layout.nb_channels);
        else if (BytesPerSample == 8)
            UnpackChannels<uint64_t>(Frame->data[0], Storage.data(), Length, Frame->ch_layout.nb_channels);
        av_frame_free(&Frame);
    }
}

BestAudioSource::CacheBlock::~CacheBlock() {
    av_frame_free(&InternalFrame);
}

uint8_t *BestAudioSource::CacheBlock::GetPlanePtr(int Plane) {
    if (InternalFrame)
        return av_frame_get_plane_buffer(InternalFrame, Plane)->data;
    else
        return Storage.data() + Plane * LineSize;
}

BestAudioSource::BestAudioSource(const char *SourceFile, int Track, const FFmpegOptions *Options) : Source(SourceFile), Track(Track) {
    if (Options)
        FFOptions = *Options;
    Decoders[0] = new LWAudioDecoder(Source.c_str(), Track, FFOptions);
    AP = Decoders[0]->GetAudioProperties();
    MaxSize = (100 * 1024 * 1024) / (static_cast<size_t>(AP.Channels) * AP.BytesPerSample);
}

BestAudioSource::~BestAudioSource() {
    for (auto iter : Decoders)
        delete iter;
}

void BestAudioSource::SetMaxCacheSize(size_t bytes) {
    MaxSize = bytes / (static_cast<size_t>(AP.Channels) * AP.BytesPerSample);
    while (CacheSize > MaxSize) {
        CacheSize -= Cache.back().Length;
        Cache.pop_back();
    }
}

void BestAudioSource::SetSeekPreRoll(size_t samples) {
    PreRoll = static_cast<int64_t>(samples);
}

bool BestAudioSource::GetExactDuration() {
    if (HasExactNumAudioSamples)
        return true;
    int Index = -1;
    for (int i = 0; i < MaxAudioSources; i++) {
        if (Decoders[i] && (Index < 0 || Decoders[Index]->GetFrameNumber() < Decoders[i]->GetFrameNumber()))
            Index = i;
    }

    if (Index < 0) {
        Decoders[0] = new LWAudioDecoder(Source.c_str(), Track, FFOptions);
        Index = 0;
    }

    LWAudioDecoder *Decoder = Decoders[Index];

    while (Decoder->SkipNextAVFrame());
    AP.NumSamples = Decoder->GetSamplePosition();
    HasExactNumAudioSamples = true;
    delete Decoder;
    Decoders[Index] = nullptr;

    return true;
}

const AudioProperties &BestAudioSource::GetAudioProperties() const {
    return AP;
}

void BestAudioSource::ZeroFillStart(uint8_t *Data[], int64_t &Start, int64_t &Count) {
    if (Start < 0) {
        int64_t Length = std::min(Count, -Start);
        size_t ByteLength = Length * AP.BytesPerSample;
        for (int i = 0; i < AP.Channels; i++) {
            memset(Data[i], 0, ByteLength);
            Data[i] += ByteLength;
        }
        Start += Length;
        Count -= Length;
    }
}

void BestAudioSource::ZeroFillEnd(uint8_t *Data[], int64_t Start, int64_t &Count) {
    if (HasExactNumAudioSamples && (Start + Count > AP.NumSamples)) {
        if (Start > AP.NumSamples) {
            int64_t Length = Count;
            //zzy, log control
            if (_debugStart != Start) {
                fprintf(stderr, "Start is bigger than NumSamples(%s), %lld|%lld, will fill zero data\n", Source.c_str(), Start, AP.NumSamples);
                _debugStart = Start;
            }
            for (int i = 0; i < AP.Channels; i++)
                memset(Data[i], 0, Length * AP.BytesPerSample);
            Count -= Length;
        } else {
            int64_t Length = std::min(Start + Count - AP.NumSamples, Count);
            size_t ByteOffset = std::min<int64_t>(AP.NumSamples - Start, 0) * AP.BytesPerSample;
            for (int i = 0; i < AP.Channels; i++)
                memset(Data[i] + ByteOffset, 0, Length * AP.BytesPerSample);
            Count -= Length;
        }
    }

    //zzy, log control
    if (_debugStart > Start) {
        _debugStart = 0;    //reset
    }
}

bool BestAudioSource::FillInBlock(CacheBlock &Block, uint8_t *Data[], int64_t &Start, int64_t &Count) {
    if ((Start >= Block.Start) && (Start < Block.Start + Block.Length)) {
        int64_t Length = std::min(Count, Block.Length - Start + Block.Start);
        if (Length == 0)
            return false;
        size_t ByteLength = Length * AP.BytesPerSample;
        size_t ByteOffset = (Start - Block.Start) * AP.BytesPerSample;;
        for (int i = 0; i < AP.Channels; i++) {
            memcpy(Data[i], Block.GetPlanePtr(i) + ByteOffset, ByteLength);
            Data[i] += ByteLength;
        }
        Start += Length;
        Count -= Length;
        return true;
    }
    return false;
}

int64_t BestAudioSource::GetAudio(uint8_t * const * const Data, int64_t Start, int64_t Count) {
    int64_t res = Count;
    if (Count <= 0)
        return 0;

    std::vector<uint8_t *> DataV;
    DataV.reserve(AP.Channels);
    for (int i = 0; i < AP.Channels; i++)
        DataV.push_back(Data[i]);

    ZeroFillStart(DataV.data(), Start, Count);

    ZeroFillEnd(DataV.data(), Start, Count);

    if (Count == 0)
        return 0;

    // Check if in cache, use a simple check from start and end and narrow it from both ends at once
    bool FilledIn = false;
    do {
        FilledIn = false;
        for (auto &iter : Cache)
            FilledIn = FilledIn || FillInBlock(iter, DataV.data(), Start, Count);
    } while (FilledIn);

    if (Count == 0)
        return res;

    int Index = -1;
    for (int i = 0; i < MaxAudioSources; i++) {
        if (Decoders[i] && Decoders[i]->GetSamplePosition() <= Start && (Index < 0 || Decoders[Index]->GetSamplePosition() < Decoders[i]->GetSamplePosition()))
            Index = i;
    }

    // If an empty slot exists simply spawn a new decoder there
    if (Index < 0) {
        for (int i = 0; i < MaxAudioSources; i++) {
            if (!Decoders[i]) {
                Index = i;
                Decoders[i] = new LWAudioDecoder(Source.c_str(), Track, FFOptions);
                break;
            }
        }
    }

    // No far enough back decoder exists and all slots are occupied so evict a random one
    if (Index < 0) {
        Index = 0;
        for (int i = 0; i < MaxAudioSources; i++) {
            if (Decoders[i] && DecoderLastUse[i] < DecoderLastUse[Index])
                Index = i;
        }
        delete Decoders[Index];
        Decoders[Index] = new LWAudioDecoder(Source.c_str(), Track, FFOptions);
    }

    LWAudioDecoder *Decoder = Decoders[Index];
    DecoderLastUse[Index] = DecoderSequenceNum++;

    while (Count > 0 && Decoder && Decoder->GetSamplePosition() < Start + Count && Decoder->HasMoreFrames()) {
        int64_t Position = Decoder->GetSamplePosition();
        int64_t Length = Decoder->GetSampleLength();
        if (Position + Length >= Start - PreRoll) {
            int64_t FrameNumber = Decoder->GetFrameNumber();
            AVFrame *Frame = Decoder->GetNextAVFrame();
            
            for (auto iter = Cache.begin(); iter != Cache.end(); ++iter) {
                if (iter->FrameNumber == FrameNumber) {
                    Cache.erase(iter);
                    break;
                }
            }
                    
            Cache.emplace_front(FrameNumber, Position, Frame);
            CacheSize += Cache.front().Length;

            FillInBlock(Cache.front(), DataV.data(), Start, Count);

            while (CacheSize > MaxSize) {
                CacheSize -= Cache.back().Length;
                Cache.pop_back();
            }

        } else if (Position + Length < Start + Count) {
            Decoder->SkipNextAVFrame();
        }

        if (!Decoder->HasMoreFrames()) {
            AP.NumSamples = Decoder->GetSamplePosition();
            HasExactNumAudioSamples = true;
            delete Decoder;
            Decoders[Index] = nullptr;
            Decoder = nullptr;
        }
    }

    if (HasExactNumAudioSamples)
        ZeroFillEnd(DataV.data(), Start, Count);

    if (Count != 0) {
        std::cerr << "Code error, failed to provide all samples" << std::endl;
        return 0;
    }

    return res;
}

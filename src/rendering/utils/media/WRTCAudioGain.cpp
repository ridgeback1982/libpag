#include "WRTCAudioGain.h"
#include <iostream>
#include <algorithm>

//#include "dr_wav.h"
#include "webrtc/agc.h"
#include "utils/file_util.h"

//zzy
namespace pag {

WRTCAudioGain::WRTCAudioGain() {
    _s16Buffer = nullptr;
    _s16BufferedSamples = 0;

    //zzy, test
//    std::string temp_file_path = pag::getPlatformTemporaryDirectory() + "/temp.pcm";
//    _dumpFile = fopen(temp_file_path.c_str(), "wb+");
//    if (_dumpFile == nullptr) {
//        std::cerr << "cannot open dump file " << temp_file_path << std::endl;
//    }
}

WRTCAudioGain::~WRTCAudioGain() {
    if (_s16Buffer) {
        // Free the allocated memory
        av_freep(&_s16Buffer[0]);  // Free the individual channel buffers
        av_freep(&_s16Buffer);     // Free the array of pointers
    }

    if (_agcInst) {
        WebRtcAgc_Free(_agcInst);
    }
    
    //zzy, test
    if (_dumpFile) {
        fclose(_dumpFile);
    }
}

int WRTCAudioGain::agcProcess(int16_t *buffer, uint32_t sampleRate, int samplesCount, int16_t agcMode) {
    if (buffer == nullptr) return -1;
    if (samplesCount == 0) return -1;
    if (_agcInst == nullptr) {
        WebRtcAgcConfig agcConfig;
        agcConfig.compressionGaindB = 15; // default 9 dB
        agcConfig.limiterEnable = 1; // default kAgcTrue (on)
        agcConfig.targetLevelDbfs = 3; // default 3 (-3 dBOv)
        int minLevel = 0;
        int maxLevel = 255;
        _agcInst = WebRtcAgc_Create();
        if (_agcInst == NULL) return -1;
        int status = WebRtcAgc_Init(_agcInst, minLevel, maxLevel, agcMode, sampleRate);
        if (status != 0) {
            printf("WebRtcAgc_Init fail\n");
            WebRtcAgc_Free(_agcInst);
            _agcInst = nullptr;
            return -1;
        }
        status = WebRtcAgc_set_config(_agcInst, agcConfig);
        if (status != 0) {
            printf("WebRtcAgc_set_config fail\n");
            WebRtcAgc_Free(_agcInst);
            _agcInst = nullptr;
            return -1;
        }
        printf("WRTCAudioGain, init done\n");
    }
    int samples = std::min(160, (int)(sampleRate / 100));
    if (samples == 0) return -1;
    const int maxSamples = 480;
    int16_t *input = buffer;
    int nTotal = (samplesCount / samples);

    size_t num_bands = 1;
    int inMicLevel, outMicLevel = -1;
    int16_t out_buffer[maxSamples];
    int16_t *out16 = out_buffer;
    uint8_t saturationWarning = 1;                 //是否有溢出发生，增益放大以后的最大值超过了65536
    int16_t echo = 0;                                 //增益放大是否考虑回声影响
    for (int i = 0; i < nTotal; i++) {
        inMicLevel = 0;
        int nAgcRet = WebRtcAgc_Process(_agcInst, (const int16_t *const *) &input, num_bands, samples,
                                        (int16_t *const *) &out16, inMicLevel, &outMicLevel, echo,
                                        &saturationWarning);

        if (nAgcRet != 0) {
            printf("failed in WebRtcAgc_Process\n");
            WebRtcAgc_Free(_agcInst);
            return -1;
        }
        memcpy(input, out_buffer, samples * sizeof(int16_t));
        input += samples;
    }

    const size_t remainedSamples = samplesCount - nTotal * samples;
    if (remainedSamples > 0) {
        if (nTotal > 0) {
            input = input - samples + remainedSamples;
        }

        inMicLevel = 0;
        int nAgcRet = WebRtcAgc_Process(_agcInst, (const int16_t *const *) &input, num_bands, samples,
                                        (int16_t *const *) &out16, inMicLevel, &outMicLevel, echo,
                                        &saturationWarning);

        if (nAgcRet != 0) {
            printf("failed in WebRtcAgc_Process during filtering the last chunk\n");
            WebRtcAgc_Free(_agcInst);
            return -1;
        }
        memcpy(&input[samples-remainedSamples], &out_buffer[samples-remainedSamples], remainedSamples * sizeof(int16_t));
        input += samples;
    }
    return 1;
}

int WRTCAudioGain::convert_Float_to_S16(const uint8_t **float_data, uint8_t **s16_data, int nb_samples, int sample_rate, int channels) {
    struct SwrContext *swr_ctx = NULL;
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "Failed to allocate SwrContext.\n");
        return -1;
    }

    // configure the SwrContext    
    AVChannelLayout in_ch_layout, out_ch_layout;
    av_channel_layout_default(&in_ch_layout, channels);
    av_channel_layout_default(&out_ch_layout, channels);
    av_opt_set_chlayout(swr_ctx, "in_channel_layout", &in_ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_channel_layout", &out_ch_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", channels == 1 ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", channels == 1 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S16P, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", sample_rate, 0);

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Failed to initialize SwrContext.\n");
        return -1;
    }

    // Perform the conversion
    int converted_samples = swr_convert(swr_ctx,
                                        s16_data,           // Output buffer
                                        nb_samples,          // Number of output samples
                                        float_data,         // Input buffer
                                        nb_samples);         // Number of input samples

    if (converted_samples < 0) {
        fprintf(stderr, "Error during conversion.\n");
        swr_free(&swr_ctx);
        return -1;
    }

    // Clean up
    swr_free(&swr_ctx);
    return 0;
}

int WRTCAudioGain::convert_S16_to_Float(const uint8_t **s16_data, uint8_t **float_data, int nb_samples, int sample_rate, int channels) {
    struct SwrContext *swr_ctx = NULL;
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "Failed to allocate SwrContext.\n");
        return -1;
    }

    // configure the SwrContext    
    AVChannelLayout in_ch_layout, out_ch_layout;
    av_channel_layout_default(&in_ch_layout, channels);
    av_channel_layout_default(&out_ch_layout, channels);
    av_opt_set_chlayout(swr_ctx, "in_channel_layout", &in_ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_channel_layout", &out_ch_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", channels == 1 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S16P, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", channels == 1 ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", sample_rate, 0);

    if (swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Failed to initialize SwrContext.\n");
        return -1;
    }

    // Perform the conversion
    int converted_samples = swr_convert(swr_ctx,
                                        float_data,           // Output buffer
                                        nb_samples,          // Number of output samples
                                        s16_data,           // Input buffer
                                        nb_samples);         // Number of input samples

    if (converted_samples < 0) {
        fprintf(stderr, "Error during conversion.\n");
        swr_free(&swr_ctx);
        return -1;
    }

    // Clean up
    swr_free(&swr_ctx);
    return 0;
}

int WRTCAudioGain::processInplace(int64_t samples, uint8_t** buffers, [[maybe_unused]]int bufferSize, int sampleRate, int format, int channels) {
    int ret = 0;
    if ((channels == 1 && format == AV_SAMPLE_FMT_FLT) ||
        (channels == 2 && format == AV_SAMPLE_FMT_FLTP)) {
        if (_s16BufferedSamples < samples) {
            if (_s16Buffer) {
                 // Free the allocated memory
                av_freep(&_s16Buffer[0]);  // Free the individual channel buffers
                av_freep(&_s16Buffer);     // Free the array of pointers
            }
            ret = av_samples_alloc_array_and_samples(
                &_s16Buffer,        // Pointer to buffer array
                NULL,            // Linesize (not needed for interleaved)
                channels,           // Number of channels
                (int)samples,     // Number of samples
                channels == 1 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S16P,      // Sample format
                0               // Alignment
            );
            if (ret < 0) {
                fprintf(stderr, "Could not allocate aligned samples\n");
                return ret;
            }
            _s16BufferedSamples = (int)samples;
        }
        
        ret = convert_Float_to_S16((const uint8_t **)buffers, _s16Buffer, (int)samples, sampleRate, channels);
        if (ret < 0) {
            return -1;
        }

        for (int i=0; i<channels; i++) {
            //agc in place
            //  kAgcModeAdaptiveAnalog  模拟音量调节
            //  kAgcModeAdaptiveDigital 自适应增益
            //  kAgcModeFixedDigital 固定增益
            ret = agcProcess((int16_t*)_s16Buffer[i], sampleRate, (int)samples, kAgcModeAdaptiveDigital);
            if (ret < 0) {
                return -1;
            }
            
//            //zzy, test
//            if (i == 0) {
//                if (_dumpFile) {
//                    fwrite(_s16Buffer[i], sizeof(int16_t), samples, _dumpFile);
//                    //printf("dump %d samples, %d returned \n", (int)samples, ws);
//                }
//            }
        }

        ret = convert_S16_to_Float((const uint8_t **)_s16Buffer, buffers, (int)samples, sampleRate, channels);
        if (ret < 0) {
            return -1;
        }


    } else {
        std::cerr << "WRTCAudioGain::processInplace, input format not supported, format:" << format << ", channels:" << channels << std::endl;
        return -1;
    }
    
    return 0;
}



}

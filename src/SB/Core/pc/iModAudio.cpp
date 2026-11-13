// PC-only: replacement sounds to 16-bit PCM. See iModAudio.h.
#include "iModAudio.h"

#include <stdio.h>
#include <string.h>

#ifdef TSSM_HAVE_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace
{
    U32 Le32(const U8* p)
    {
        return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
    }

    U16 Le16(const U8* p)
    {
        return (U16)(p[0] | (p[1] << 8));
    }

    void PutS16(std::vector<U8>& out, S32 v)
    {
        if (v > 32767)
        {
            v = 32767;
        }
        if (v < -32768)
        {
            v = -32768;
        }
        out.push_back((U8)(v & 0xFF));
        out.push_back((U8)((v >> 8) & 0xFF));
    }

    bool ReadFile(const char* path, std::vector<U8>* data)
    {
        FILE* fp = fopen(path, "rb");
        if (fp == NULL)
        {
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (len < 0)
        {
            fclose(fp);
            return false;
        }
        data->resize((size_t)len);
        bool ok = len == 0 || fread(&(*data)[0], 1, (size_t)len, fp) == (size_t)len;
        fclose(fp);
        return ok;
    }

    // One sample of a WAV frame as a 32-bit int scaled to 16 bits.
    S32 WavSample(const U8* p, U16 format, U16 bits)
    {
        if (format == 3) // IEEE float
        {
            if (bits == 32)
            {
                U32 u = Le32(p);
                float f;
                memcpy(&f, &u, sizeof(f));
                return (S32)(f * 32767.0f);
            }
            return 0;
        }
        switch (bits)
        {
        case 8:
            return ((S32)p[0] - 128) << 8;
        case 16:
            return (S16)Le16(p);
        case 24:
            return (S32)(((U32)p[0] << 8) | ((U32)p[1] << 16) | ((U32)p[2] << 24)) >> 16;
        case 32:
            return (S32)Le32(p) >> 16;
        }
        return 0;
    }

    // RIFF WAVE: 0 when it is not one, -1 when it is one this cannot read, 1 ok.
    int DecodeWav(const std::vector<U8>& d, iModPcm* out, std::string* error)
    {
        if (d.size() < 12 || memcmp(&d[0], "RIFF", 4) != 0 || memcmp(&d[8], "WAVE", 4) != 0)
        {
            return 0;
        }

        U16 format = 0, channels = 0, bits = 0, block = 0;
        U32 rate = 0;
        const U8* data = NULL;
        U32 dataLen = 0;

        size_t pos = 12;
        while (pos + 8 <= d.size())
        {
            const U8* c = &d[pos];
            U32 len = Le32(c + 4);
            size_t body = pos + 8;
            if (len > d.size() - body)
            {
                len = (U32)(d.size() - body); // a truncated last chunk, as writers leave them
            }
            if (memcmp(c, "fmt ", 4) == 0 && len >= 16)
            {
                format = Le16(&d[body]);
                channels = Le16(&d[body + 2]);
                rate = Le32(&d[body + 4]);
                block = Le16(&d[body + 12]);
                bits = Le16(&d[body + 14]);
                if (format == 0xFFFE && len >= 26)
                {
                    format = Le16(&d[body + 24]); // WAVE_FORMAT_EXTENSIBLE: the subformat
                }
            }
            else if (memcmp(c, "data", 4) == 0)
            {
                data = &d[body];
                dataLen = len;
            }
            pos = body + len + (len & 1);
        }

        if (data == NULL || channels == 0 || rate == 0 || block == 0)
        {
            *error = "a WAV without a fmt or data chunk";
            return -1;
        }
        if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) &&
            !(format == 3 && bits == 32))
        {
            char why[96];
            snprintf(why, sizeof(why), "WAV format %u at %u bits (PCM or float WAV only)",
                     (unsigned)format, (unsigned)bits);
            *error = why;
            return -1;
        }

        const U32 bytesPer = bits / 8;
        const U32 frames = dataLen / block;
        const U16 outCh = channels >= 2 ? 2 : 1;
        out->channels = outCh;
        out->rate = rate;
        out->bytes.clear();
        out->bytes.reserve((size_t)frames * outCh * 2);
        for (U32 f = 0; f < frames; f++)
        {
            const U8* frame = data + (size_t)f * block;
            if (channels <= 2)
            {
                for (U16 ch = 0; ch < channels; ch++)
                {
                    PutS16(out->bytes, WavSample(frame + ch * bytesPer, format, bits));
                }
            }
            else
            {
                // Odd channels left, even right: front pair, then the rest.
                S32 l = 0, r = 0;
                for (U16 ch = 0; ch < channels; ch++)
                {
                    S32 s = WavSample(frame + ch * bytesPer, format, bits);
                    (ch & 1 ? r : l) += s;
                }
                PutS16(out->bytes, l / ((channels + 1) / 2));
                PutS16(out->bytes, r / (channels / 2));
            }
        }
        return 1;
    }

#ifdef TSSM_HAVE_FFMPEG
    bool DecodeFFmpeg(const char* path, iModPcm* out, std::string* error)
    {
        AVFormatContext* fmt = NULL;
        if (avformat_open_input(&fmt, path, NULL, NULL) < 0)
        {
            *error = "FFmpeg cannot open it";
            return false;
        }
        if (avformat_find_stream_info(fmt, NULL) < 0)
        {
            avformat_close_input(&fmt);
            *error = "FFmpeg finds no streams in it";
            return false;
        }
        int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        if (stream < 0)
        {
            avformat_close_input(&fmt);
            *error = "it has no audio stream";
            return false;
        }
        const AVCodec* codec = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
        AVCodecContext* dec = codec != NULL ? avcodec_alloc_context3(codec) : NULL;
        if (dec == NULL ||
            avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar) < 0 ||
            avcodec_open2(dec, codec, NULL) < 0)
        {
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            *error = "FFmpeg has no decoder for its audio";
            return false;
        }

#if LIBAVUTIL_VERSION_MAJOR >= 57
        int inChannels = dec->ch_layout.nb_channels;
#else
        int inChannels = dec->channels;
#endif
        const int outChannels = inChannels >= 2 ? 2 : 1;
        SwrContext* swr = NULL;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, outChannels);
        if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, dec->sample_rate,
                                &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0,
                                NULL) < 0)
        {
            swr = NULL;
        }
        av_channel_layout_uninit(&outLayout);
#else
        int64_t inLayout = dec->channel_layout ? (int64_t)dec->channel_layout
                                               : av_get_default_channel_layout(inChannels);
        swr = swr_alloc_set_opts(NULL, av_get_default_channel_layout(outChannels),
                                 AV_SAMPLE_FMT_S16, dec->sample_rate, inLayout, dec->sample_fmt,
                                 dec->sample_rate, 0, NULL);
#endif
        if (swr == NULL || swr_init(swr) < 0)
        {
            swr_free(&swr);
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            *error = "FFmpeg cannot convert its samples";
            return false;
        }

        out->channels = (U16)outChannels;
        out->rate = (U32)dec->sample_rate;
        out->bytes.clear();

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        auto drain = [&]() {
            while (avcodec_receive_frame(dec, frame) == 0)
            {
                int cap = swr_get_out_samples(swr, frame->nb_samples);
                if (cap > 0)
                {
                    size_t at = out->bytes.size();
                    out->bytes.resize(at + (size_t)cap * outChannels * 2);
                    U8* dst = &out->bytes[at];
                    int got = swr_convert(swr, &dst, cap, (const U8**)frame->extended_data,
                                          frame->nb_samples);
                    out->bytes.resize(at + (size_t)(got > 0 ? got : 0) * outChannels * 2);
                }
                av_frame_unref(frame);
            }
        };
        while (av_read_frame(fmt, pkt) >= 0)
        {
            if (pkt->stream_index == stream && avcodec_send_packet(dec, pkt) == 0)
            {
                drain();
            }
            av_packet_unref(pkt);
        }
        avcodec_send_packet(dec, NULL);
        drain();

        // What the resampler still holds.
        for (;;)
        {
            int cap = 4096;
            size_t at = out->bytes.size();
            out->bytes.resize(at + (size_t)cap * outChannels * 2);
            U8* dst = &out->bytes[at];
            int got = swr_convert(swr, &dst, cap, NULL, 0);
            out->bytes.resize(at + (size_t)(got > 0 ? got : 0) * outChannels * 2);
            if (got <= 0)
            {
                break;
            }
        }

        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);

        if (out->bytes.empty())
        {
            *error = "it decoded to no samples";
            return false;
        }
        return true;
    }
#endif
} // namespace

bool iModDecodeAudio(const char* path, iModPcm* out, std::string* error)
{
    std::vector<U8> data;
    if (!ReadFile(path, &data))
    {
        *error = "cannot be read";
        return false;
    }

    std::string wavError;
    int wav = DecodeWav(data, out, &wavError);
    if (wav > 0)
    {
        if (out->bytes.empty())
        {
            *error = "the WAV has no samples";
            return false;
        }
        return true;
    }

#ifdef TSSM_HAVE_FFMPEG
    // Also a WAV this reader refused (ADPCM, A-law): FFmpeg reads those.
    return DecodeFFmpeg(path, out, error);
#else
    *error = wav < 0 ? wavError : "not a WAV, and this build has no FFmpeg for other formats";
    return false;
#endif
}

const char* iModAudioDecoders()
{
#ifdef TSSM_HAVE_FFMPEG
    return "WAV + FFmpeg";
#else
    return "WAV";
#endif
}

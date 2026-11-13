#ifndef FMOD_H
#define FMOD_H

// FMOD 3.7x (GameCube) public API, as linked into the game (C linkage).
// Only the parts used by the game code are declared here.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FSOUND_SAMPLE FSOUND_SAMPLE;
typedef struct FSOUND_STREAM FSOUND_STREAM;
typedef struct FSOUND_DSPUNIT FSOUND_DSPUNIT;
typedef struct FMUSIC_MODULE FMUSIC_MODULE;

// DSP-ADPCM decoder state of a GameCube sample. Size: 0x2E
typedef struct FSOUND_GCADPCMINFO
{
    signed short coef[16];
    unsigned short gain;
    unsigned short pred_scale;
    signed short yn1;
    signed short yn2;
    unsigned short loop_pred_scale;
    signed short loop_yn1;
    signed short loop_yn2;
} FSOUND_GCADPCMINFO;

typedef signed char (*FSOUND_STREAMCALLBACK)(FSOUND_STREAM* stream, void* buff, int len,
                                             void* userdata);

#define FSOUND_FREE -1
#define FSOUND_UNMANAGED -2
#define FSOUND_ALL -3

#define FSOUND_LOOP_OFF 0x00000001
#define FSOUND_LOOP_NORMAL 0x00000002
#define FSOUND_LOOP_BIDI 0x00000004
#define FSOUND_MONO 0x00000020
#define FSOUND_HW3D 0x00001000
#define FSOUND_HW2D 0x00080000
#define FSOUND_NONBLOCKING 0x01000000
#define FSOUND_GCADPCM 0x02000000

#define FSOUND_INIT_DONTLATENCYADJUST 0x0080
#define FSOUND_INIT_GC_INITLIBS 0x0100

#define FMOD_VERSION 3.73f

enum FSOUND_MIXERTYPES
{
    FSOUND_MIXER_AUTODETECT,
    FSOUND_MIXER_BLENDMODE,
    FSOUND_MIXER_MMXP5,
    FSOUND_MIXER_MMXP6,
    FSOUND_MIXER_QUALITY_AUTODETECT,
    FSOUND_MIXER_QUALITY_FPU,
    FSOUND_MIXER_QUALITY_MMXP5,
    FSOUND_MIXER_QUALITY_MMXP6,
    FSOUND_MIXER_MONO,
    FSOUND_MIXER_QUALITY_MONO,
    FSOUND_MIXER_MAX
};

enum FSOUND_SPEAKERMODES
{
    FSOUND_SPEAKERMODE_DOLBYDIGITAL,
    FSOUND_SPEAKERMODE_HEADPHONES,
    FSOUND_SPEAKERMODE_MONO,
    FSOUND_SPEAKERMODE_QUAD,
    FSOUND_SPEAKERMODE_STEREO,
    FSOUND_SPEAKERMODE_SURROUND,
    FSOUND_SPEAKERMODE_DTS,
    FSOUND_SPEAKERMODE_PROLOGIC2,
    FSOUND_SPEAKERMODE_PROLOGIC2_INTERIOR
};

float FSOUND_GetVersion();
signed char FSOUND_SetMixer(int mixer);
signed char FSOUND_SetMaxHardwareChannels(int max);
signed char FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags);
void FSOUND_Close();
void FSOUND_Update();
void FSOUND_GetMemoryStats(unsigned int* currentalloced, unsigned int* maxalloced);
void FSOUND_3D_SetRolloffFactor(float rolloff);
void FSOUND_3D_Listener_SetAttributes(const float* pos, const float* vel, float fx, float fy,
                                      float fz, float tx, float ty, float tz);
void FSOUND_SetSpeakerMode(unsigned int speakermode);

signed char FSOUND_StopSound(int channel);
signed char FSOUND_SetFrequency(int channel, int freq);
signed char FSOUND_SetVolume(int channel, int vol);
signed char FSOUND_SetPaused(int channel, signed char paused);
signed char FSOUND_SetPriority(int channel, int priority);
signed char FSOUND_IsPlaying(int channel);
signed char FSOUND_GetPaused(int channel);
unsigned int FSOUND_GetLoopMode(int channel);
FSOUND_SAMPLE* FSOUND_GetCurrentSample(int channel);
int FSOUND_GetNumSubChannels(int channel);
int FSOUND_GetSubChannel(int channel, int subchannel);
signed char FSOUND_3D_SetAttributes(int channel, const float* pos, const float* vel);
signed char FSOUND_3D_SetMinMaxDistance(int channel, float min, float max);

signed char FSOUND_Sample_SetMode(FSOUND_SAMPLE* sptr, unsigned int mode);
int FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE* sptr, FSOUND_DSPUNIT* dsp,
                       signed char startpaused);
signed char FSOUND_Sample_GetDefaults(FSOUND_SAMPLE* sptr, int* deffreq, int* defvol, int* defpan,
                                      int* defpri);

FSOUND_STREAM* FSOUND_Stream_Open(const char* name_or_data, unsigned int mode, int offset,
                                  int length);
FSOUND_STREAM* FSOUND_Stream_Create(FSOUND_STREAMCALLBACK callback, int length, unsigned int mode,
                                    int samplerate, void* userdata);
FSOUND_SAMPLE* FSOUND_Stream_GetSample(FSOUND_STREAM* stream);
int FSOUND_Stream_PlayEx(int channel, FSOUND_STREAM* stream, FSOUND_DSPUNIT* dsp,
                         signed char startpaused);
signed char FSOUND_Stream_Close(FSOUND_STREAM* stream);
int FSOUND_Stream_GetOpenState(FSOUND_STREAM* stream);
signed char FSOUND_Stream_SetBufferSize(int ms);

FMUSIC_MODULE* FMUSIC_LoadSongEx(const char* name_or_data, int offset, int length,
                                 unsigned int mode, const int* samplelist, int samplelistnum);
int FMUSIC_GetOpenState(FMUSIC_MODULE* mod);
signed char FMUSIC_FreeSong(FMUSIC_MODULE* mod);
FSOUND_SAMPLE* FMUSIC_GetSample(FMUSIC_MODULE* mod, int sampno);

// GameCube specific (fmodgc.h); parameter meanings not known yet
signed char FSOUND_SetAramBlock(unsigned int address, unsigned int size);
signed char FSOUND_AUXFX_SetReverbHiDpl2(float, float, float, float, float);
signed char FSOUND_AUXFX_SetReverbHi(float, float, float, float, float, float, int);
signed char FSOUND_AUXFX_SetLevels(int channel, int, int);
signed char FSOUND_AUXFX_ShutdownEffect(int);
signed char FSOUND_GC_Stream_SetGCADPCMLoopContext(FSOUND_STREAM* stream);
signed char FSOUND_GC_Sample_SetGCADPCMInfo(FSOUND_SAMPLE* sptr, FSOUND_GCADPCMINFO* info);
void FSOUND_GC_SetDvdStatusCallback(void (*callback)(int status));
void FSOUND_GC_SetUpdateDvdStatusCallback(void (*callback)(int status));

#ifdef __cplusplus
}
#endif

#endif

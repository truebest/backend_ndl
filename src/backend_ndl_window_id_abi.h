/* SPDX-License-Identifier: MIT */
#ifndef BACKEND_NDL_WINDOW_ID_ABI_H
#define BACKEND_NDL_WINDOW_ID_ABI_H

/* Minimal, independently maintained description of the window-id DirectMedia
 * ABI used by backend_ndl. It is private to the wrapper: no NDK declaration
 * is copied into the public API or linked into the product. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT static_assert
#else
#define BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT _Static_assert
#endif

typedef void (*StateCallbackFP)(const int type, const long long numValue,
                                const char *strValue);
typedef void (*MultiChannelPCMCallbackFP)(const int isSupported, const char *strValue);

typedef enum NDL_DIRECTVIDEO_SRC_TYPE_ {
    NDL_DIRECTVIDEO_SRC_TYPE_NONE = 0,
    NDL_DIRECTVIDEO_SRC_TYPE_H264,
    NDL_DIRECTVIDEO_SRC_TYPE_HEVC,
    NDL_DIRECTVIDEO_SRC_TYPE_VP9,
    NDL_DIRECTVIDEO_SRC_TYPE_AV1,
} NDL_DIRECTVIDEO_SRC_TYPE;

typedef struct NDL_DIRECTVIDEO_DATA_INFO {
    int width;
    int height;
    NDL_DIRECTVIDEO_SRC_TYPE source;
} NDL_DIRECTVIDEO_DATA_INFO_T;

typedef enum NDL_DIRECTAUDIO_SRC_TYPE_ {
    NDL_DIRECTAUDIO_SRC_TYPE_NONE = 0,
    NDL_DIRECTAUDIO_SRC_TYPE_PCM,
    NDL_DIRECTAUDIO_SRC_TYPE_MP3,
    NDL_DIRECTAUDIO_SRC_TYPE_OPUS,
} NDL_DIRECTAUDIO_SRC_TYPE;

typedef enum NDL_DIRECTAUDIO_SAMPLERATE {
    NDL_DIRECTAUDIO_SAMPLERATE_BYPASS = 0,
    NDL_DIRECTAUDIO_SAMPLERATE_48K,
    NDL_DIRECTAUDIO_SAMPLERATE_44_1K,
    NDL_DIRECTAUDIO_SAMPLERATE_32K,
    NDL_DIRECTAUDIO_SAMPLERATE_24K,
    NDL_DIRECTAUDIO_SAMPLERATE_16K,
    NDL_DIRECTAUDIO_SAMPLERATE_12K,
    NDL_DIRECTAUDIO_SAMPLERATE_8K,
    NDL_DIRECTAUDIO_SAMPLERATE_22_05K,
    NDL_DIRECTAUDIO_SAMPLERATE_MAX,
} NDL_DIRECTAUDIO_SAMPLERATE_T;

typedef struct NDL_DIRECTAUDIO_PCM_INFO_ {
    char *format;
    char *layout;
    char *channelMode;
    NDL_DIRECTAUDIO_SAMPLERATE_T sampleRate;
} NDL_DIRECTAUDIO_PCM_INFO_T;

typedef struct NDL_DIRECTAUDIO_OPUS_INFO_ {
    int channels;
    double sampleRate;
    char *streamHeader;
} NDL_DIRECTAUDIO_OPUS_INFO_T;

typedef struct NDL_DIRECTAUDIO_DATA_INFO {
    NDL_DIRECTAUDIO_SRC_TYPE audioCodec;
    union {
        NDL_DIRECTAUDIO_PCM_INFO_T pcmInfo;
        NDL_DIRECTAUDIO_OPUS_INFO_T opusInfo;
    } audio_data;
} NDL_DIRECTAUDIO_DATA_INFO_T;

typedef struct NDL_DIRECTMEDIA_DATA_INFO {
    NDL_DIRECTVIDEO_DATA_INFO_T videoDataInfo;
    NDL_DIRECTAUDIO_DATA_INFO_T audioDataInfo;
} NDL_DIRECTMEDIA_DATA_INFO_T;

typedef struct NDL_DIRECTVIDEO_HDR_INFO {
    unsigned int displayPrimariesGx;
    unsigned int displayPrimariesGy;
    unsigned int displayPrimariesBx;
    unsigned int displayPrimariesBy;
    unsigned int displayPrimariesRx;
    unsigned int displayPrimariesRy;
    unsigned int whitePointX;
    unsigned int whitePointY;
    unsigned int maxDisplayMasteringLuminance;
    unsigned int minDisplayMasteringLuminance;
    unsigned int maxContentLightLevel;
    unsigned int maxFrameAverageLightLevel;
    unsigned int transferCharacteristics;
    unsigned int colorPrimaries;
    unsigned int matrixCoeffs;
} NDL_DIRECTVIDEO_HDR_INFO_T;

typedef enum NDL_DIRECTMEDIA_APP_STATE_ {
    NDL_DIRECTMEDIA_APP_STATE_FOREGROUND = 0,
    NDL_DIRECTMEDIA_APP_STATE_BACKGROUND,
} NDL_DIRECTMEDIA_APP_STATE;

const char *NDL_DirectMediaGetError(void);
int NDL_DirectMediaSetWindowId(const char *window_id);
int NDL_DirectMediaInit(const char *app_id);
int NDL_DirectMediaQuit(void);
int NDL_DirectMediaSetAppState(NDL_DIRECTMEDIA_APP_STATE state);
int NDL_DirectMediaLoad(NDL_DIRECTMEDIA_DATA_INFO_T *media_info,
                        StateCallbackFP media_state_callback);
int NDL_DirectMediaUnload(void);
int NDL_DirectAudioPlay(void *buffer, unsigned int size, long long pts);
int NDL_DirectAudioGetTotalBufferSize(int *total);
int NDL_DirectAudioGetAvailableBufferSize(int *available);
int NDL_DirectAudioSupportMultiChannel(int *is_supported);
int NDL_DirectAudioRegisterCallback(MultiChannelPCMCallbackFP callback);
int NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts);
int NDL_DirectVideoSetArea(int left, int top, int width, int height);
int NDL_DirectVideoSetHDRInfo(NDL_DIRECTVIDEO_HDR_INFO_T info);
int NDL_DirectVideoSetFrameDropThreshold(int threshold);
int NDL_DirectVideoGetRenderBufferLength(int *buffer_length);
int NDL_DirectVideoFlushRenderBuffer(void);
int NDL_DirectEffectLoad(NDL_DIRECTAUDIO_PCM_INFO_T *pcm_info,
                         unsigned int *preferred_size);
int NDL_DirectEffectUnload(void);
int NDL_DirectEffectPlay(void *buffer, unsigned int size);
int NDL_DirectEffectGetAvailableBufferSize(unsigned int *available);

BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTVIDEO_SRC_TYPE_NONE == 0,
                                  "DirectMedia video NONE value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTVIDEO_SRC_TYPE_H264 == 1,
                                  "DirectMedia H.264 value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTVIDEO_SRC_TYPE_HEVC == 2,
                                  "DirectMedia HEVC value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTVIDEO_SRC_TYPE_VP9 == 3,
                                  "DirectMedia VP9 value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTVIDEO_SRC_TYPE_AV1 == 4,
                                  "DirectMedia AV1 value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTAUDIO_SRC_TYPE_NONE == 0,
                                  "DirectMedia audio NONE value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTAUDIO_SRC_TYPE_PCM == 1,
                                  "DirectMedia PCM value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTAUDIO_SAMPLERATE_48K == 1,
                                  "DirectMedia 48 kHz value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTAUDIO_SAMPLERATE_44_1K == 2,
                                  "DirectMedia 44.1 kHz value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTAUDIO_SAMPLERATE_22_05K == 8,
                                  "DirectMedia 22.05 kHz value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTMEDIA_APP_STATE_FOREGROUND == 0,
                                  "DirectMedia foreground value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(NDL_DIRECTMEDIA_APP_STATE_BACKGROUND == 1,
                                  "DirectMedia background value");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(sizeof(NDL_DIRECTVIDEO_HDR_INFO_T) == 60,
                                  "DirectMedia HDR ABI size");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTVIDEO_HDR_INFO_T, matrixCoeffs) == 56,
                                  "DirectMedia HDR ABI tail");

#if UINTPTR_MAX == UINT32_MAX
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(sizeof(NDL_DIRECTVIDEO_DATA_INFO_T) == 12,
                                  "DirectMedia video ABI size");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTVIDEO_DATA_INFO_T, width) == 0,
                                  "DirectMedia video width offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTVIDEO_DATA_INFO_T, height) == 4,
                                  "DirectMedia video height offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTVIDEO_DATA_INFO_T, source) == 8,
                                  "DirectMedia video source offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(sizeof(NDL_DIRECTAUDIO_PCM_INFO_T) == 16,
                                  "DirectEffect PCM ABI size");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_OPUS_INFO_T, channels) == 0,
                                  "DirectMedia Opus channels offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_OPUS_INFO_T, sampleRate) == 8,
                                  "DirectMedia Opus sample rate offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_OPUS_INFO_T, streamHeader) == 16,
                                  "DirectMedia Opus stream header offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_PCM_INFO_T, format) == 0,
                                  "DirectEffect PCM format offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_PCM_INFO_T, layout) == 4,
                                  "DirectEffect PCM layout offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_PCM_INFO_T, channelMode) == 8,
                                  "DirectEffect PCM channel offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(offsetof(NDL_DIRECTAUDIO_PCM_INFO_T, sampleRate) == 12,
                                  "DirectEffect PCM rate offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(sizeof(NDL_DIRECTMEDIA_DATA_INFO_T) == 48,
                                  "DirectMedia combined ABI size");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(
    offsetof(NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo.audioCodec) == 16,
    "DirectMedia audio codec offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(
    offsetof(NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo.audio_data.pcmInfo.format) == 24,
    "DirectMedia PCM format offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(
    offsetof(NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo.audio_data.pcmInfo.layout) == 28,
    "DirectMedia PCM layout offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(
    offsetof(NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo.audio_data.pcmInfo.channelMode) == 32,
    "DirectMedia PCM channel offset");
BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT(
    offsetof(NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo.audio_data.pcmInfo.sampleRate) == 36,
    "DirectMedia PCM rate offset");
#endif

#undef BACKEND_NDL_DIRECTMEDIA_STATIC_ASSERT

#ifdef __cplusplus
}
#endif

#endif

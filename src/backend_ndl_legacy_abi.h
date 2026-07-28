/* SPDX-License-Identifier: MIT */
#ifndef BACKEND_NDL_LEGACY_ABI_H
#define BACKEND_NDL_LEGACY_ABI_H

/* Minimal, private description of the callback-init DirectMedia v2 ABI.
 * Names are deliberately local to backend_ndl: no SDK/community header is
 * copied or installed, and no legacy type leaks through the public API. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define BACKEND_NDL_LEGACY_STATIC_ASSERT static_assert
#else
#define BACKEND_NDL_LEGACY_STATIC_ASSERT _Static_assert
#endif

typedef void (*BackendNdlLegacyResourceReleasedFn)(const char *type);
typedef void (*BackendNdlLegacyStateCallbackFn)(int type, long long number,
                                                const char *text);

typedef struct BackendNdlLegacyVideoInfo {
    int width;
    int height;
    int type;
    int reserved;
} BackendNdlLegacyVideoInfo;

typedef struct BackendNdlLegacyPcmInfo {
    int type;
    int reserved;
    const char *format;
    const char *layout;
    const char *channel_mode;
    int sample_rate;
} BackendNdlLegacyPcmInfo;

typedef struct BackendNdlLegacyOpusInfo {
    int type;
    int reserved;
    int channels;
    int reserved2;
    double sample_rate;
    const char *stream_header;
} BackendNdlLegacyOpusInfo;

typedef struct BackendNdlLegacyMediaInfo {
    BackendNdlLegacyVideoInfo video;
    union {
        int type;
        BackendNdlLegacyPcmInfo pcm;
        BackendNdlLegacyOpusInfo opus;
        unsigned char padding[32];
    } audio;
} BackendNdlLegacyMediaInfo;

typedef int (*BackendNdlLegacyInitFn)(const char *app_id,
                                     BackendNdlLegacyResourceReleasedFn callback);
typedef int (*BackendNdlLegacyLoadFn)(BackendNdlLegacyMediaInfo *info,
                                     BackendNdlLegacyStateCallbackFn callback);

enum {
    BACKEND_NDL_LEGACY_VIDEO_H264 = 1,
    BACKEND_NDL_LEGACY_AUDIO_PCM = 1,
    BACKEND_NDL_LEGACY_AUDIO_OPUS = 3,
    BACKEND_NDL_LEGACY_PCM_48K = 1,
    BACKEND_NDL_LEGACY_PCM_44_1K = 2,
    BACKEND_NDL_LEGACY_PCM_32K = 3,
    BACKEND_NDL_LEGACY_PCM_24K = 4,
    BACKEND_NDL_LEGACY_PCM_22_05K = 5,
    BACKEND_NDL_LEGACY_PCM_16K = 6,
    BACKEND_NDL_LEGACY_PCM_12K = 7,
    BACKEND_NDL_LEGACY_PCM_8K = 8,
};

#if UINTPTR_MAX == UINT32_MAX
BACKEND_NDL_LEGACY_STATIC_ASSERT(sizeof(BackendNdlLegacyVideoInfo) == 16,
                                 "legacy DirectMedia video ABI size");
BACKEND_NDL_LEGACY_STATIC_ASSERT(sizeof(BackendNdlLegacyPcmInfo) == 24,
                                 "legacy DirectMedia PCM ABI size");
BACKEND_NDL_LEGACY_STATIC_ASSERT(sizeof(BackendNdlLegacyMediaInfo) == 48,
                                 "legacy DirectMedia combined ABI size");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, video.width) == 0,
                                 "legacy DirectMedia video width offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, video.height) == 4,
                                 "legacy DirectMedia video height offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, video.type) == 8,
                                 "legacy DirectMedia video type offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, audio.type) == 16,
                                 "legacy DirectMedia audio type offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, audio.pcm.format) == 24,
                                 "legacy DirectMedia PCM format offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(offsetof(BackendNdlLegacyMediaInfo, audio.pcm.layout) == 28,
                                 "legacy DirectMedia PCM layout offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(
    offsetof(BackendNdlLegacyMediaInfo, audio.pcm.channel_mode) == 32,
    "legacy DirectMedia PCM channel offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(
    offsetof(BackendNdlLegacyMediaInfo, audio.pcm.sample_rate) == 36,
    "legacy DirectMedia PCM rate offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(
    offsetof(BackendNdlLegacyMediaInfo, audio.opus.channels) == 24,
    "legacy DirectMedia Opus channels offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(
    offsetof(BackendNdlLegacyMediaInfo, audio.opus.sample_rate) == 32,
    "legacy DirectMedia Opus sample rate offset");
BACKEND_NDL_LEGACY_STATIC_ASSERT(
    offsetof(BackendNdlLegacyMediaInfo, audio.opus.stream_header) == 40,
    "legacy DirectMedia Opus stream header offset");
#endif

#undef BACKEND_NDL_LEGACY_STATIC_ASSERT

#endif

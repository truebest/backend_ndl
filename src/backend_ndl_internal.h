/* SPDX-License-Identifier: MIT */
#ifndef BACKEND_NDL_INTERNAL_H
#define BACKEND_NDL_INTERNAL_H

#include "backend_ndl_api.h"

#include <pthread.h>
#include <stdatomic.h>

#if ATOMIC_LLONG_LOCK_FREE != 2
#error "backend_ndl requires lock-free 64-bit atomics for mutex-free feeds"
#endif

#define BACKEND_NDL_FEED_VIDEO_READY UINT64_C(1)
#define BACKEND_NDL_FEED_AUDIO_READY UINT64_C(2)
#define BACKEND_NDL_FEED_NEED_KEYFRAME UINT64_C(4)
#define BACKEND_NDL_FEED_FLAG_MASK UINT64_C(7)
#define BACKEND_NDL_FEED_GENERATION_SHIFT 3

typedef struct BackendNdlMediaConfig {
    bool has_video;
    BackendNdlVideoInfo video;
    bool has_audio;
    BackendNdlAudioInfo audio;
    bool has_video_hdr;
    BackendNdlHdrInfo video_hdr;
} BackendNdlMediaConfig;

struct BackendNdl {
    /* Control plane only. FeedAudio/FeedVideo must never acquire this mutex. */
    pthread_mutex_t lifecycle_lock;
    BackendNdlConfig config;

    char *app_id;
    char *window_id;
    char *webos_sdk_version;
    unsigned int webos_sdk_major;

    void *dl_handle;
    bool owns_dl_handle;
    BackendNdlApi api;
    BackendNdlAbiProfile abi_profile;
    BackendNdlCapabilities capabilities;

    bool ndl_initialized;
    bool media_loaded;
    uint64_t reload_generation;

    BackendNdlMediaConfig media;

    bool has_video_area;
    int video_left;
    int video_top;
    int video_width;
    int video_height;

    bool effect_loaded;
    BackendNdlPcmInfo effect_pcm;

    _Atomic unsigned long long feed_state;
    _Atomic long long loaded_start_ms;
    _Atomic unsigned long long audio_feed_count;
    _Atomic unsigned long long video_feed_count;
    _Atomic unsigned int audio_overflow_streak;
    _Atomic unsigned int audio_not_ready_streak;
    _Atomic unsigned int audio_error_streak;
    _Atomic unsigned int video_error_streak;
    /* Runtime-stat fields pack a load-epoch tag with each metric. EWMA also
     * carries an initialized bit so zero and the full uint32_t duration range
     * stay representable. Writers from an older load can therefore finish
     * without delaying reset or contaminating the new load. */
    _Atomic unsigned long long video_play_samples;
    _Atomic unsigned long long video_play_us_ewma;
    _Atomic unsigned long long video_play_us_max;
    _Atomic unsigned long long video_play_errors;
    /* A successful media load publishes a new tagged epoch without waiting for
     * feed threads. The writer count and completion version make snapshots
     * coherent while allowing multiple feeds to record concurrently. */
    _Atomic unsigned long long video_stats_epoch;
    _Atomic unsigned long long video_stats_writers;
    _Atomic unsigned long long video_stats_version;
    _Atomic int multichannel_status;
};

typedef union BackendNdlMediaInfo {
    NDL_DIRECTMEDIA_DATA_INFO_T current;
    BackendNdlLegacyMediaInfo callback;
} BackendNdlMediaInfo;

extern pthread_mutex_t g_backend_ndl_registry_lock;
extern BackendNdl *g_backend_ndl_active;

void backend_ndl_log(BackendNdl *ctx, BackendNdlLogLevel level, const char *format, ...);
bool backend_ndl_log_enabled(const BackendNdl *ctx, BackendNdlLogLevel level);
const char *backend_ndl_last_error(const BackendNdl *ctx);
long long backend_ndl_monotonic_ms(void);
long long backend_ndl_monotonic_us(void);
long long backend_ndl_media_time_for_state(const BackendNdl *ctx, uint64_t state);
uint64_t backend_ndl_feed_generation(uint64_t state);
BackendNdlResult backend_ndl_opt_get_int(BackendNdl *ctx, int (*fn)(int *), int *out);

#define BACKEND_NDL_LOG(ctx_, level_, ...) backend_ndl_log((ctx_), (level_), __VA_ARGS__)
#define BACKEND_NDL_DEBUG_LOG(ctx_, ...) \
    BACKEND_NDL_LOG((ctx_), BACKEND_NDL_LOG_DEBUG, __VA_ARGS__)

#define BACKEND_NDL_AUDIO_DEBUG_CADENCE 512
#define BACKEND_NDL_VIDEO_DEBUG_CADENCE 300

const char *backend_ndl_pcm_channel_mode(uint16_t channels);
int backend_ndl_pcm_sample_rate(BackendNdlAbiProfile profile, uint32_t hz);
BackendNdlResult backend_ndl_fill_pcm_info(const BackendNdlPcmInfo *pcm,
                                           NDL_DIRECTAUDIO_PCM_INFO_T *out);
BackendNdlResult backend_ndl_audio_fill_media_info(BackendNdlAbiProfile profile,
                                                   const BackendNdlAudioInfo *audio,
                                                   BackendNdlMediaInfo *out);
BackendNdlResult backend_ndl_video_fill_media_info(BackendNdlAbiProfile profile,
                                                   const BackendNdlVideoInfo *video,
                                                   BackendNdlMediaInfo *out);
BackendNdlResult backend_ndl_audio_after_load_locked(BackendNdl *ctx);
void backend_ndl_video_before_load_locked(BackendNdl *ctx);
void backend_ndl_video_after_load_locked(BackendNdl *ctx);
BackendNdlResult backend_ndl_effect_unload_locked(BackendNdl *ctx);
void backend_ndl_video_reset_runtime_stats(BackendNdl *ctx);
void backend_ndl_audio_register_capability_callback(BackendNdl *ctx);
BackendNdlMultichannelStatus backend_ndl_multichannel_from_query(int value);
BackendNdlMultichannelStatus backend_ndl_multichannel_from_callback(int value);

#endif

/* SPDX-License-Identifier: MIT */
#ifndef BACKEND_NDL_BACKEND_NDL_H
#define BACKEND_NDL_BACKEND_NDL_H

/*
 * C11 wrapper around the process-global webOS NDL DirectMedia facade.
 *
 * The public interface contains no webOS SDK types. The implementation
 * resolves libNDL_directmedia.so.1 at runtime; that facade owns loading its
 * private implementation and transitive player libraries.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BackendNdl BackendNdl;

/* DEFAULT preserves firmware behavior. Explicit profiles require the
 * optional GStreamer module and are part of the media load configuration. */
typedef enum BackendNdlAppType {
    BACKEND_NDL_APP_DEFAULT = 0,
    BACKEND_NDL_APP_RTC,
    BACKEND_NDL_APP_MIRACAST,
    BACKEND_NDL_APP_ATSC3,
    BACKEND_NDL_APP_CAMERA,
    BACKEND_NDL_APP_JP4K,
    BACKEND_NDL_APP_AIRPLAY,
    BACKEND_NDL_APP_DIRECTMEDIA,
    BACKEND_NDL_APP_APPCAST,
    BACKEND_NDL_APP_R2R,
    BACKEND_NDL_APP_SYE
} BackendNdlAppType;

enum BackendNdlVideoFlags {
    BACKEND_NDL_VIDEO_DECODE_ONLY = 1u << 0,
    BACKEND_NDL_VIDEO_CONFIG_ONLY = 1u << 1
};

typedef enum BackendNdlVideoPathState {
    BACKEND_NDL_VIDEO_PATH_NOT_READY,
    BACKEND_NDL_VIDEO_PATH_READY,
    BACKEND_NDL_VIDEO_PATH_FAILED
} BackendNdlVideoPathState;

/* Generations skip zero on rollover; cumulative picture counters wrap modulo 2^32. */
typedef struct BackendNdlVideoStatus {
    BackendNdlVideoPathState state;
    uint32_t generation;
    bool supports_decode_only;
    bool supports_frame_events;
    uint32_t submitted, decoded, suppressed, forwarded;
    uint32_t queued_buffers, pending_pictures, max_pending_pictures;
    size_t queued_bytes;
} BackendNdlVideoStatus;

/* A matched decoder output, not confirmation of physical presentation. Times
 * use the same monotonic clock; their difference includes input queueing. */
typedef struct BackendNdlVideoFrameEvent {
    uint32_t generation;
    int64_t pts_ms;
    int64_t submitted_at_us, decoded_at_us;
    bool decode_only;
} BackendNdlVideoFrameEvent;

/* May run on streaming threads. Must be short, thread-safe and non-reentrant
 * into this context. Event storage expires on return. No calls after close. */
typedef void (*BackendNdlVideoFrameFn)(void *userdata, const BackendNdlVideoFrameEvent *event);

/* Derive milliseconds from the successful Load generation observed by this
 * feed. Feed calls never acquire the wrapper lifecycle mutex. */
#define BACKEND_NDL_PTS_AUTO INT64_MIN

/* Smallest render-queue threshold known to affect observed firmware. Values 0 and 1
 * have been observed to return success while logging that no threshold is needed. */
#define BACKEND_NDL_MIN_FRAME_DROP_THRESHOLD 2

typedef enum BackendNdlResult {
    BACKEND_NDL_OK = 0,
    BACKEND_NDL_ERROR = 1,
    BACKEND_NDL_UNAVAILABLE = 2,
    BACKEND_NDL_UNSUPPORTED = 3,
    BACKEND_NDL_INVALID_ARGUMENT = 4,
    BACKEND_NDL_NOT_READY = 5,
    BACKEND_NDL_OVERFLOW = 6,
    BACKEND_NDL_NEED_KEYFRAME = 7
} BackendNdlResult;

typedef enum BackendNdlVideoCodec {
    BACKEND_NDL_VIDEO_NONE = 0,
    BACKEND_NDL_VIDEO_H264 = 1,
    BACKEND_NDL_VIDEO_H265 = 2,
    BACKEND_NDL_VIDEO_VP9 = 3,
    BACKEND_NDL_VIDEO_AV1 = 4
} BackendNdlVideoCodec;

typedef enum BackendNdlAudioCodec {
    BACKEND_NDL_AUDIO_NONE = 0,
    BACKEND_NDL_AUDIO_PCM_S16LE = 1,
    BACKEND_NDL_AUDIO_OPUS = 2
} BackendNdlAudioCodec;

typedef enum BackendNdlLogLevel {
    BACKEND_NDL_LOG_DEBUG = 0,
    BACKEND_NDL_LOG_INFO = 1,
    BACKEND_NDL_LOG_WARN = 2,
    BACKEND_NDL_LOG_ERROR = 3,
    BACKEND_NDL_LOG_OFF = 4
} BackendNdlLogLevel;

typedef enum BackendNdlMultichannelStatus {
    BACKEND_NDL_MULTICHANNEL_UNKNOWN = -1,
    BACKEND_NDL_MULTICHANNEL_UNSUPPORTED = 0,
    BACKEND_NDL_MULTICHANNEL_NO_DEVICE = 1,
    BACKEND_NDL_MULTICHANNEL_OUTPUT_MODE_BLOCKED = 2,
    BACKEND_NDL_MULTICHANNEL_READY = 3
} BackendNdlMultichannelStatus;

typedef enum BackendNdlAbiProfile {
    BACKEND_NDL_ABI_UNKNOWN = 0,
    BACKEND_NDL_ABI_V2_CALLBACK_INIT = 1,
    BACKEND_NDL_ABI_V2_WINDOW_ID_INIT = 2
} BackendNdlAbiProfile;

typedef uint32_t BackendNdlCapabilities;
enum BackendNdlCapability {
    /* Reserved for source compatibility. The facade owns its loader
     * lifecycle; backend_ndl never reports or invokes this capability. */
    BACKEND_NDL_CAP_DL_LIFECYCLE = UINT32_C(1) << 0,
    BACKEND_NDL_CAP_APP_STATE = UINT32_C(1) << 1,
    BACKEND_NDL_CAP_WINDOW_ID = UINT32_C(1) << 2,
    BACKEND_NDL_CAP_VIDEO_PLAY = UINT32_C(1) << 3,
    BACKEND_NDL_CAP_VIDEO_AREA = UINT32_C(1) << 4,
    BACKEND_NDL_CAP_VIDEO_FRAME_DROP = UINT32_C(1) << 5,
    BACKEND_NDL_CAP_VIDEO_HDR = UINT32_C(1) << 6,
    BACKEND_NDL_CAP_VIDEO_QUEUE_LENGTH = UINT32_C(1) << 7,
    BACKEND_NDL_CAP_VIDEO_FLUSH = UINT32_C(1) << 8,
    BACKEND_NDL_CAP_AUDIO_PLAY = UINT32_C(1) << 9,
    BACKEND_NDL_CAP_AUDIO_AVAILABLE = UINT32_C(1) << 10,
    BACKEND_NDL_CAP_AUDIO_TOTAL = UINT32_C(1) << 11,
    BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY = UINT32_C(1) << 12,
    BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_CALLBACK = UINT32_C(1) << 13,
    BACKEND_NDL_CAP_EFFECT = UINT32_C(1) << 14
};

/* Callbacks are synchronous and may run concurrently on firmware/feed
 * threads. They must be thread-safe, must remain valid until close returns,
 * and must not call back into the same BackendNdl context. */
typedef void (*BackendNdlLogFn)(void *userdata, BackendNdlLogLevel level, const char *message);
typedef void (*BackendNdlMediaEventFn)(void *userdata, int type, int64_t number, const char *text);
typedef void (*BackendNdlMultichannelChangedFn)(void *userdata,
                                                BackendNdlMultichannelStatus status,
                                                const char *detail);

typedef struct BackendNdlConfig {
    /* Required non-empty webOS application id. Copied by backend_ndl_open(). */
    const char *app_id;

    /* Optional facade path override. NULL tries firmware sonames, then RTLD_DEFAULT. */
    const char *library_path;

    /* Optional external window id. Copied and applied before DirectMediaInit. */
    const char *window_id;

    /* Optional getSystemInfo sdkVersion. Copied by backend_ndl_open(); values
     * below 5 are rejected, while missing/malformed values rely on ABI probe. */
    const char *webos_sdk_version;

    BackendNdlLogFn log_fn;
    BackendNdlMediaEventFn media_event_fn;
    BackendNdlMultichannelChangedFn multichannel_changed_fn;
    void *userdata;
    BackendNdlLogLevel minimum_log_level;

    /* Best-effort firmware render-queue threshold, applied before every media load
     * as videoRenderQueueThreshold. Values below
     * BACKEND_NDL_MIN_FRAME_DROP_THRESHOLD are not sent. The default -1 and the
     * public setter's accepted values 0 and 1 therefore leave threshold arming
     * disabled. Exact queueing and drop behavior is device/firmware specific. */
    int video_frame_drop_threshold;
    bool require_keyframe_after_reload;
    bool guard_audio_overflow;
    /* Prime configured PCM or Opus after each successful media load. */
    bool prime_pcm_after_load;
    BackendNdlVideoFrameFn video_frame_fn;
} BackendNdlConfig;

typedef struct BackendNdlVideoInfo {
    BackendNdlVideoCodec codec;
    uint32_t width;
    uint32_t height;
    BackendNdlAppType app_type;
} BackendNdlVideoInfo;

typedef struct BackendNdlPcmInfo {
    uint32_t sample_rate_hz;
    /* 1 (mono), 2 (stereo), or 6. Six channels require READY status. */
    uint16_t channels;
} BackendNdlPcmInfo;

typedef struct BackendNdlAudioInfo {
    BackendNdlAudioCodec codec;
    uint32_t sample_rate_hz;
    uint16_t channels;
    BackendNdlAppType app_type;
} BackendNdlAudioInfo;

/* Exact field order of the target DirectVideo HDR value parameter. */
typedef struct BackendNdlHdrInfo {
    uint32_t display_primaries_gx;
    uint32_t display_primaries_gy;
    uint32_t display_primaries_bx;
    uint32_t display_primaries_by;
    uint32_t display_primaries_rx;
    uint32_t display_primaries_ry;
    uint32_t white_point_x;
    uint32_t white_point_y;
    uint32_t max_display_mastering_luminance;
    uint32_t min_display_mastering_luminance;
    uint32_t max_content_light_level;
    uint32_t max_frame_average_light_level;
    uint32_t transfer_characteristics;
    uint32_t color_primaries;
    uint32_t matrix_coeffs;
} BackendNdlHdrInfo;

/* Mutex-free coherent snapshot of synchronous DirectVideoPlay calls since the
 * most recent successful media load. A zero sample count means the timing
 * fields are not yet valid. */
typedef struct BackendNdlRuntimeStats {
    uint32_t video_play_samples;
    uint32_t video_play_us_ewma;
    uint32_t video_play_us_max;
    uint32_t video_play_errors;
} BackendNdlRuntimeStats;

void backend_ndl_config_defaults(BackendNdlConfig *config);

/* DirectMedia is process-global, so only one context may exist. close must not
 * race any API call; stop and join feed producers before closing. */
BackendNdl *backend_ndl_open(const BackendNdlConfig *config);
void backend_ndl_close(BackendNdl *ctx);

/* Last-resort DirectMedia release for a process that is dying without reaching
 * backend_ndl_close() (fatal signal, stray exit()). The claim is a lock-free
 * pointer exchange — safe in a signal handler and once-only per open (a new
 * backend_ndl_open() re-arms it). The NDL_DirectMediaUnload/NDL_DirectMediaQuit
 * calls it then makes are vendor code with no async-signal-safety guarantee,
 * so the release stays best effort: pair it with a watchdog such as alarm()
 * when calling from a fatal context, and expect a concurrent second fatal
 * signal to be able to cut it short. A backend_ndl_close() that finds the
 * claim gone skips its own facade teardown, including dlclose, in case the
 * claimant is still mid-call. Returns true when the release calls were
 * attempted; their NDL return codes are discarded. The BackendNdl object stays
 * allocated and unusable — the process must terminate afterwards. */
bool backend_ndl_emergency_release(void);

BackendNdlCapabilities backend_ndl_get_capabilities(const BackendNdl *ctx);
BackendNdlAbiProfile backend_ndl_get_abi_profile(const BackendNdl *ctx);
const char *backend_ndl_abi_profile_name(BackendNdlAbiProfile profile);

/* Replace both tracks as one DirectMedia configuration. FeedAudio/FeedVideo
 * may overlap this control operation; a crossed generation is reported as a
 * transient NOT_READY/NEED_KEYFRAME result. */
BackendNdlResult backend_ndl_set_media(BackendNdl *ctx,
                                      const BackendNdlVideoInfo *video,
                                      const BackendNdlAudioInfo *audio);
BackendNdlResult backend_ndl_unload(BackendNdl *ctx);
BackendNdlResult backend_ndl_reload(BackendNdl *ctx);

/* Audio and video feeds may execute concurrently and never take the lifecycle mutex.
 * Callers retain ordering responsibility within each individual track. */
BackendNdlResult backend_ndl_feed_video(BackendNdl *ctx,
                                        const void *data,
                                        size_t len,
                                        long long ndl_pts,
                                        bool is_keyframe);
/* CONFIG_ONLY expects no image and cannot accompany DECODE_ONLY or is_keyframe.
 * With output tracking enabled, parameter-only AUs must use this function.
 * Unknown flags are invalid; unsupported DECODE_ONLY is rejected. */
BackendNdlResult backend_ndl_feed_video_ex(BackendNdl *ctx, const void *data, size_t len,
                                           long long ndl_pts, bool is_keyframe,
                                           uint32_t flags);
BackendNdlResult backend_ndl_get_video_status(const BackendNdl *ctx,
                                              BackendNdlVideoStatus *out);
BackendNdlResult backend_ndl_feed_audio(BackendNdl *ctx,
                                        const void *data,
                                        size_t len,
                                        long long ndl_pts);

long long backend_ndl_media_time_ms(const BackendNdl *ctx);
BackendNdlResult backend_ndl_get_runtime_stats(const BackendNdl *ctx,
                                               BackendNdlRuntimeStats *out);

BackendNdlResult backend_ndl_get_audio_available(BackendNdl *ctx, int *available_bytes);
BackendNdlResult backend_ndl_get_audio_total(BackendNdl *ctx, int *total_bytes);
BackendNdlResult backend_ndl_get_multichannel_status(BackendNdl *ctx,
                                                     BackendNdlMultichannelStatus *status);
BackendNdlResult backend_ndl_get_video_render_buffer_length(BackendNdl *ctx, int *frames);
BackendNdlResult backend_ndl_flush_video_render_buffer(BackendNdl *ctx);
BackendNdlResult backend_ndl_set_video_area(BackendNdl *ctx, int left, int top, int width, int height);
/* Configure future media loads. This does not alter an already loaded pipeline;
 * use backend_ndl_reload() to apply the new value. Values 0 and 1 disable
 * threshold arming and are never passed to the facade. */
BackendNdlResult backend_ndl_set_video_frame_drop_threshold(BackendNdl *ctx, int threshold);
BackendNdlResult backend_ndl_set_video_hdr_info(BackendNdl *ctx, const BackendNdlHdrInfo *info);
BackendNdlResult backend_ndl_set_foreground(BackendNdl *ctx, bool foreground);

BackendNdlResult backend_ndl_effect_load(BackendNdl *ctx,
                                        const BackendNdlPcmInfo *pcm,
                                        uint32_t *preferred_size);
BackendNdlResult backend_ndl_effect_feed(BackendNdl *ctx, const void *data, size_t len);
BackendNdlResult backend_ndl_get_effect_available(BackendNdl *ctx, uint32_t *available_bytes);
BackendNdlResult backend_ndl_effect_unload(BackendNdl *ctx);

bool backend_ndl_pcm_sample_rate_supported(uint32_t hz);
const char *backend_ndl_result_name(BackendNdlResult result);
const char *backend_ndl_log_level_name(BackendNdlLogLevel level);

#ifdef __cplusplus
}
#endif

#endif

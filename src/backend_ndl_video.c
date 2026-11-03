/* SPDX-License-Identifier: MIT */
/* Video translation, mutex-free DirectVideo feed and video control APIs. */
#define _GNU_SOURCE

#include "backend_ndl_internal.h"

#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

#define BACKEND_NDL_VIDEO_STATS_TAG_SHIFT 32u
#define BACKEND_NDL_VIDEO_STATS_VALUE_MASK UINT64_C(0xffffffff)
#define BACKEND_NDL_VIDEO_STATS_EWMA_TAG_SHIFT 33u
#define BACKEND_NDL_VIDEO_STATS_EWMA_TAG_MASK UINT32_C(0x7fffffff)
#define BACKEND_NDL_VIDEO_STATS_EWMA_INITIALIZED (UINT64_C(1) << 32u)

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
static void (*g_backend_ndl_test_after_video_stats_begin)(BackendNdl *);

void backend_ndl_test_set_after_video_stats_begin(void (*hook)(BackendNdl *)) {
    g_backend_ndl_test_after_video_stats_begin = hook;
}
#endif

static NDL_DIRECTVIDEO_HDR_INFO_T backend_ndl_sdk_hdr(const BackendNdlHdrInfo *info) {
    NDL_DIRECTVIDEO_HDR_INFO_T result;
    _Static_assert(sizeof(result) == sizeof(*info), "public/private HDR layouts match");
    memcpy(&result, info, sizeof(result));
    return result;
}

BackendNdlResult backend_ndl_video_fill_media_info(BackendNdlAbiProfile profile,
                                                   const BackendNdlVideoInfo *video,
                                                   BackendNdlMediaInfo *out) {
    if (video->width == 0 || video->height == 0) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    if (profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT) {
        if (video->codec != BACKEND_NDL_VIDEO_H264) {
            return BACKEND_NDL_UNSUPPORTED;
        }
        out->callback.video.width = (int)video->width;
        out->callback.video.height = (int)video->height;
        out->callback.video.type = BACKEND_NDL_LEGACY_VIDEO_H264;
        return BACKEND_NDL_OK;
    }
    switch (video->codec) {
        case BACKEND_NDL_VIDEO_H264:
            out->current.videoDataInfo.source = NDL_DIRECTVIDEO_SRC_TYPE_H264;
            break;
        case BACKEND_NDL_VIDEO_H265:
            out->current.videoDataInfo.source = NDL_DIRECTVIDEO_SRC_TYPE_HEVC;
            break;
        case BACKEND_NDL_VIDEO_VP9:
            out->current.videoDataInfo.source = NDL_DIRECTVIDEO_SRC_TYPE_VP9;
            break;
        case BACKEND_NDL_VIDEO_AV1:
            out->current.videoDataInfo.source = NDL_DIRECTVIDEO_SRC_TYPE_AV1;
            break;
        default:
            return BACKEND_NDL_UNSUPPORTED;
    }
    out->current.videoDataInfo.width = (int)video->width;
    out->current.videoDataInfo.height = (int)video->height;
    return BACKEND_NDL_OK;
}

/* The threshold is consumed while the firmware builds the media-load payload as
 * option.externalStreamingInfo.contents.esInfo.videoRenderQueueThreshold. It must
 * therefore be set before every load: the observed implementation clears its
 * "threshold set" flag on unload. Values <= 1 are an observed no-op (with a debug
 * message despite a successful return), so the smallest known effective value is 2.
 * The exact queueing and frame-drop behavior remains firmware/device specific. */
void backend_ndl_video_before_load_locked(BackendNdl *ctx) {
    if (ctx->config.video_frame_drop_threshold < BACKEND_NDL_MIN_FRAME_DROP_THRESHOLD ||
        !ctx->api.DirectVideoSetFrameDropThreshold) {
        return;
    }
    int rc = ctx->api.DirectVideoSetFrameDropThreshold(ctx->config.video_frame_drop_threshold);
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                        "NDL_DirectVideoSetFrameDropThreshold(%d) failed rc=%d error=%s",
                        ctx->config.video_frame_drop_threshold, rc,
                        backend_ndl_last_error(ctx));
        return;
    }
    BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "video render queue threshold armed: %d",
                    ctx->config.video_frame_drop_threshold);
}

void backend_ndl_video_after_load_locked(BackendNdl *ctx) {
    if (!ctx->media.has_video) return;
    if (ctx->has_video_area && ctx->api.DirectVideoSetArea) {
        int rc = ctx->api.DirectVideoSetArea(ctx->video_left, ctx->video_top,
                                             ctx->video_width, ctx->video_height);
        if (rc != 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                            "NDL_DirectVideoSetArea failed rc=%d error=%s", rc,
                            backend_ndl_last_error(ctx));
        }
    }
    if (ctx->media.has_video_hdr && ctx->api.DirectVideoSetHDRInfo) {
        int rc =
            ctx->api.DirectVideoSetHDRInfo(backend_ndl_sdk_hdr(&ctx->media.video_hdr));
        if (rc != 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                            "NDL_DirectVideoSetHDRInfo failed rc=%d error=%s", rc,
                            backend_ndl_last_error(ctx));
        }
    }
}

static bool backend_ndl_video_state_ready(uint32_t initial, uint32_t current) {
    return backend_ndl_feed_generation(initial) == backend_ndl_feed_generation(current) &&
           (current & BACKEND_NDL_FEED_VIDEO_READY) != 0;
}

static unsigned long long backend_ndl_video_stats_pack(uint32_t tag, uint32_t value) {
    return ((unsigned long long)tag << BACKEND_NDL_VIDEO_STATS_TAG_SHIFT) | value;
}

static uint32_t backend_ndl_video_stats_packed_tag(unsigned long long value) {
    return (uint32_t)(value >> BACKEND_NDL_VIDEO_STATS_TAG_SHIFT);
}

static uint32_t backend_ndl_video_stats_packed_value(unsigned long long value) {
    return (uint32_t)(value & BACKEND_NDL_VIDEO_STATS_VALUE_MASK);
}

static unsigned long long backend_ndl_video_stats_pack_ewma(uint32_t tag,
                                                            bool initialized,
                                                            uint32_t value) {
    return ((unsigned long long)(tag & BACKEND_NDL_VIDEO_STATS_EWMA_TAG_MASK)
            << BACKEND_NDL_VIDEO_STATS_EWMA_TAG_SHIFT) |
           (initialized ? BACKEND_NDL_VIDEO_STATS_EWMA_INITIALIZED : 0) | value;
}

static uint32_t backend_ndl_video_stats_ewma_tag(unsigned long long value) {
    return (uint32_t)(value >> BACKEND_NDL_VIDEO_STATS_EWMA_TAG_SHIFT);
}

static bool backend_ndl_video_stats_writer_begin(BackendNdl *ctx, uint32_t tag) {
    unsigned long long observed =
        atomic_load_explicit(&ctx->video_stats_writers, memory_order_acquire);
    for (;;) {
        if (backend_ndl_video_stats_packed_tag(observed) != tag) {
            return false;
        }
        uint32_t writers = backend_ndl_video_stats_packed_value(observed);
        if (writers == UINT32_MAX) {
            return false;
        }
        unsigned long long desired = backend_ndl_video_stats_pack(tag, writers + 1u);
        if (atomic_compare_exchange_weak_explicit(
                &ctx->video_stats_writers, &observed, desired, memory_order_acq_rel,
                memory_order_acquire)) {
            return true;
        }
    }
}

static void backend_ndl_video_stats_increment(_Atomic unsigned long long *field,
                                              uint32_t tag,
                                              bool saturate,
                                              memory_order success_order) {
    unsigned long long observed = atomic_load_explicit(field, memory_order_relaxed);
    for (;;) {
        if (backend_ndl_video_stats_packed_tag(observed) != tag) {
            return;
        }
        uint32_t value = backend_ndl_video_stats_packed_value(observed);
        if (saturate && value == UINT32_MAX) {
            return;
        }
        unsigned long long desired = backend_ndl_video_stats_pack(tag, value + 1u);
        if (atomic_compare_exchange_weak_explicit(field, &observed, desired,
                                                  success_order,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void backend_ndl_video_stats_update_ewma(BackendNdl *ctx,
                                                uint32_t tag,
                                                uint32_t sample_us) {
    unsigned long long observed =
        atomic_load_explicit(&ctx->video_play_us_ewma, memory_order_relaxed);
    for (;;) {
        if (backend_ndl_video_stats_ewma_tag(observed) !=
            (tag & BACKEND_NDL_VIDEO_STATS_EWMA_TAG_MASK)) {
            return;
        }
        bool initialized =
            (observed & BACKEND_NDL_VIDEO_STATS_EWMA_INITIALIZED) != 0;
        uint32_t ewma = backend_ndl_video_stats_packed_value(observed);
        uint32_t next;
        if (!initialized) {
            next = sample_us;
        } else if (sample_us >= ewma) {
            uint32_t difference = sample_us - ewma;
            uint32_t step = difference / 8u + (difference % 8u != 0);
            next = ewma + step;
        } else {
            uint32_t difference = ewma - sample_us;
            uint32_t step = difference / 8u + (difference % 8u != 0);
            next = ewma - step;
        }
        unsigned long long desired = backend_ndl_video_stats_pack_ewma(tag, true, next);
        if (atomic_compare_exchange_weak_explicit(
                &ctx->video_play_us_ewma, &observed, desired,
                memory_order_relaxed, memory_order_relaxed)) {
            return;
        }
    }
}

static void backend_ndl_video_stats_update_max(BackendNdl *ctx,
                                               uint32_t tag,
                                               uint32_t sample_us) {
    unsigned long long observed =
        atomic_load_explicit(&ctx->video_play_us_max, memory_order_relaxed);
    for (;;) {
        if (backend_ndl_video_stats_packed_tag(observed) != tag ||
            backend_ndl_video_stats_packed_value(observed) >= sample_us) {
            return;
        }
        unsigned long long desired = backend_ndl_video_stats_pack(tag, sample_us);
        if (atomic_compare_exchange_weak_explicit(&ctx->video_play_us_max, &observed,
                                                  desired, memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void backend_ndl_video_stats_writer_end(BackendNdl *ctx, uint32_t tag) {
    backend_ndl_video_stats_increment(&ctx->video_stats_version, tag, false,
                                      memory_order_release);

    unsigned long long observed =
        atomic_load_explicit(&ctx->video_stats_writers, memory_order_relaxed);
    for (;;) {
        if (backend_ndl_video_stats_packed_tag(observed) != tag) {
            return;
        }
        uint32_t writers = backend_ndl_video_stats_packed_value(observed);
        if (writers == 0) {
            return;
        }
        unsigned long long desired = backend_ndl_video_stats_pack(tag, writers - 1u);
        if (atomic_compare_exchange_weak_explicit(
                &ctx->video_stats_writers, &observed, desired, memory_order_acq_rel,
                memory_order_acquire)) {
            return;
        }
    }
}

void backend_ndl_video_reset_runtime_stats(BackendNdl *ctx) {
    uint32_t epoch =
        atomic_load_explicit(&ctx->video_stats_epoch, memory_order_relaxed);
    uint32_t next_epoch = epoch == UINT32_MAX ? 1 : epoch + 1;
    uint32_t tag = next_epoch;
    unsigned long long empty = backend_ndl_video_stats_pack(tag, 0);

    /* Publish the writer tag first so an old writer either finishes before
     * these stores or sees the new tag and abandons its update. New writers do
     * not observe next_epoch until the final release store. */
    atomic_store_explicit(&ctx->video_stats_writers, empty, memory_order_relaxed);
    atomic_store_explicit(&ctx->video_stats_version, empty, memory_order_relaxed);
    atomic_store_explicit(&ctx->video_play_samples, empty, memory_order_relaxed);
    atomic_store_explicit(&ctx->video_play_us_ewma,
                          backend_ndl_video_stats_pack_ewma(tag, false, 0),
                          memory_order_relaxed);
    atomic_store_explicit(&ctx->video_play_us_max, empty, memory_order_relaxed);
    atomic_store_explicit(&ctx->video_play_errors, empty, memory_order_relaxed);
    atomic_store_explicit(&ctx->video_stats_epoch, next_epoch, memory_order_release);
}

static void backend_ndl_video_record_runtime(BackendNdl *ctx,
                                             uint32_t expected_epoch,
                                             long long elapsed_us,
                                             bool failed) {
    uint32_t tag = expected_epoch;
    if (!backend_ndl_video_stats_writer_begin(ctx, tag)) {
        return;
    }

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
    if (g_backend_ndl_test_after_video_stats_begin) {
        g_backend_ndl_test_after_video_stats_begin(ctx);
    }
#endif

    uint32_t bounded_sample = elapsed_us <= 0 ? 0
        : elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
    backend_ndl_video_stats_update_ewma(ctx, tag, bounded_sample);
    backend_ndl_video_stats_update_max(ctx, tag, bounded_sample);
    if (failed) {
        backend_ndl_video_stats_increment(&ctx->video_play_errors, tag, true,
                                          memory_order_relaxed);
    }
    backend_ndl_video_stats_increment(&ctx->video_play_samples, tag, true,
                                      memory_order_relaxed);
    backend_ndl_video_stats_writer_end(ctx, tag);
}

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
void backend_ndl_test_record_video_runtime(BackendNdl *ctx,
                                           uint32_t elapsed_us,
                                           bool failed) {
    uint32_t epoch =
        atomic_load_explicit(&ctx->video_stats_epoch, memory_order_acquire);
    backend_ndl_video_record_runtime(ctx, epoch, elapsed_us, failed);
}
#endif

BackendNdlResult backend_ndl_feed_video(BackendNdl *ctx,
                                        const void *data,
                                        size_t len,
                                        long long ndl_pts,
                                        bool is_keyframe) {
    return backend_ndl_feed_video_ex(ctx, data, len, ndl_pts, is_keyframe, 0);
}

BackendNdlResult backend_ndl_feed_video_ex(BackendNdl *ctx, const void *data, size_t len,
                                           long long ndl_pts, bool is_keyframe,
                                           uint32_t flags) {
    if (!ctx || !data || len == 0 || len > UINT32_MAX) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    if ((flags & ~(BACKEND_NDL_VIDEO_DECODE_ONLY | BACKEND_NDL_VIDEO_CONFIG_ONLY)) ||
        ((flags & BACKEND_NDL_VIDEO_CONFIG_ONLY) &&
         ((flags & BACKEND_NDL_VIDEO_DECODE_ONLY) || is_keyframe))) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    if (!ctx->api.DirectVideoPlay) return BACKEND_NDL_UNAVAILABLE;
    BackendNdlVideoStatus path;
    backend_ndl_get_video_status(ctx, &path);
    if (path.state == BACKEND_NDL_VIDEO_PATH_FAILED) return BACKEND_NDL_ERROR;
    if (path.state != BACKEND_NDL_VIDEO_PATH_READY) return BACKEND_NDL_NOT_READY;
    if ((flags & BACKEND_NDL_VIDEO_DECODE_ONLY) && !path.supports_decode_only)
        return BACKEND_NDL_UNSUPPORTED;
    uint32_t state = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if ((state & BACKEND_NDL_FEED_VIDEO_READY) == 0) {
        return BACKEND_NDL_NOT_READY;
    }
    if ((state & BACKEND_NDL_FEED_NEED_KEYFRAME) != 0 && !is_keyframe) {
        return BACKEND_NDL_NEED_KEYFRAME;
    }
    if (ndl_pts == BACKEND_NDL_PTS_AUTO) {
        ndl_pts = backend_ndl_media_time_for_state(ctx, state);
    }

    uint32_t stats_epoch =
        atomic_load_explicit(&ctx->video_stats_epoch, memory_order_acquire);
    uint32_t before_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_video_state_ready(state, before_play)) {
        return (before_play & BACKEND_NDL_FEED_VIDEO_READY) != 0
                   ? BACKEND_NDL_NEED_KEYFRAME
                   : BACKEND_NDL_NOT_READY;
    }

    long long started_us = backend_ndl_monotonic_us();
    void *submission = backend_ndl_gst_feed_begin(ctx, flags);
    int rc = ctx->api.DirectVideoPlay((void *)data, (unsigned int)len, ndl_pts);
    BackendNdlResult gst_result = backend_ndl_gst_feed_end(submission);
    long long finished_us = backend_ndl_monotonic_us();
    backend_ndl_video_record_runtime(ctx, stats_epoch, finished_us - started_us,
                                     rc != 0);
    uint32_t after_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_video_state_ready(state, after_play)) {
        return (after_play & BACKEND_NDL_FEED_VIDEO_READY) != 0
                   ? BACKEND_NDL_NEED_KEYFRAME
                   : BACKEND_NDL_NOT_READY;
    }
    backend_ndl_get_video_status(ctx, &path);
    if (gst_result != BACKEND_NDL_OK || path.state == BACKEND_NDL_VIDEO_PATH_FAILED)
        return BACKEND_NDL_ERROR;
    if (rc != 0) {
        unsigned int streak = atomic_fetch_add_explicit(&ctx->video_error_streak, 1,
                                                         memory_order_relaxed) + 1;
        if (streak == 1 || streak % BACKEND_NDL_VIDEO_DEBUG_CADENCE == 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "NDL_DirectVideoPlay failure #%u rc=%d len=%zu error=%s "
                            "(error text best-effort)",
                            streak, rc, len, backend_ndl_last_error(ctx));
        }
        return BACKEND_NDL_ERROR;
    }
    atomic_store_explicit(&ctx->video_error_streak, 0, memory_order_relaxed);

    if (is_keyframe && (after_play & BACKEND_NDL_FEED_NEED_KEYFRAME) != 0) {
        uint32_t expected = after_play;
        uint32_t desired = after_play & ~BACKEND_NDL_FEED_NEED_KEYFRAME;
        if (!atomic_compare_exchange_strong_explicit(&ctx->feed_state, &expected, desired,
                                                     memory_order_acq_rel,
                                                     memory_order_acquire) &&
            !backend_ndl_video_state_ready(state, expected)) {
            return (expected & BACKEND_NDL_FEED_VIDEO_READY) != 0
                       ? BACKEND_NDL_NEED_KEYFRAME
                       : BACKEND_NDL_NOT_READY;
        }
    }

    uint32_t count = atomic_fetch_add_explicit(&ctx->video_feed_count, 1,
                                                          memory_order_relaxed) + 1;
    if (backend_ndl_log_enabled(ctx, BACKEND_NDL_LOG_DEBUG) &&
        count % BACKEND_NDL_VIDEO_DEBUG_CADENCE == 0) {
        int depth = -1;
        if (ctx->api.DirectVideoGetRenderBufferLength) {
            (void)ctx->api.DirectVideoGetRenderBufferLength(&depth);
        }
        BackendNdlRuntimeStats stats;
        (void)backend_ndl_get_runtime_stats(ctx, &stats);
        BACKEND_NDL_DEBUG_LOG(
            ctx,
            "video sink: fed=%u render_queue=%d pts=%lld play_calls=%u "
            "play_us_ewma=%u play_us_max=%u play_errors=%u",
            count, depth, ndl_pts, stats.video_play_samples, stats.video_play_us_ewma,
            stats.video_play_us_max, stats.video_play_errors);
    }
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_get_video_status(const BackendNdl *ctx,
                                              BackendNdlVideoStatus *out) {
    if (!ctx || !out) return BACKEND_NDL_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    uint32_t state = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    out->generation = backend_ndl_feed_generation(state);
    out->state = (state & BACKEND_NDL_FEED_VIDEO_READY)
                     ? BACKEND_NDL_VIDEO_PATH_READY : BACKEND_NDL_VIDEO_PATH_NOT_READY;
    backend_ndl_gst_status(ctx, out);
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_get_runtime_stats(const BackendNdl *ctx,
                                               BackendNdlRuntimeStats *out) {
    if (!ctx || !out) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }

    uint32_t samples;
    uint32_t ewma;
    uint32_t maximum;
    uint32_t errors;
    for (;;) {
        uint32_t epoch =
            atomic_load_explicit(&ctx->video_stats_epoch, memory_order_acquire);
        uint32_t tag = epoch;
        unsigned long long writers_before =
            atomic_load_explicit(&ctx->video_stats_writers, memory_order_acquire);
        unsigned long long version_before =
            atomic_load_explicit(&ctx->video_stats_version, memory_order_acquire);
        if (backend_ndl_video_stats_packed_tag(writers_before) != tag ||
            backend_ndl_video_stats_packed_value(writers_before) != 0 ||
            backend_ndl_video_stats_packed_tag(version_before) != tag) {
            sched_yield();
            continue;
        }

        unsigned long long packed_samples =
            atomic_load_explicit(&ctx->video_play_samples, memory_order_relaxed);
        unsigned long long packed_ewma =
            atomic_load_explicit(&ctx->video_play_us_ewma, memory_order_relaxed);
        unsigned long long packed_maximum =
            atomic_load_explicit(&ctx->video_play_us_max, memory_order_relaxed);
        unsigned long long packed_errors =
            atomic_load_explicit(&ctx->video_play_errors, memory_order_relaxed);
        if (backend_ndl_video_stats_packed_tag(packed_samples) != tag ||
            backend_ndl_video_stats_ewma_tag(packed_ewma) !=
                (tag & BACKEND_NDL_VIDEO_STATS_EWMA_TAG_MASK) ||
            backend_ndl_video_stats_packed_tag(packed_maximum) != tag ||
            backend_ndl_video_stats_packed_tag(packed_errors) != tag) {
            continue;
        }
        /* Keep the snapshot reads before writer/version validation on ARM. */
        atomic_thread_fence(memory_order_acquire);
        unsigned long long writers_after =
            atomic_load_explicit(&ctx->video_stats_writers, memory_order_acquire);
        unsigned long long version_after =
            atomic_load_explicit(&ctx->video_stats_version, memory_order_acquire);
        uint32_t epoch_after =
            atomic_load_explicit(&ctx->video_stats_epoch, memory_order_acquire);
        if (epoch != epoch_after || version_before != version_after ||
            writers_before != writers_after) {
            continue;
        }

        samples = backend_ndl_video_stats_packed_value(packed_samples);
        ewma = (packed_ewma & BACKEND_NDL_VIDEO_STATS_EWMA_INITIALIZED) != 0
                   ? backend_ndl_video_stats_packed_value(packed_ewma)
                   : 0;
        maximum = backend_ndl_video_stats_packed_value(packed_maximum);
        errors = backend_ndl_video_stats_packed_value(packed_errors);
        break;
    }
    out->video_play_samples = samples;
    out->video_play_us_ewma = ewma;
    out->video_play_us_max = maximum;
    out->video_play_errors = errors;
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_get_video_render_buffer_length(BackendNdl *ctx, int *frames) {
    return backend_ndl_opt_get_int(ctx,
                                   ctx ? ctx->api.DirectVideoGetRenderBufferLength : NULL,
                                   frames);
}

BackendNdlResult backend_ndl_flush_video_render_buffer(BackendNdl *ctx) {
    if (!ctx) return BACKEND_NDL_INVALID_ARGUMENT;
    if (!ctx->api.DirectVideoFlushRenderBuffer) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    backend_ndl_gst_flush(ctx);
    int rc = ctx->api.DirectVideoFlushRenderBuffer();
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_set_video_area(BackendNdl *ctx, int left, int top,
                                            int width, int height) {
    if (!ctx || width <= 0 || height <= 0) return BACKEND_NDL_INVALID_ARGUMENT;
    if (!ctx->api.DirectVideoSetArea) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    ctx->has_video_area = true;
    ctx->video_left = left;
    ctx->video_top = top;
    ctx->video_width = width;
    ctx->video_height = height;
    int rc = 0;
    if (ctx->media_loaded && ctx->media.has_video) {
        rc = ctx->api.DirectVideoSetArea(left, top, width, height);
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_set_video_frame_drop_threshold(BackendNdl *ctx, int threshold) {
    if (!ctx || threshold < 0) return BACKEND_NDL_INVALID_ARGUMENT;
    if (!ctx->api.DirectVideoSetFrameDropThreshold) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    ctx->config.video_frame_drop_threshold = threshold;
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_set_video_hdr_info(BackendNdl *ctx,
                                                const BackendNdlHdrInfo *info) {
    if (!ctx || !info) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_VIDEO_HDR) == 0) {
        return BACKEND_NDL_UNAVAILABLE;
    }
    pthread_mutex_lock(&ctx->lifecycle_lock);
    if (!ctx->media.has_video) {
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return BACKEND_NDL_NOT_READY;
    }
    if (ctx->media.video.codec != BACKEND_NDL_VIDEO_VP9) {
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return BACKEND_NDL_UNSUPPORTED;
    }
    ctx->media.has_video_hdr = true;
    ctx->media.video_hdr = *info;
    int rc = 0;
    if (ctx->media_loaded) {
        rc = ctx->api.DirectVideoSetHDRInfo(backend_ndl_sdk_hdr(info));
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

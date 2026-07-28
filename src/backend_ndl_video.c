/* SPDX-License-Identifier: MIT */
/* Video translation, mutex-free DirectVideo feed and video control APIs. */
#define _GNU_SOURCE

#include "backend_ndl_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

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

void backend_ndl_video_after_load_locked(BackendNdl *ctx) {
    if (!ctx->media.has_video) return;
    if (ctx->config.video_frame_drop_threshold >= 0 &&
        ctx->api.DirectVideoSetFrameDropThreshold) {
        int rc = ctx->api.DirectVideoSetFrameDropThreshold(
            ctx->config.video_frame_drop_threshold);
        if (rc != 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                            "NDL_DirectVideoSetFrameDropThreshold(%d) failed rc=%d error=%s",
                            ctx->config.video_frame_drop_threshold, rc,
                            backend_ndl_last_error(ctx));
        }
    }
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

static bool backend_ndl_video_state_ready(uint64_t initial, uint64_t current) {
    return backend_ndl_feed_generation(initial) == backend_ndl_feed_generation(current) &&
           (current & BACKEND_NDL_FEED_VIDEO_READY) != 0;
}

BackendNdlResult backend_ndl_feed_video(BackendNdl *ctx,
                                        const void *data,
                                        size_t len,
                                        long long ndl_pts,
                                        bool is_keyframe) {
    if (!ctx || !data || len == 0 || len > UINT32_MAX) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    if (!ctx->api.DirectVideoPlay) {
        return BACKEND_NDL_UNAVAILABLE;
    }

    uint64_t state = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if ((state & BACKEND_NDL_FEED_VIDEO_READY) == 0) {
        return BACKEND_NDL_NOT_READY;
    }
    if ((state & BACKEND_NDL_FEED_NEED_KEYFRAME) != 0 && !is_keyframe) {
        return BACKEND_NDL_NEED_KEYFRAME;
    }
    if (ndl_pts == BACKEND_NDL_PTS_AUTO) {
        ndl_pts = backend_ndl_media_time_for_state(ctx, state);
    }

    uint64_t before_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_video_state_ready(state, before_play)) {
        return (before_play & BACKEND_NDL_FEED_VIDEO_READY) != 0
                   ? BACKEND_NDL_NEED_KEYFRAME
                   : BACKEND_NDL_NOT_READY;
    }

    int rc = ctx->api.DirectVideoPlay((void *)data, (unsigned int)len, ndl_pts);
    uint64_t after_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_video_state_ready(state, after_play)) {
        return (after_play & BACKEND_NDL_FEED_VIDEO_READY) != 0
                   ? BACKEND_NDL_NEED_KEYFRAME
                   : BACKEND_NDL_NOT_READY;
    }
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
        unsigned long long expected = after_play;
        unsigned long long desired = after_play & ~BACKEND_NDL_FEED_NEED_KEYFRAME;
        if (!atomic_compare_exchange_strong_explicit(&ctx->feed_state, &expected, desired,
                                                     memory_order_acq_rel,
                                                     memory_order_acquire) &&
            !backend_ndl_video_state_ready(state, expected)) {
            return (expected & BACKEND_NDL_FEED_VIDEO_READY) != 0
                       ? BACKEND_NDL_NEED_KEYFRAME
                       : BACKEND_NDL_NOT_READY;
        }
    }

    unsigned long long count = atomic_fetch_add_explicit(&ctx->video_feed_count, 1,
                                                          memory_order_relaxed) + 1;
    if (backend_ndl_log_enabled(ctx, BACKEND_NDL_LOG_DEBUG) &&
        count % BACKEND_NDL_VIDEO_DEBUG_CADENCE == 0 &&
        ctx->api.DirectVideoGetRenderBufferLength) {
        int depth = -1;
        if (ctx->api.DirectVideoGetRenderBufferLength(&depth) == 0) {
            BACKEND_NDL_DEBUG_LOG(ctx, "video sink: fed=%llu render_queue=%d pts=%lld",
                                  count, depth, ndl_pts);
        }
    }
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
    int rc = 0;
    if (ctx->media_loaded && ctx->media.has_video) {
        rc = ctx->api.DirectVideoSetFrameDropThreshold(threshold);
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
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

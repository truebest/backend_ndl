/* SPDX-License-Identifier: MIT */
/* PCM translation, mutex-free DirectAudio feed, multichannel capability and
 * the independent DirectEffect state machine. */
#define _GNU_SOURCE

#include "backend_ndl_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(BACKEND_NDL_ENABLED) && BACKEND_NDL_ENABLED

const char *backend_ndl_pcm_channel_mode(uint16_t channels) {
    switch (channels) {
        case 1:
            return "mono";
        case 2:
            return "stereo";
        case 6:
            return "6-channel";
        default:
            return NULL;
    }
}

int backend_ndl_pcm_sample_rate(BackendNdlAbiProfile profile, uint32_t hz) {
    switch (hz) {
        case 48000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_48K
                       : NDL_DIRECTAUDIO_SAMPLERATE_48K;
        case 44100:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_44_1K
                       : NDL_DIRECTAUDIO_SAMPLERATE_44_1K;
        case 32000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_32K
                       : NDL_DIRECTAUDIO_SAMPLERATE_32K;
        case 24000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_24K
                       : NDL_DIRECTAUDIO_SAMPLERATE_24K;
        case 16000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_16K
                       : NDL_DIRECTAUDIO_SAMPLERATE_16K;
        case 12000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_12K
                       : NDL_DIRECTAUDIO_SAMPLERATE_12K;
        case 8000:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_8K
                       : NDL_DIRECTAUDIO_SAMPLERATE_8K;
        case 22050:
            return profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                       ? BACKEND_NDL_LEGACY_PCM_22_05K
                       : NDL_DIRECTAUDIO_SAMPLERATE_22_05K;
        default:
            return 0;
    }
}

BackendNdlResult backend_ndl_fill_pcm_info(const BackendNdlPcmInfo *pcm,
                                           NDL_DIRECTAUDIO_PCM_INFO_T *out) {
    if (!pcm || !out) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    const char *mode = backend_ndl_pcm_channel_mode(pcm->channels);
    int rate = backend_ndl_pcm_sample_rate(BACKEND_NDL_ABI_V2_WINDOW_ID_INIT,
                                           pcm->sample_rate_hz);
    if (!mode || rate == 0) {
        return BACKEND_NDL_UNSUPPORTED;
    }
    memset(out, 0, sizeof(*out));
    out->format = (char *)"S16LE";
    out->layout = (char *)"interleaved";
    out->channelMode = (char *)mode;
    out->sampleRate = (NDL_DIRECTAUDIO_SAMPLERATE_T)rate;
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_audio_fill_media_info_locked(BackendNdl *ctx,
                                                          BackendNdlMediaInfo *out) {
    if (ctx->abi_profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT) {
        const char *mode = backend_ndl_pcm_channel_mode(ctx->pcm.channels);
        int rate = backend_ndl_pcm_sample_rate(ctx->abi_profile, ctx->pcm.sample_rate_hz);
        if (!mode || rate == 0 || ctx->pcm.channels > 2) {
            return BACKEND_NDL_UNSUPPORTED;
        }
        out->legacy.audio.pcm.type = BACKEND_NDL_LEGACY_AUDIO_PCM;
        out->legacy.audio.pcm.format = "S16LE";
        out->legacy.audio.pcm.layout = "interleaved";
        out->legacy.audio.pcm.channel_mode = mode;
        out->legacy.audio.pcm.sample_rate = rate;
        return BACKEND_NDL_OK;
    }
    BackendNdlResult result = backend_ndl_fill_pcm_info(
        &ctx->pcm, &out->current.audioDataInfo.audio_data.pcmInfo);
    if (result == BACKEND_NDL_OK) {
        out->current.audioDataInfo.audioCodec = NDL_DIRECTAUDIO_SRC_TYPE_PCM;
    }
    return result;
}

void backend_ndl_audio_after_load_locked(BackendNdl *ctx) {
    if (!ctx->has_audio || !ctx->config.prime_pcm_after_load || !ctx->api.DirectAudioPlay) {
        return;
    }
    unsigned short silence[6] = {0};
    unsigned int size = (unsigned int)ctx->pcm.channels * (unsigned int)sizeof(unsigned short);
    uint64_t transition = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    uint64_t temporary = transition | BACKEND_NDL_FEED_AUDIO_READY;
    long long pts = backend_ndl_media_time_for_state(ctx, temporary);
    int rc = ctx->api.DirectAudioPlay(silence, size, pts);
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                        "PCM priming failed rc=%d error=%s (continuing)", rc,
                        backend_ndl_last_error(ctx));
    } else {
        BACKEND_NDL_DEBUG_LOG(ctx, "primed PCM sink: %u bytes pts=%lld", size, pts);
    }
}

BackendNdlMultichannelStatus backend_ndl_multichannel_from_query(int value) {
    switch (value) {
        case 0:
            return BACKEND_NDL_MULTICHANNEL_UNSUPPORTED;
        case 1:
            return BACKEND_NDL_MULTICHANNEL_NO_DEVICE;
        case 2:
            return BACKEND_NDL_MULTICHANNEL_OUTPUT_MODE_BLOCKED;
        case 3:
            return BACKEND_NDL_MULTICHANNEL_READY;
        default:
            return BACKEND_NDL_MULTICHANNEL_UNKNOWN;
    }
}

BackendNdlMultichannelStatus backend_ndl_multichannel_from_callback(int value) {
    switch (value) {
        case 0:
            return BACKEND_NDL_MULTICHANNEL_NO_DEVICE;
        case 1:
            return BACKEND_NDL_MULTICHANNEL_OUTPUT_MODE_BLOCKED;
        case 2:
            return BACKEND_NDL_MULTICHANNEL_READY;
        default:
            return BACKEND_NDL_MULTICHANNEL_UNKNOWN;
    }
}

static void backend_ndl_multichannel_callback(int value, const char *detail) {
    pthread_mutex_lock(&g_backend_ndl_registry_lock);
    BackendNdl *ctx = g_backend_ndl_active;
    if (ctx) {
        BackendNdlMultichannelStatus status = backend_ndl_multichannel_from_callback(value);
        atomic_store_explicit(&ctx->multichannel_status, status, memory_order_release);
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                        "multichannel PCM status changed: raw=%d normalized=%d detail=%s", value,
                        (int)status, detail ? detail : "");
        if (ctx->config.multichannel_changed_fn) {
            ctx->config.multichannel_changed_fn(ctx->config.userdata, status, detail);
        }
    }
    pthread_mutex_unlock(&g_backend_ndl_registry_lock);
}

void backend_ndl_audio_register_capability_callback(BackendNdl *ctx) {
    if ((ctx->capabilities & BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY) != 0) {
        int raw = -1;
        int rc = ctx->api.DirectAudioSupportMultiChannel(&raw);
        if (rc == 0) {
            BackendNdlMultichannelStatus status = backend_ndl_multichannel_from_query(raw);
            atomic_store_explicit(&ctx->multichannel_status, status, memory_order_release);
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                            "multichannel PCM status: raw=%d normalized=%d", raw, (int)status);
        } else {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                            "NDL_DirectAudioSupportMultiChannel failed rc=%d error=%s", rc,
                            backend_ndl_last_error(ctx));
        }
    }
    if ((ctx->capabilities & BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_CALLBACK) == 0) {
        return;
    }
    int rc = ctx->api.DirectAudioRegisterCallback(backend_ndl_multichannel_callback);
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                        "NDL_DirectAudioRegisterCallback failed rc=%d error=%s", rc,
                        backend_ndl_last_error(ctx));
    } else {
        BACKEND_NDL_DEBUG_LOG(ctx, "multichannel capability callback registered");
    }
}

static bool backend_ndl_audio_state_ready(uint64_t initial, uint64_t current) {
    return backend_ndl_feed_generation(initial) == backend_ndl_feed_generation(current) &&
           (current & BACKEND_NDL_FEED_AUDIO_READY) != 0;
}

BackendNdlResult backend_ndl_feed_audio(BackendNdl *ctx,
                                        const void *data,
                                        size_t len,
                                        long long ndl_pts) {
    if (!ctx || !data || len == 0 || len > UINT32_MAX) {
        return BACKEND_NDL_INVALID_ARGUMENT;
    }
    if (!ctx->api.DirectAudioPlay) {
        return BACKEND_NDL_UNAVAILABLE;
    }

    uint64_t state = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if ((state & BACKEND_NDL_FEED_AUDIO_READY) == 0) {
        atomic_fetch_add_explicit(&ctx->audio_not_ready_streak, 1, memory_order_relaxed);
        return BACKEND_NDL_NOT_READY;
    }

    if (ctx->config.guard_audio_overflow && ctx->api.DirectAudioGetAvailableBufferSize) {
        int available = -1;
        if (ctx->api.DirectAudioGetAvailableBufferSize(&available) == 0) {
            uint64_t after_guard = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
            if (!backend_ndl_audio_state_ready(state, after_guard)) {
                atomic_fetch_add_explicit(&ctx->audio_not_ready_streak, 1,
                                          memory_order_relaxed);
                return BACKEND_NDL_NOT_READY;
            }
        }
        if (available >= 0 && len > (size_t)available) {
            unsigned int streak = atomic_fetch_add_explicit(&ctx->audio_overflow_streak, 1,
                                                             memory_order_relaxed) + 1;
            if (streak == 1 || streak % BACKEND_NDL_AUDIO_DEBUG_CADENCE == 0) {
                BACKEND_NDL_DEBUG_LOG(ctx, "audio overflow drop #%u: len=%zu available=%d",
                                      streak, len, available);
            }
            return BACKEND_NDL_OVERFLOW;
        }
    }

    if (ndl_pts == BACKEND_NDL_PTS_AUTO) {
        ndl_pts = backend_ndl_media_time_for_state(ctx, state);
    }
    uint64_t before_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_audio_state_ready(state, before_play)) {
        atomic_fetch_add_explicit(&ctx->audio_not_ready_streak, 1, memory_order_relaxed);
        return BACKEND_NDL_NOT_READY;
    }

    int rc = ctx->api.DirectAudioPlay((void *)data, (unsigned int)len, ndl_pts);
    uint64_t after_play = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    if (!backend_ndl_audio_state_ready(state, after_play)) {
        atomic_fetch_add_explicit(&ctx->audio_not_ready_streak, 1, memory_order_relaxed);
        return BACKEND_NDL_NOT_READY;
    }
    if (rc != 0) {
        unsigned int streak = atomic_fetch_add_explicit(&ctx->audio_error_streak, 1,
                                                         memory_order_relaxed) + 1;
        if (streak == 1 || streak % BACKEND_NDL_AUDIO_DEBUG_CADENCE == 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "NDL_DirectAudioPlay failure #%u rc=%d len=%zu error=%s "
                            "(error text best-effort)",
                            streak, rc, len, backend_ndl_last_error(ctx));
        }
        return BACKEND_NDL_ERROR;
    }
    atomic_store_explicit(&ctx->audio_error_streak, 0, memory_order_relaxed);

    unsigned int overflow = atomic_exchange_explicit(&ctx->audio_overflow_streak, 0,
                                                       memory_order_relaxed);
    unsigned int not_ready = atomic_exchange_explicit(&ctx->audio_not_ready_streak, 0,
                                                        memory_order_relaxed);
    if (overflow || not_ready) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                        "audio sink recovered: dropped %u block(s) on overflow, %u while not ready",
                        overflow, not_ready);
    }

    unsigned long long count = atomic_fetch_add_explicit(&ctx->audio_feed_count, 1,
                                                          memory_order_relaxed) + 1;
    if (backend_ndl_log_enabled(ctx, BACKEND_NDL_LOG_DEBUG) &&
        count % BACKEND_NDL_AUDIO_DEBUG_CADENCE == 0 &&
        ctx->api.DirectAudioGetAvailableBufferSize) {
        int available = -1;
        int total = -1;
        if (ctx->api.DirectAudioGetAvailableBufferSize(&available) == 0) {
            if (ctx->api.DirectAudioGetTotalBufferSize) {
                (void)ctx->api.DirectAudioGetTotalBufferSize(&total);
            }
            BACKEND_NDL_DEBUG_LOG(ctx, "audio sink: fed=%llu available=%d total=%d pts=%lld",
                                  count, available, total, ndl_pts);
        }
    }
    return BACKEND_NDL_OK;
}

BackendNdlResult backend_ndl_get_audio_available(BackendNdl *ctx, int *available_bytes) {
    return backend_ndl_opt_get_int(ctx,
                                   ctx ? ctx->api.DirectAudioGetAvailableBufferSize : NULL,
                                   available_bytes);
}

BackendNdlResult backend_ndl_get_audio_total(BackendNdl *ctx, int *total_bytes) {
    return backend_ndl_opt_get_int(ctx, ctx ? ctx->api.DirectAudioGetTotalBufferSize : NULL,
                                   total_bytes);
}

BackendNdlResult backend_ndl_get_multichannel_status(BackendNdl *ctx,
                                                     BackendNdlMultichannelStatus *status) {
    if (!ctx || !status) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY) == 0) {
        return BACKEND_NDL_UNAVAILABLE;
    }
    pthread_mutex_lock(&ctx->lifecycle_lock);
    int raw = -1;
    int rc = ctx->api.DirectAudioSupportMultiChannel(&raw);
    if (rc == 0) {
        *status = backend_ndl_multichannel_from_query(raw);
        atomic_store_explicit(&ctx->multichannel_status, *status, memory_order_release);
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_effect_load(BackendNdl *ctx,
                                        const BackendNdlPcmInfo *pcm,
                                        uint32_t *preferred_size) {
    if (!ctx || !pcm || !preferred_size) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_EFFECT) == 0) return BACKEND_NDL_UNAVAILABLE;
    if (pcm->channels == 6 &&
        atomic_load_explicit(&ctx->multichannel_status, memory_order_acquire) !=
            BACKEND_NDL_MULTICHANNEL_READY) {
        return BACKEND_NDL_UNSUPPORTED;
    }
    NDL_DIRECTAUDIO_PCM_INFO_T tv_pcm;
    BackendNdlResult fill = backend_ndl_fill_pcm_info(pcm, &tv_pcm);
    if (fill != BACKEND_NDL_OK) return fill;

    pthread_mutex_lock(&ctx->lifecycle_lock);
    if (ctx->effect_loaded) {
        (void)ctx->api.DirectEffectUnload();
        ctx->effect_loaded = false;
    }
    unsigned int preferred = 0;
    int rc = ctx->api.DirectEffectLoad(&tv_pcm, &preferred);
    if (rc == 0) {
        ctx->effect_loaded = true;
        ctx->effect_pcm = *pcm;
        *preferred_size = preferred;
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_effect_feed(BackendNdl *ctx, const void *data, size_t len) {
    if (!ctx || !data || len == 0 || len > UINT32_MAX) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_EFFECT) == 0) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    if (!ctx->effect_loaded) {
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return BACKEND_NDL_NOT_READY;
    }
    unsigned int available = UINT_MAX;
    if (ctx->api.DirectEffectGetAvailableBufferSize(&available) == 0 && len > available) {
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return BACKEND_NDL_OVERFLOW;
    }
    int rc = ctx->api.DirectEffectPlay((void *)data, (unsigned int)len);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_get_effect_available(BackendNdl *ctx, uint32_t *available_bytes) {
    if (!ctx || !available_bytes) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_EFFECT) == 0) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    unsigned int available = 0;
    int rc = ctx->api.DirectEffectGetAvailableBufferSize(&available);
    if (rc == 0) *available_bytes = available;
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_effect_unload(BackendNdl *ctx) {
    if (!ctx) return BACKEND_NDL_INVALID_ARGUMENT;
    if ((ctx->capabilities & BACKEND_NDL_CAP_EFFECT) == 0) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    int rc = 0;
    if (ctx->effect_loaded) {
        rc = ctx->api.DirectEffectUnload();
        if (rc == 0) ctx->effect_loaded = false;
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

bool backend_ndl_pcm_sample_rate_supported(uint32_t hz) {
    return backend_ndl_pcm_sample_rate(BACKEND_NDL_ABI_V2_WINDOW_ID_INIT, hz) != 0;
}

#else

#include <backend_ndl/backend_ndl.h>

BackendNdlResult backend_ndl_feed_audio(BackendNdl *ctx, const void *data, size_t len,
                                        long long ndl_pts) {
    (void)ctx; (void)data; (void)len; (void)ndl_pts; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_get_audio_available(BackendNdl *ctx, int *available_bytes) {
    (void)ctx; (void)available_bytes; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_get_audio_total(BackendNdl *ctx, int *total_bytes) {
    (void)ctx; (void)total_bytes; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_get_multichannel_status(BackendNdl *ctx,
                                                     BackendNdlMultichannelStatus *status) {
    (void)ctx; (void)status; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_effect_load(BackendNdl *ctx, const BackendNdlPcmInfo *pcm,
                                        uint32_t *preferred_size) {
    (void)ctx; (void)pcm; (void)preferred_size; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_effect_feed(BackendNdl *ctx, const void *data, size_t len) {
    (void)ctx; (void)data; (void)len; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_get_effect_available(BackendNdl *ctx, uint32_t *available_bytes) {
    (void)ctx; (void)available_bytes; return BACKEND_NDL_UNAVAILABLE;
}
BackendNdlResult backend_ndl_effect_unload(BackendNdl *ctx) {
    (void)ctx; return BACKEND_NDL_UNAVAILABLE;
}
bool backend_ndl_pcm_sample_rate_supported(uint32_t hz) { (void)hz; return false; }

#endif

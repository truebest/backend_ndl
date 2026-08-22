/* SPDX-License-Identifier: MIT */
/* DirectMedia facade loading and the serialized control plane. FeedAudio and
 * FeedVideo live in their track files and never acquire lifecycle_lock. */
#define _GNU_SOURCE

#include "backend_ndl_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dlfcn.h>

#define BACKEND_NDL_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

void backend_ndl_config_defaults(BackendNdlConfig *config) {
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->minimum_log_level = BACKEND_NDL_LOG_INFO;
    config->guard_audio_overflow = true;
    config->video_frame_drop_threshold = -1;
}

const char *backend_ndl_abi_profile_name(BackendNdlAbiProfile profile) {
    switch (profile) {
        case BACKEND_NDL_ABI_V2_CALLBACK_INIT:
            return "v2-callback-init";
        case BACKEND_NDL_ABI_V2_WINDOW_ID_INIT:
            return "v2-window-id-init";
        case BACKEND_NDL_ABI_UNKNOWN:
        default:
            return "unknown";
    }
}

static unsigned int backend_ndl_sdk_major(const char *version) {
    if (!version) {
        return 0;
    }
    while (isspace((unsigned char)*version)) {
        version++;
    }
    if (*version < '0' || *version > '9') {
        return 0;
    }
    unsigned int value = 0;
    do {
        unsigned int digit = (unsigned int)(*version - '0');
        if (value > (UINT_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
        version++;
    } while (*version >= '0' && *version <= '9');
    if (*version != '\0' && *version != '.') {
        return 0;
    }
    return value;
}

const char *backend_ndl_result_name(BackendNdlResult result) {
    switch (result) {
        case BACKEND_NDL_OK:
            return "OK";
        case BACKEND_NDL_ERROR:
            return "ERROR";
        case BACKEND_NDL_UNAVAILABLE:
            return "UNAVAILABLE";
        case BACKEND_NDL_UNSUPPORTED:
            return "UNSUPPORTED";
        case BACKEND_NDL_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case BACKEND_NDL_NOT_READY:
            return "NOT_READY";
        case BACKEND_NDL_OVERFLOW:
            return "OVERFLOW";
        case BACKEND_NDL_NEED_KEYFRAME:
            return "NEED_KEYFRAME";
        default:
            return "UNKNOWN";
    }
}

const char *backend_ndl_log_level_name(BackendNdlLogLevel level) {
    switch (level) {
        case BACKEND_NDL_LOG_DEBUG:
            return "DEBUG";
        case BACKEND_NDL_LOG_INFO:
            return "INFO";
        case BACKEND_NDL_LOG_WARN:
            return "WARN";
        case BACKEND_NDL_LOG_ERROR:
            return "ERROR";
        case BACKEND_NDL_LOG_OFF:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

static void backend_ndl_vlog_config(const BackendNdlConfig *config,
                                    BackendNdlLogLevel level,
                                    const char *format,
                                    va_list args) {
    if (!config || !config->log_fn || !format || level < config->minimum_log_level ||
        config->minimum_log_level >= BACKEND_NDL_LOG_OFF) {
        return;
    }
    char message[1024];
    int written = vsnprintf(message, sizeof(message), format, args);
    if (written < 0) {
        (void)snprintf(message, sizeof(message), "<format-error>");
    } else if ((size_t)written >= sizeof(message)) {
        static const char marker[] = " <truncated>";
        memcpy(message + sizeof(message) - sizeof(marker), marker, sizeof(marker));
    }
    for (char *p = message; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
    config->log_fn(config->userdata, level, message);
}

static void backend_ndl_log_config(const BackendNdlConfig *config,
                                   BackendNdlLogLevel level,
                                   const char *format,
                                   ...) {
    va_list args;
    va_start(args, format);
    backend_ndl_vlog_config(config, level, format, args);
    va_end(args);
}

pthread_mutex_t g_backend_ndl_registry_lock = PTHREAD_MUTEX_INITIALIZER;
BackendNdl *g_backend_ndl_active = NULL;

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
static bool g_backend_ndl_test_force_process_symbols;

void backend_ndl_test_force_process_symbols(bool enabled) {
    g_backend_ndl_test_force_process_symbols = enabled;
}
#endif

/* Unload/Quit cached for backend_ndl_emergency_release(): written on the
 * open/close control path, consumed lock-free from fatal-signal context where
 * dereferencing the (possibly mid-teardown) context would be unsafe. Atomics
 * are only signal-safe when lock-free; function pointers share the object
 * pointer representation on every dlsym-capable platform. */
#if ATOMIC_POINTER_LOCK_FREE != 2
#error "backend_ndl requires lock-free atomic pointers for the emergency release"
#endif
typedef int (*BackendNdlEmergencyFn)(void);
static _Atomic(BackendNdlEmergencyFn) g_backend_ndl_emergency_unload;
static _Atomic(BackendNdlEmergencyFn) g_backend_ndl_emergency_quit;

bool backend_ndl_emergency_release(void) {
    /* Claiming Quit first makes the release once-only across racing callers. */
    BackendNdlEmergencyFn quit =
        atomic_exchange_explicit(&g_backend_ndl_emergency_quit, NULL, memory_order_acq_rel);
    if (!quit) {
        return false;
    }
    BackendNdlEmergencyFn unload =
        atomic_exchange_explicit(&g_backend_ndl_emergency_unload, NULL, memory_order_acq_rel);
    if (unload) {
        (void)unload();
    }
    (void)quit();
    return true;
}

void backend_ndl_log(BackendNdl *ctx, BackendNdlLogLevel level, const char *format, ...) {
    if (!ctx) {
        return;
    }
    va_list args;
    va_start(args, format);
    backend_ndl_vlog_config(&ctx->config, level, format, args);
    va_end(args);
}

bool backend_ndl_log_enabled(const BackendNdl *ctx, BackendNdlLogLevel level) {
    return ctx && ctx->config.log_fn && level >= ctx->config.minimum_log_level &&
           ctx->config.minimum_log_level < BACKEND_NDL_LOG_OFF;
}

static char *backend_ndl_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

const char *backend_ndl_last_error(const BackendNdl *ctx) {
    if (!ctx || !ctx->api.DirectMediaGetError) {
        return "(NDL error unavailable)";
    }
    const char *error = ctx->api.DirectMediaGetError();
    return error ? error : "(NDL returned no error text)";
}

long long backend_ndl_monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

long long backend_ndl_monotonic_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (long long)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

uint64_t backend_ndl_feed_generation(uint64_t state) {
    return state >> BACKEND_NDL_FEED_GENERATION_SHIFT;
}

long long backend_ndl_media_time_for_state(const BackendNdl *ctx, uint64_t state) {
    if (!ctx || backend_ndl_feed_generation(state) == 0 ||
        (state & (BACKEND_NDL_FEED_VIDEO_READY | BACKEND_NDL_FEED_AUDIO_READY)) == 0) {
        return 0;
    }
    long long start = atomic_load_explicit(&ctx->loaded_start_ms, memory_order_acquire);
    long long now = backend_ndl_monotonic_ms();
    return now > start ? now - start : 0;
}

static void *backend_ndl_sym(BackendNdl *ctx, void *handle, const char *name, bool required) {
    dlerror();
    void *symbol = dlsym(handle, name);
    const char *error = dlerror();
    if (!symbol && required) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR, "missing required symbol %s: %s", name,
                        error ? error : "not found");
    }
    return symbol;
}

static bool backend_ndl_resolve_api(BackendNdl *ctx) {
#define REQ(name) do { \
        ctx->api.name = (__typeof__(ctx->api.name))backend_ndl_sym(ctx, ctx->dl_handle, "NDL_" #name, true); \
        if (!ctx->api.name) { return false; } \
    } while (0)
#define OPT(name) do { \
        ctx->api.name = (__typeof__(ctx->api.name))backend_ndl_sym(ctx, ctx->dl_handle, "NDL_" #name, false); \
        if (!ctx->api.name) { \
            BACKEND_NDL_DEBUG_LOG(ctx, "optional symbol NDL_" #name " not present"); \
        } \
    } while (0)

    REQ(DirectMediaGetError);
    void *init_symbol = backend_ndl_sym(ctx, ctx->dl_handle, "NDL_DirectMediaInit", true);
    void *load_symbol = backend_ndl_sym(ctx, ctx->dl_handle, "NDL_DirectMediaLoad", true);
    if (!init_symbol || !load_symbol) {
        return false;
    }
    OPT(DirectMediaSetWindowId);
    REQ(DirectMediaQuit);
    REQ(DirectMediaUnload);

    if (ctx->api.DirectMediaSetWindowId) {
        ctx->abi_profile = BACKEND_NDL_ABI_V2_WINDOW_ID_INIT;
        ctx->api.DirectMediaInitCurrent =
            (__typeof__(ctx->api.DirectMediaInitCurrent))init_symbol;
        ctx->api.DirectMediaLoadCurrent =
            (__typeof__(ctx->api.DirectMediaLoadCurrent))load_symbol;
    } else {
        ctx->abi_profile = BACKEND_NDL_ABI_V2_CALLBACK_INIT;
        ctx->api.DirectMediaInitCallback =
            (__typeof__(ctx->api.DirectMediaInitCallback))init_symbol;
        ctx->api.DirectMediaLoadCallback =
            (__typeof__(ctx->api.DirectMediaLoadCallback))load_symbol;
    }

    OPT(DirectMediaSetAppState);
    OPT(DirectAudioPlay);
    OPT(DirectAudioGetTotalBufferSize);
    OPT(DirectAudioGetAvailableBufferSize);
    OPT(DirectVideoPlay);
    OPT(DirectVideoSetArea);
    OPT(DirectVideoSetFrameDropThreshold);
    OPT(DirectVideoGetRenderBufferLength);
    OPT(DirectVideoFlushRenderBuffer);
    if (ctx->abi_profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT) {
        OPT(DirectAudioSupportMultiChannel);
        OPT(DirectAudioRegisterCallback);
        OPT(DirectVideoSetHDRInfo);
        OPT(DirectEffectLoad);
        OPT(DirectEffectUnload);
        OPT(DirectEffectPlay);
        OPT(DirectEffectGetAvailableBufferSize);
    }
#undef REQ
#undef OPT
    BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "detected DirectMedia ABI profile: %s",
                    backend_ndl_abi_profile_name(ctx->abi_profile));
    return true;
}

static BackendNdlCapabilities backend_ndl_detect_capabilities(const BackendNdlApi *api,
                                                              BackendNdlAbiProfile profile) {
    BackendNdlCapabilities caps = 0;
    if (api->DirectMediaSetAppState) caps |= BACKEND_NDL_CAP_APP_STATE;
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT && api->DirectMediaSetWindowId) {
        caps |= BACKEND_NDL_CAP_WINDOW_ID;
    }
    if (api->DirectVideoPlay) caps |= BACKEND_NDL_CAP_VIDEO_PLAY;
    if (api->DirectVideoSetArea) caps |= BACKEND_NDL_CAP_VIDEO_AREA;
    if (api->DirectVideoSetFrameDropThreshold) caps |= BACKEND_NDL_CAP_VIDEO_FRAME_DROP;
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT && api->DirectVideoSetHDRInfo) {
        caps |= BACKEND_NDL_CAP_VIDEO_HDR;
    }
    if (api->DirectVideoGetRenderBufferLength) caps |= BACKEND_NDL_CAP_VIDEO_QUEUE_LENGTH;
    if (api->DirectVideoFlushRenderBuffer) caps |= BACKEND_NDL_CAP_VIDEO_FLUSH;
    if (api->DirectAudioPlay) caps |= BACKEND_NDL_CAP_AUDIO_PLAY;
    if (api->DirectAudioGetAvailableBufferSize) caps |= BACKEND_NDL_CAP_AUDIO_AVAILABLE;
    if (api->DirectAudioGetTotalBufferSize) caps |= BACKEND_NDL_CAP_AUDIO_TOTAL;
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT && api->DirectAudioSupportMultiChannel) {
        caps |= BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY;
    }
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT && api->DirectAudioRegisterCallback) {
        caps |= BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_CALLBACK;
    }
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT && api->DirectEffectLoad &&
        api->DirectEffectUnload && api->DirectEffectPlay &&
        api->DirectEffectGetAvailableBufferSize) {
        caps |= BACKEND_NDL_CAP_EFFECT;
    }
    return caps;
}

static bool backend_ndl_open_library(BackendNdl *ctx) {
    static const char *const candidates[] = {
        "libNDL_directmedia.so.1",
        "libNDL_directmedia.so.1.0.0",
        "libNDL_directmedia.so",
    };

    if (ctx->config.library_path && ctx->config.library_path[0]) {
        ctx->dl_handle = dlopen(ctx->config.library_path, RTLD_NOW | RTLD_LOCAL);
        if (!ctx->dl_handle) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR, "dlopen(%s) failed: %s",
                            ctx->config.library_path, dlerror());
            return false;
        }
        ctx->owns_dl_handle = true;
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "loaded NDL library: %s",
                        ctx->config.library_path);
        return true;
    }

    bool try_sonames = true;
#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
    try_sonames = !g_backend_ndl_test_force_process_symbols;
#endif
    if (try_sonames) {
        for (size_t i = 0; i < BACKEND_NDL_ARRAY_LEN(candidates); i++) {
            ctx->dl_handle = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
            if (ctx->dl_handle) {
                ctx->owns_dl_handle = true;
                BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                                "loaded NDL library: %s", candidates[i]);
                return true;
            }
            BACKEND_NDL_DEBUG_LOG(ctx, "dlopen(%s) failed: %s", candidates[i],
                                  dlerror());
        }
    }

    ctx->dl_handle = RTLD_DEFAULT;
    ctx->owns_dl_handle = false;
    if (backend_ndl_sym(ctx, ctx->dl_handle, "NDL_DirectMediaGetError", false)) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                        "using DirectMedia facade from process-global symbols");
        return true;
    }
    ctx->dl_handle = NULL;
    BACKEND_NDL_LOG(
        ctx, BACKEND_NDL_LOG_ERROR,
        "could not find a DirectMedia facade in known sonames or process-global symbols");
    return false;
}

static void backend_ndl_load_callback(int type, long long number, const char *text) {
    pthread_mutex_lock(&g_backend_ndl_registry_lock);
    BackendNdl *ctx = g_backend_ndl_active;
    switch (type) {
        case 0x16:
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "load callback: LOADCOMPLETED %s",
                            text ? text : "");
            break;
        case 0x17:
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "load callback: UNLOADCOMPLETED %s",
                            text ? text : "");
            break;
        case 0x1a:
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "load callback: PLAYING %s",
                            text ? text : "");
            break;
        default:
            BACKEND_NDL_DEBUG_LOG(ctx, "load callback: type=%d number=%lld text=%s", type,
                                  number, text ? text : "");
            break;
    }
    if (ctx && ctx->config.media_event_fn) {
        ctx->config.media_event_fn(ctx->config.userdata, type, (int64_t)number, text);
    }
    pthread_mutex_unlock(&g_backend_ndl_registry_lock);
}

static uint64_t backend_ndl_begin_generation_locked(BackendNdl *ctx,
                                                    uint64_t *previous_state) {
    const uint64_t max_generation = UINT64_MAX >> BACKEND_NDL_FEED_GENERATION_SHIFT;
    ctx->reload_generation =
        ctx->reload_generation >= max_generation ? 1 : ctx->reload_generation + 1;
    uint64_t state = ctx->reload_generation << BACKEND_NDL_FEED_GENERATION_SHIFT;
    uint64_t previous =
        atomic_exchange_explicit(&ctx->feed_state, state, memory_order_acq_rel);
    if (previous_state) {
        *previous_state = previous;
    }
    return state;
}

static bool backend_ndl_media_has_tracks(const BackendNdlMediaConfig *media) {
    return media->has_video || media->has_audio;
}

static BackendNdlResult backend_ndl_build_media_info(
    const BackendNdl *ctx,
    const BackendNdlMediaConfig *media,
    BackendNdlMediaInfo *out) {
    if (!backend_ndl_media_has_tracks(media)) {
        return BACKEND_NDL_NOT_READY;
    }
    memset(out, 0, sizeof(*out));
    if (media->has_video) {
        BackendNdlResult result =
            backend_ndl_video_fill_media_info(ctx->abi_profile, &media->video, out);
        if (result != BACKEND_NDL_OK) return result;
    }
    if (media->has_audio) {
        BackendNdlResult result =
            backend_ndl_audio_fill_media_info(ctx->abi_profile, &media->audio, out);
        if (result != BACKEND_NDL_OK) return result;
    }
    return BACKEND_NDL_OK;
}

static uint64_t backend_ndl_loaded_feed_flags(const BackendNdl *ctx,
                                              const BackendNdlMediaConfig *media) {
    uint64_t flags = 0;
    if (media->has_video) flags |= BACKEND_NDL_FEED_VIDEO_READY;
    if (media->has_audio) flags |= BACKEND_NDL_FEED_AUDIO_READY;
    if (media->has_video && ctx->config.require_keyframe_after_reload) {
        flags |= BACKEND_NDL_FEED_NEED_KEYFRAME;
    }
    return flags;
}

static BackendNdlResult backend_ndl_unload_locked(BackendNdl *ctx) {
    if (!ctx->media_loaded) {
        return BACKEND_NDL_OK;
    }
    int rc = ctx->api.DirectMediaUnload();
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                        "NDL_DirectMediaUnload failed rc=%d error=%s", rc,
                        backend_ndl_last_error(ctx));
        return BACKEND_NDL_ERROR;
    }
    ctx->media_loaded = false;
    atomic_store_explicit(&ctx->loaded_start_ms, 0, memory_order_release);
    BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO, "media unloaded");
    return BACKEND_NDL_OK;
}

static BackendNdlResult backend_ndl_load_media_info_locked(
    BackendNdl *ctx,
    BackendNdlMediaInfo *info,
    const char *operation) {
    int rc;
    backend_ndl_video_before_load_locked(ctx);
    if (ctx->abi_profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT) {
        rc = ctx->api.DirectMediaLoadCurrent(&info->current, backend_ndl_load_callback);
    } else {
        rc = ctx->api.DirectMediaLoadCallback(&info->callback, backend_ndl_load_callback);
    }
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                        "NDL_DirectMediaLoad (%s) failed rc=%d error=%s", operation, rc,
                        backend_ndl_last_error(ctx));
        return BACKEND_NDL_ERROR;
    }
    return BACKEND_NDL_OK;
}

static BackendNdlResult backend_ndl_publish_loaded_locked(
    BackendNdl *ctx,
    const BackendNdlMediaConfig *media,
    uint64_t transition_state) {
    ctx->media = *media;
    ctx->media_loaded = true;
    atomic_store_explicit(&ctx->loaded_start_ms, backend_ndl_monotonic_ms(),
                          memory_order_release);
    backend_ndl_video_after_load_locked(ctx);
    BackendNdlResult audio_result = backend_ndl_audio_after_load_locked(ctx);
    if (audio_result != BACKEND_NDL_OK) {
        BackendNdlResult unload_result = backend_ndl_unload_locked(ctx);
        if (unload_result != BACKEND_NDL_OK) {
            BACKEND_NDL_LOG(
                ctx, BACKEND_NDL_LOG_ERROR,
                "could not unload media after post-load setup failed: %s",
                backend_ndl_result_name(unload_result));
        }
        return audio_result;
    }

    backend_ndl_video_reset_runtime_stats(ctx);

    uint64_t flags = backend_ndl_loaded_feed_flags(ctx, media);
    atomic_store_explicit(&ctx->feed_state, transition_state | flags, memory_order_release);
    BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                    "media loaded: generation=%llu video=%s audio=%s",
                    (unsigned long long)ctx->reload_generation,
                    media->has_video ? "yes" : "no", media->has_audio ? "yes" : "no");
    return BACKEND_NDL_OK;
}

static BackendNdlResult backend_ndl_replace_media_locked(
    BackendNdl *ctx,
    const BackendNdlMediaConfig *next) {
    if (!ctx->ndl_initialized) {
        return BACKEND_NDL_NOT_READY;
    }

    BackendNdlMediaInfo next_info;
    if (backend_ndl_media_has_tracks(next)) {
        BackendNdlResult build_result = backend_ndl_build_media_info(ctx, next, &next_info);
        if (build_result != BACKEND_NDL_OK) {
            return build_result;
        }
    }

    BackendNdlMediaConfig previous = ctx->media;
    bool previous_loaded = ctx->media_loaded;
    BackendNdlMediaInfo previous_info;
    if (previous_loaded) {
        BackendNdlResult build_result =
            backend_ndl_build_media_info(ctx, &previous, &previous_info);
        if (build_result != BACKEND_NDL_OK) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "stored media configuration is invalid: %s",
                            backend_ndl_result_name(build_result));
            return build_result;
        }
    }

    uint64_t previous_state = 0;
    uint64_t transition_state =
        backend_ndl_begin_generation_locked(ctx, &previous_state);
    BackendNdlResult unload_result = backend_ndl_unload_locked(ctx);
    if (unload_result != BACKEND_NDL_OK) {
        uint64_t previous_flags = previous_state & BACKEND_NDL_FEED_FLAG_MASK;
        atomic_store_explicit(&ctx->feed_state, transition_state | previous_flags,
                              memory_order_release);
        return unload_result;
    }

    if (!backend_ndl_media_has_tracks(next)) {
        ctx->media = *next;
        return BACKEND_NDL_OK;
    }

    BackendNdlResult load_result =
        backend_ndl_load_media_info_locked(ctx, &next_info, "reconfigure");
    if (load_result == BACKEND_NDL_OK) {
        load_result =
            backend_ndl_publish_loaded_locked(ctx, next, transition_state);
        if (load_result == BACKEND_NDL_OK) {
            return BACKEND_NDL_OK;
        }
    }

    if (!ctx->media_loaded) {
        ctx->media = previous;
    }
    if (previous_loaded && !ctx->media_loaded) {
        BackendNdlResult restore_result =
            backend_ndl_load_media_info_locked(ctx, &previous_info, "restore");
        if (restore_result == BACKEND_NDL_OK) {
            restore_result =
                backend_ndl_publish_loaded_locked(ctx, &previous, transition_state);
            if (restore_result == BACKEND_NDL_OK) {
                BACKEND_NDL_LOG(
                    ctx, BACKEND_NDL_LOG_INFO,
                    "restored previous media configuration after failed reconfigure");
            }
        }
        if (restore_result != BACKEND_NDL_OK) {
            ctx->media = previous;
            atomic_store_explicit(&ctx->feed_state, transition_state,
                                  memory_order_release);
            BACKEND_NDL_LOG(
                ctx, BACKEND_NDL_LOG_ERROR,
                "could not restore previous media configuration after failed reconfigure: %s",
                backend_ndl_result_name(restore_result));
        }
    } else if (ctx->media_loaded) {
        atomic_store_explicit(&ctx->feed_state, transition_state,
                              memory_order_release);
        BACKEND_NDL_LOG(
            ctx, BACKEND_NDL_LOG_ERROR,
            "failed replacement remains loaded but disabled after teardown failure");
    } else {
        atomic_store_explicit(&ctx->feed_state, transition_state,
                              memory_order_release);
    }
    return load_result;
}

static bool backend_ndl_video_info_equal(const BackendNdlVideoInfo *left,
                                         const BackendNdlVideoInfo *right) {
    return left->codec == right->codec && left->width == right->width &&
           left->height == right->height;
}

static BackendNdlResult backend_ndl_reload_locked(BackendNdl *ctx) {
    BackendNdlMediaConfig media = ctx->media;
    return backend_ndl_replace_media_locked(ctx, &media);
}

static bool backend_ndl_api_has_required_profile(const BackendNdlApi *api,
                                                 BackendNdlAbiProfile profile) {
    if (!api || !api->DirectMediaGetError || !api->DirectMediaQuit ||
        !api->DirectMediaUnload) {
        return false;
    }
    if (profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT) {
        return api->DirectMediaSetWindowId && api->DirectMediaInitCurrent &&
               api->DirectMediaLoadCurrent;
    }
    if (profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT) {
        return !api->DirectMediaSetWindowId && api->DirectMediaInitCallback &&
               api->DirectMediaLoadCallback;
    }
    return false;
}

static BackendNdl *backend_ndl_open_common(const BackendNdlConfig *config,
                                           BackendNdlAbiProfile injected_profile,
                                           const BackendNdlApi *api) {
    if (!config || !config->app_id || !config->app_id[0] ||
        config->minimum_log_level < BACKEND_NDL_LOG_DEBUG ||
        config->minimum_log_level > BACKEND_NDL_LOG_OFF) {
        backend_ndl_log_config(config, BACKEND_NDL_LOG_ERROR,
                               "config with a non-empty app_id is required");
        return NULL;
    }

    unsigned int sdk_major = backend_ndl_sdk_major(config->webos_sdk_version);
    if (sdk_major > 0 && sdk_major < 5) {
        backend_ndl_log_config(config, BACKEND_NDL_LOG_ERROR,
                               "webOS SDK %u is unsupported; DirectMedia v2 requires SDK 5+",
                               sdk_major);
        return NULL;
    }

    pthread_mutex_lock(&g_backend_ndl_registry_lock);
    bool already_open = g_backend_ndl_active != NULL;
    pthread_mutex_unlock(&g_backend_ndl_registry_lock);
    if (already_open) {
        backend_ndl_log_config(config, BACKEND_NDL_LOG_ERROR,
                               "backend already open; DirectMedia is process-global");
        return NULL;
    }

    BackendNdl *ctx = (BackendNdl *)calloc(1, sizeof(*ctx));
    if (!ctx || pthread_mutex_init(&ctx->lifecycle_lock, NULL) != 0) {
        free(ctx);
        return NULL;
    }
    ctx->config = *config;
    atomic_init(&ctx->feed_state, 0);
    atomic_init(&ctx->loaded_start_ms, 0);
    atomic_init(&ctx->audio_feed_count, 0);
    atomic_init(&ctx->video_feed_count, 0);
    atomic_init(&ctx->audio_overflow_streak, 0);
    atomic_init(&ctx->audio_not_ready_streak, 0);
    atomic_init(&ctx->audio_error_streak, 0);
    atomic_init(&ctx->video_error_streak, 0);
    atomic_init(&ctx->video_play_samples, 0);
    atomic_init(&ctx->video_play_us_ewma, 0);
    atomic_init(&ctx->video_play_us_max, 0);
    atomic_init(&ctx->video_play_errors, 0);
    atomic_init(&ctx->video_stats_epoch, 0);
    atomic_init(&ctx->video_stats_writers, 0);
    atomic_init(&ctx->video_stats_version, 0);
    atomic_init(&ctx->multichannel_status, BACKEND_NDL_MULTICHANNEL_UNKNOWN);

    ctx->app_id = backend_ndl_strdup(config->app_id);
    ctx->window_id = backend_ndl_strdup(config->window_id);
    ctx->webos_sdk_version = backend_ndl_strdup(config->webos_sdk_version);
    ctx->webos_sdk_major = sdk_major;
    if (!ctx->app_id || (config->window_id && !ctx->window_id) ||
        (config->webos_sdk_version && !ctx->webos_sdk_version)) {
        free(ctx->webos_sdk_version);
        free(ctx->window_id);
        free(ctx->app_id);
        pthread_mutex_destroy(&ctx->lifecycle_lock);
        free(ctx);
        return NULL;
    }
    ctx->config.app_id = ctx->app_id;
    ctx->config.window_id = ctx->window_id;
    ctx->config.webos_sdk_version = ctx->webos_sdk_version;

    pthread_mutex_lock(&g_backend_ndl_registry_lock);
    if (g_backend_ndl_active) {
        pthread_mutex_unlock(&g_backend_ndl_registry_lock);
        free(ctx->webos_sdk_version);
        free(ctx->window_id);
        free(ctx->app_id);
        pthread_mutex_destroy(&ctx->lifecycle_lock);
        free(ctx);
        return NULL;
    }
    g_backend_ndl_active = ctx;
    pthread_mutex_unlock(&g_backend_ndl_registry_lock);

    if (api) {
        ctx->api = *api;
        ctx->abi_profile = injected_profile;
        if (!backend_ndl_api_has_required_profile(&ctx->api, ctx->abi_profile)) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "open_with_api: incomplete %s profile",
                            backend_ndl_abi_profile_name(ctx->abi_profile));
            backend_ndl_close(ctx);
            return NULL;
        }
    } else if (!backend_ndl_open_library(ctx) || !backend_ndl_resolve_api(ctx)) {
        backend_ndl_close(ctx);
        return NULL;
    }
    ctx->capabilities = backend_ndl_detect_capabilities(&ctx->api, ctx->abi_profile);

    if (ctx->window_id && ctx->window_id[0]) {
        if (ctx->abi_profile != BACKEND_NDL_ABI_V2_WINDOW_ID_INIT ||
            !ctx->api.DirectMediaSetWindowId) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "window_id is unsupported by DirectMedia profile %s",
                            backend_ndl_abi_profile_name(ctx->abi_profile));
            backend_ndl_close(ctx);
            return NULL;
        }
        int window_rc = ctx->api.DirectMediaSetWindowId(ctx->window_id);
        if (window_rc != 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "NDL_DirectMediaSetWindowId failed rc=%d error=%s", window_rc,
                            backend_ndl_last_error(ctx));
            backend_ndl_close(ctx);
            return NULL;
        }
    }

    int rc;
    if (ctx->abi_profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT) {
        rc = ctx->api.DirectMediaInitCurrent(ctx->app_id);
    } else {
        rc = ctx->api.DirectMediaInitCallback(ctx->app_id, NULL);
    }
    if (rc != 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                        "NDL_DirectMediaInit(%s) failed rc=%d error=%s", ctx->app_id, rc,
                        backend_ndl_last_error(ctx));
        backend_ndl_close(ctx);
        return NULL;
    }
    ctx->ndl_initialized = true;
    atomic_store_explicit(&g_backend_ndl_emergency_unload, ctx->api.DirectMediaUnload,
                          memory_order_release);
    atomic_store_explicit(&g_backend_ndl_emergency_quit, ctx->api.DirectMediaQuit,
                          memory_order_release);
    backend_ndl_audio_register_capability_callback(ctx);
    if (ctx->webos_sdk_version && ctx->webos_sdk_major == 0) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                        "unrecognized webOS sdkVersion=%s; relying on ABI profile",
                        ctx->webos_sdk_version);
    } else if (ctx->webos_sdk_major > 11) {
        BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                        "future/unqualified webOS sdkVersion=%s; relying on ABI profile",
                        ctx->webos_sdk_version);
    }
    BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_INFO,
                    "initialized app_id=%s sdk=%s profile=%s capabilities=0x%llx",
                    ctx->app_id, ctx->webos_sdk_version ? ctx->webos_sdk_version : "unknown",
                    backend_ndl_abi_profile_name(ctx->abi_profile),
                    (unsigned long long)ctx->capabilities);
    return ctx;
}

BackendNdl *backend_ndl_open(const BackendNdlConfig *config) {
    return backend_ndl_open_common(config, BACKEND_NDL_ABI_UNKNOWN, NULL);
}

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
BackendNdl *backend_ndl_open_with_api(const BackendNdlConfig *config,
                                      BackendNdlAbiProfile profile,
                                      const BackendNdlApi *api) {
    return api ? backend_ndl_open_common(config, profile, api) : NULL;
}
#endif

void backend_ndl_close(BackendNdl *ctx) {
    if (!ctx) return;

    pthread_mutex_lock(&g_backend_ndl_registry_lock);
    if (g_backend_ndl_active == ctx) g_backend_ndl_active = NULL;
    pthread_mutex_unlock(&g_backend_ndl_registry_lock);

    pthread_mutex_lock(&ctx->lifecycle_lock);
    bool facade_owned = true;
    if (ctx->ndl_initialized) {
        (void)backend_ndl_begin_generation_locked(ctx, NULL);
        /* Take facade ownership through the same claim the emergency release
         * uses. Losing it means a dying-process handler is (or was) inside
         * Unload/Quit: skip every further facade call and keep the library
         * mapped so that handler never executes unmapped code. */
        BackendNdlEmergencyFn quit_fn = atomic_exchange_explicit(
            &g_backend_ndl_emergency_quit, NULL, memory_order_acq_rel);
        atomic_store_explicit(&g_backend_ndl_emergency_unload, NULL, memory_order_release);
        facade_owned = quit_fn != NULL;
        if (!facade_owned) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_WARN,
                            "emergency release claimed DirectMedia; skipping facade teardown");
            ctx->effect_loaded = false;
            ctx->media_loaded = false;
            ctx->ndl_initialized = false;
        }
    }
    (void)backend_ndl_effect_unload_locked(ctx);
    if (ctx->media_loaded) (void)backend_ndl_unload_locked(ctx);
    if (ctx->ndl_initialized) {
        int rc = ctx->api.DirectMediaQuit();
        if (rc != 0) {
            BACKEND_NDL_LOG(ctx, BACKEND_NDL_LOG_ERROR,
                            "NDL_DirectMediaQuit failed rc=%d error=%s", rc,
                            backend_ndl_last_error(ctx));
        }
        ctx->ndl_initialized = false;
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);

    /* With the claim lost the handle leaks deliberately: the process is dying
     * and the claimant may still be executing facade code. */
    if (ctx->owns_dl_handle && facade_owned) dlclose(ctx->dl_handle);
    free(ctx->webos_sdk_version);
    free(ctx->window_id);
    free(ctx->app_id);
    pthread_mutex_destroy(&ctx->lifecycle_lock);
    free(ctx);
}

BackendNdlCapabilities backend_ndl_get_capabilities(const BackendNdl *ctx) {
    return ctx ? ctx->capabilities : 0;
}

BackendNdlAbiProfile backend_ndl_get_abi_profile(const BackendNdl *ctx) {
    return ctx ? ctx->abi_profile : BACKEND_NDL_ABI_UNKNOWN;
}

BackendNdlResult backend_ndl_set_media(BackendNdl *ctx,
                                      const BackendNdlVideoInfo *video,
                                      const BackendNdlAudioInfo *audio) {
    if (!ctx) return BACKEND_NDL_INVALID_ARGUMENT;
    if (video) {
        if (!ctx->api.DirectVideoPlay) return BACKEND_NDL_UNAVAILABLE;
        if (video->codec == BACKEND_NDL_VIDEO_NONE || video->width == 0 || video->height == 0 ||
            video->width > (uintmax_t)INT_MAX || video->height > (uintmax_t)INT_MAX) {
            return BACKEND_NDL_INVALID_ARGUMENT;
        }
        if (video->codec < BACKEND_NDL_VIDEO_H264 || video->codec > BACKEND_NDL_VIDEO_AV1) {
            return BACKEND_NDL_UNSUPPORTED;
        }
        if (ctx->abi_profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT &&
            video->codec != BACKEND_NDL_VIDEO_H264) {
            return BACKEND_NDL_UNSUPPORTED;
        }
    }
    if (audio) {
        if (!ctx->api.DirectAudioPlay) return BACKEND_NDL_UNAVAILABLE;
        if (audio->codec == BACKEND_NDL_AUDIO_NONE ||
            audio->sample_rate_hz == 0 || audio->channels == 0) {
            return BACKEND_NDL_INVALID_ARGUMENT;
        }
        if (audio->codec != BACKEND_NDL_AUDIO_PCM_S16LE &&
            audio->codec != BACKEND_NDL_AUDIO_OPUS) {
            return BACKEND_NDL_UNSUPPORTED;
        }
        if (audio->codec == BACKEND_NDL_AUDIO_PCM_S16LE) {
            if (!backend_ndl_pcm_channel_mode(audio->channels) ||
                !backend_ndl_pcm_sample_rate_supported(audio->sample_rate_hz)) {
                return BACKEND_NDL_UNSUPPORTED;
            }
            if (audio->channels == 6 &&
                atomic_load_explicit(&ctx->multichannel_status, memory_order_acquire) !=
                    BACKEND_NDL_MULTICHANNEL_READY) {
                return BACKEND_NDL_UNSUPPORTED;
            }
        } else if (audio->channels != 2 || audio->sample_rate_hz != 48000) {
            return BACKEND_NDL_UNSUPPORTED;
        }
    }

    BackendNdlMediaConfig next = {0};
    next.has_video = video != NULL;
    next.has_audio = audio != NULL;
    if (video) next.video = *video;
    if (audio) next.audio = *audio;

    pthread_mutex_lock(&ctx->lifecycle_lock);
    if (video && ctx->media.has_video &&
        backend_ndl_video_info_equal(video, &ctx->media.video)) {
        next.has_video_hdr = ctx->media.has_video_hdr;
        next.video_hdr = ctx->media.video_hdr;
    }
    BackendNdlResult result = backend_ndl_replace_media_locked(ctx, &next);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return result;
}

BackendNdlResult backend_ndl_unload(BackendNdl *ctx) {
    return backend_ndl_set_media(ctx, NULL, NULL);
}

BackendNdlResult backend_ndl_reload(BackendNdl *ctx) {
    if (!ctx) return BACKEND_NDL_INVALID_ARGUMENT;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    BackendNdlResult result = backend_ndl_reload_locked(ctx);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return result;
}

long long backend_ndl_media_time_ms(const BackendNdl *ctx) {
    if (!ctx) return 0;
    uint64_t state = atomic_load_explicit(&ctx->feed_state, memory_order_acquire);
    return backend_ndl_media_time_for_state(ctx, state);
}

BackendNdlResult backend_ndl_opt_get_int(BackendNdl *ctx, int (*fn)(int *), int *out) {
    if (!ctx || !out) return BACKEND_NDL_INVALID_ARGUMENT;
    if (!fn) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    int rc = fn(out);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

BackendNdlResult backend_ndl_set_foreground(BackendNdl *ctx, bool foreground) {
    if (!ctx) return BACKEND_NDL_INVALID_ARGUMENT;
    if (!ctx->api.DirectMediaSetAppState) return BACKEND_NDL_UNAVAILABLE;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    int rc = ctx->api.DirectMediaSetAppState(
        foreground ? NDL_DIRECTMEDIA_APP_STATE_FOREGROUND
                   : NDL_DIRECTMEDIA_APP_STATE_BACKGROUND);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return rc == 0 ? BACKEND_NDL_OK : BACKEND_NDL_ERROR;
}

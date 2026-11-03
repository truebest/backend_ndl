/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "backend_ndl_internal.h"
#include <string.h>

static const char *const app_types[] = {
    NULL, "RTC", "MIRACAST", "ATSC3", "CAMERA", "JP4K", "AIRPLAY",
    "DIRECTMEDIA", "APPCAST", "R2R", "SYE"
};

BackendNdlResult backend_ndl_gst_validate(const BackendNdlVideoInfo *video,
                                          const BackendNdlAudioInfo *audio) {
    if ((video && (unsigned)video->app_type >= sizeof(app_types) / sizeof(*app_types)) ||
        (audio && (unsigned)audio->app_type >= sizeof(app_types) / sizeof(*app_types)))
        return BACKEND_NDL_INVALID_ARGUMENT;
    bool selected = (video && video->app_type) || (audio && audio->app_type);
#if defined(BACKEND_NDL_WITH_GSTREAMER) && BACKEND_NDL_WITH_GSTREAMER
    if ((video && video->app_type && video->codec != BACKEND_NDL_VIDEO_H264 &&
         video->codec != BACKEND_NDL_VIDEO_H265) ||
        (audio && audio->app_type && audio->codec != BACKEND_NDL_AUDIO_PCM_S16LE))
        return BACKEND_NDL_UNSUPPORTED;
    (void)selected;
    return BACKEND_NDL_OK;
#else
    return selected ? BACKEND_NDL_UNSUPPORTED : BACKEND_NDL_OK;
#endif
}

#if defined(BACKEND_NDL_WITH_GSTREAMER) && BACKEND_NDL_WITH_GSTREAMER
#include <dlfcn.h>
#include <stdarg.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#define QUEUE_BUFFERS 512u
#define QUEUE_BYTES (32u * 1024u * 1024u)
#define PENDING_PICTURES 128u

typedef struct {
    GstClockTime token, original_pts;
    int64_t submitted_at_us;
    bool decode_only;
} Picture;

typedef struct Attempt {
    gint refs;
    GMutex lock;
    GCond idle;
    BackendNdl *owner;
    BackendNdlAppType video_type, audio_type;
    BackendNdlVideoCodec codec;
    uint32_t generation, epoch;
    bool closed, failed, flushing, source_bound, decoder_bound, decoder_attaching, sink_bound;
    unsigned callbacks, buffers;
    size_t bytes;
    GWeakRef root, source;
    Picture pending[PENDING_PICTURES];
    GstClockTime last_token, flushed_through;
    uint32_t submitted, decoded, suppressed, forwarded;
    unsigned depth, max_depth;
    gint64 last_report;
} Attempt;

struct BackendNdlGst {
    GMutex lock;
    Attempt *active;
    uint32_t generation;
};

typedef struct {
    gint refs;
    Attempt *attempt;
    uint32_t epoch;
    uint32_t flags;
    int64_t submitted_at_us;
    unsigned pushes;
    bool reserved, consumed;
    size_t bytes;
} Submission;

typedef struct {
    GstMeta meta;
    Submission *submission;
} FeedMeta;

static void attempt_unref(gpointer data);
static GPrivate construction = G_PRIVATE_INIT(attempt_unref);
static _Thread_local Attempt *loading;
static _Thread_local Submission *feeding;
static GQuark owner_key;
static gsize initialized;

static void init(void) {
    if (g_once_init_enter(&initialized)) {
        owner_key = g_quark_from_static_string("backend-ndl-owner");
        g_once_init_leave(&initialized, 1);
    }
}

static Attempt *attempt_ref(Attempt *a) {
    if (a) g_atomic_int_inc(&a->refs);
    return a;
}

static void attempt_unref(gpointer data) {
    Attempt *a = data;
    if (!a || !g_atomic_int_dec_and_test(&a->refs)) return;
    g_weak_ref_clear(&a->root);
    g_weak_ref_clear(&a->source);
    g_cond_clear(&a->idle);
    g_mutex_clear(&a->lock);
    g_free(a);
}

static Attempt *current(const BackendNdl *ctx) {
    if (!ctx->gst) return NULL;
    g_mutex_lock(&ctx->gst->lock);
    Attempt *a = attempt_ref(ctx->gst->active);
    g_mutex_unlock(&ctx->gst->lock);
    return a;
}

/* A callback keeps the owner alive until retirement has drained its users. */
static BackendNdl *owner_begin(Attempt *a) {
    g_mutex_lock(&a->lock);
    BackendNdl *owner = a->closed ? NULL : a->owner;
    if (owner) a->callbacks++;
    g_mutex_unlock(&a->lock);
    return owner;
}

static void owner_end(Attempt *a) {
    g_mutex_lock(&a->lock);
    if (--a->callbacks == 0) g_cond_broadcast(&a->idle);
    g_mutex_unlock(&a->lock);
}

static GstPadProbeReturn fail(Attempt *a, const char *reason) {
    g_mutex_lock(&a->lock);
    bool first = !a->closed && !a->failed;
    if (!a->closed) a->failed = true;
    g_mutex_unlock(&a->lock);
    if (first) {
        BackendNdl *owner = owner_begin(a);
        if (owner) {
            BACKEND_NDL_LOG(owner, BACKEND_NDL_LOG_ERROR, "GStreamer: %s", reason);
            owner_end(a);
        }
    }
    return GST_PAD_PROBE_DROP;
}

static void retire(Attempt *a) {
    if (!a) return;
    g_mutex_lock(&a->lock);
    a->closed = true;
    a->owner = NULL;
    while (a->callbacks) g_cond_wait(&a->idle, &a->lock);
    g_mutex_unlock(&a->lock);
    GstElement *source = g_weak_ref_get(&a->source);
    if (source) {
        gst_element_set_state(source, GST_STATE_NULL);
        gst_object_unref(source);
    }
}

static bool tracking(const Attempt *a) {
    return a->codec == BACKEND_NDL_VIDEO_H264 && a->video_type &&
           a->video_type != BACKEND_NDL_APP_DIRECTMEDIA;
}

bool backend_ndl_gst_open(BackendNdl *ctx) {
    init();
    ctx->gst = g_try_new0(struct BackendNdlGst, 1);
    if (!ctx->gst) return false;
    g_mutex_init(&ctx->gst->lock);
    return true;
}

void backend_ndl_gst_unload(BackendNdl *ctx) {
    if (!ctx->gst) return;
    g_mutex_lock(&ctx->gst->lock);
    Attempt *a = ctx->gst->active;
    ctx->gst->active = NULL;
    g_mutex_unlock(&ctx->gst->lock);
    retire(a);
    attempt_unref(a);
}

void backend_ndl_gst_close(BackendNdl *ctx) {
    if (!ctx->gst) return;
    backend_ndl_gst_unload(ctx);
    g_mutex_clear(&ctx->gst->lock);
    g_free(ctx->gst);
    ctx->gst = NULL;
}

void backend_ndl_gst_load_begin(BackendNdl *ctx, const BackendNdlMediaConfig *media) {
    backend_ndl_gst_unload(ctx);
    loading = NULL;
    if ((!media->has_video || !media->video.app_type) &&
        (!media->has_audio || !media->audio.app_type)) return;
    Attempt *a = g_new0(Attempt, 1);
    a->refs = 1;
    a->owner = ctx;
    a->video_type = media->has_video ? media->video.app_type : BACKEND_NDL_APP_DEFAULT;
    a->audio_type = media->has_audio ? media->audio.app_type : BACKEND_NDL_APP_DEFAULT;
    a->codec = media->has_video ? media->video.codec : BACKEND_NDL_VIDEO_NONE;
    a->epoch = 1;
    g_mutex_init(&a->lock);
    g_cond_init(&a->idle);
    g_weak_ref_init(&a->root, NULL);
    g_weak_ref_init(&a->source, NULL);
    g_mutex_lock(&ctx->gst->lock);
    ctx->gst->generation = ctx->gst->generation >= BACKEND_NDL_FEED_GENERATION_MAX ? 1 : ctx->gst->generation + 1;
    a->generation = ctx->gst->generation;
    ctx->gst->active = a;
    g_mutex_unlock(&ctx->gst->lock);
    loading = a;
}

void backend_ndl_gst_load_end(BackendNdl *ctx, bool success) {
    loading = NULL;
    if (!success) backend_ndl_gst_unload(ctx);
}

void backend_ndl_gst_status(const BackendNdl *ctx, BackendNdlVideoStatus *out) {
    Attempt *a = current(ctx);
    if (!a) return;
    g_mutex_lock(&a->lock);
    out->generation = a->generation;
    bool ready = !a->closed && !a->flushing && a->source_bound &&
                 a->decoder_bound && a->sink_bound;
    out->supports_decode_only = ready && tracking(a);
    out->supports_frame_events = out->supports_decode_only;
    if (a->failed) out->state = BACKEND_NDL_VIDEO_PATH_FAILED;
    else if (a->video_type && !ready) out->state = BACKEND_NDL_VIDEO_PATH_NOT_READY;
    out->submitted = a->submitted;
    out->decoded = a->decoded;
    out->suppressed = a->suppressed;
    out->forwarded = a->forwarded;
    out->queued_buffers = a->buffers;
    out->queued_bytes = a->bytes;
    out->pending_pictures = a->depth;
    out->max_pending_pictures = a->max_depth;
    g_mutex_unlock(&a->lock);
    attempt_unref(a);
}

static void reset(Attempt *a, bool flushing) {
    g_mutex_lock(&a->lock);
    if (a->epoch == UINT32_MAX) {
        a->flushing = true;
        g_mutex_unlock(&a->lock);
        fail(a, "flush epoch exhausted; reload required");
        return;
    }
    memset(a->pending, 0, sizeof(a->pending));
    a->depth = 0;
    a->flushed_through = a->last_token;
    a->flushing = flushing;
    ++a->epoch;
    g_mutex_unlock(&a->lock);
}

void backend_ndl_gst_flush(BackendNdl *ctx) {
    Attempt *a = current(ctx);
    if (a) { reset(a, false); attempt_unref(a); }
}

static void release_queue(Submission *s) {
    Attempt *a = s->attempt;
    g_mutex_lock(&a->lock);
    if (s->reserved) {
        s->reserved = false;
        a->buffers--;
        a->bytes -= s->bytes;
    }
    g_mutex_unlock(&a->lock);
}

static Submission *submission_ref(Submission *s) {
    g_atomic_int_inc(&s->refs);
    return s;
}

static void submission_unref(Submission *s) {
    if (!g_atomic_int_dec_and_test(&s->refs)) return;
    release_queue(s);
    attempt_unref(s->attempt);
    g_free(s);
}

void *backend_ndl_gst_feed_begin(BackendNdl *ctx, uint32_t flags) {
    Attempt *a = current(ctx);
    if (!a) return NULL;
    if (!a->video_type) { attempt_unref(a); return NULL; }
    Submission *s = g_new0(Submission, 1);
    s->refs = 1;
    s->attempt = a;
    s->flags = flags;
    s->submitted_at_us = g_get_monotonic_time();
    g_mutex_lock(&a->lock);
    s->epoch = a->epoch;
    g_mutex_unlock(&a->lock);
    feeding = s;
    return s;
}

BackendNdlResult backend_ndl_gst_feed_end(void *data) {
    Submission *s = data;
    if (!s) return BACKEND_NDL_OK;
    feeding = NULL;
    if (s->pushes != 1) fail(s->attempt, "video feed did not transfer exactly one metadata record");
    g_mutex_lock(&s->attempt->lock);
    bool failed = s->attempt->failed;
    g_mutex_unlock(&s->attempt->lock);
    submission_unref(s);
    return failed ? BACKEND_NDL_ERROR : BACKEND_NDL_OK;
}

static gboolean meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    (void)params; (void)buffer;
    ((FeedMeta *)meta)->submission = NULL;
    return TRUE;
}

static void meta_free(GstMeta *meta, GstBuffer *buffer) {
    (void)buffer;
    Submission *s = ((FeedMeta *)meta)->submission;
    if (s) submission_unref(s);
}

static const GstMetaInfo *feed_meta_info(void);

static gboolean meta_transform(GstBuffer *dest, GstMeta *meta, GstBuffer *src,
                                GQuark type, gpointer data) {
    (void)src;
    if (!GST_META_TRANSFORM_IS_COPY(type)) return FALSE;
    GstMetaTransformCopy *copy = data;
    if (copy->region) return FALSE;
    FeedMeta *next = (FeedMeta *)gst_buffer_add_meta(dest, feed_meta_info(), NULL);
    if (!next) return FALSE;
    next->submission = submission_ref(((FeedMeta *)meta)->submission);
    return TRUE;
}

static const GstMetaInfo *feed_meta_info(void) {
    static gsize info;
    if (g_once_init_enter(&info)) {
        const gchar *tags[] = {NULL};
        GType api = gst_meta_api_type_register("BackendNdlFeedMetaAPI", tags);
        const GstMetaInfo *registered = gst_meta_register(api, "BackendNdlFeedMeta",
            sizeof(FeedMeta), meta_init, meta_free, meta_transform);
        g_once_init_leave(&info, (gsize)registered);
    }
    return (const GstMetaInfo *)info;
}

static FeedMeta *get_meta(GstBuffer *buffer) {
    return (FeedMeta *)gst_buffer_get_meta(buffer, feed_meta_info()->api);
}

static GstPadProbeReturn timestamp_units(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    (void)pad;
    Attempt *a = data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    FeedMeta *meta = get_meta(buffer);
    if (meta) release_queue(meta->submission);
    g_mutex_lock(&a->lock);
    bool closed = a->closed;
    g_mutex_unlock(&a->lock);
    if (closed) return GST_PAD_PROBE_DROP;
    if (!GST_BUFFER_PTS_IS_VALID(buffer) || GST_BUFFER_PTS(buffer) >= GST_CLOCK_TIME_NONE / GST_MSECOND)
        return fail(a, "invalid NDL millisecond PTS");
    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    /* This firmware puts NDL milliseconds in GstBuffer's nanosecond field. */
    GST_BUFFER_PTS(buffer) *= GST_MSECOND;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn input_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    (void)pad;
    Attempt *a = data;
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEventType type = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info));
        if (type == GST_EVENT_FLUSH_START || type == GST_EVENT_FLUSH_STOP)
            reset(a, type == GST_EVENT_FLUSH_START);
        /* Initial STREAM_START follows appsrc queueing: preserve that epoch. */
        if (type == GST_EVENT_STREAM_START) {
            g_mutex_lock(&a->lock);
            bool started = a->last_token != 0;
            g_mutex_unlock(&a->lock);
            if (started) reset(a, false);
        }
        return GST_PAD_PROBE_OK;
    }
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    FeedMeta *meta = get_meta(buffer);
    if (!meta) return fail(a, "decoder input lost its feed metadata");
    if (meta->submission->attempt != a) {
        Attempt *previous = meta->submission->attempt;
        g_mutex_lock(&previous->lock);
        bool retired = previous->closed;
        g_mutex_unlock(&previous->lock);
        return retired ? GST_PAD_PROBE_DROP : fail(a, "decoder input belongs to another pipeline");
    }
    Submission *s = meta->submission;
    release_queue(s);
    g_mutex_lock(&a->lock);
    if (a->closed || a->failed || a->flushing || s->epoch != a->epoch) {
        g_mutex_unlock(&a->lock);
        return GST_PAD_PROBE_DROP;
    }
    if (s->consumed) {
        g_mutex_unlock(&a->lock);
        return fail(a, "decoder received a repeated feed identity");
    }
    s->consumed = true;
    bool configuration = (s->flags & BACKEND_NDL_VIDEO_CONFIG_ONLY) != 0;
    unsigned i = 0;
    while (i < PENDING_PICTURES && a->pending[i].token) i++;
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    if ((!configuration && i == PENDING_PICTURES) || !GST_CLOCK_TIME_IS_VALID(pts) ||
        pts >= GST_CLOCK_TIME_NONE - GST_MSECOND ||
        a->last_token >= GST_CLOCK_TIME_NONE - GST_MSECOND) {
        g_mutex_unlock(&a->lock);
        return fail(a, "decoder exceeded pending-picture or timestamp capacity");
    }
    GstClockTime token = pts / GST_MSECOND * GST_MSECOND;
    if (token <= a->last_token) token = a->last_token + GST_MSECOND;
    a->last_token = token;
    /* Config-only timestamps cannot alias a pending image if a caller mislabels an AU. */
    if (!configuration) {
        a->pending[i] = (Picture){token, pts, s->submitted_at_us,
                                  (s->flags & BACKEND_NDL_VIDEO_DECODE_ONLY) != 0};
        a->submitted++;
        if (++a->depth > a->max_depth) a->max_depth = a->depth;
    }
    g_mutex_unlock(&a->lock);
    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GST_BUFFER_PTS(buffer) = token;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn output_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    (void)pad;
    Attempt *a = data;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    GstClockTime token = GST_BUFFER_PTS(buffer);
    int64_t now = g_get_monotonic_time();
    g_mutex_lock(&a->lock);
    if (a->closed || a->failed || a->flushing || (token && token <= a->flushed_through)) {
        g_mutex_unlock(&a->lock);
        return GST_PAD_PROBE_DROP;
    }
    unsigned i = 0;
    while (i < PENDING_PICTURES && (!token || a->pending[i].token != token)) i++;
    if (i == PENDING_PICTURES) {
        g_mutex_unlock(&a->lock);
        return fail(a, "decoder output has no matching feed identity");
    }
    Picture picture = a->pending[i];
    a->pending[i].token = 0;
    a->depth--;
    a->decoded++;
    if (picture.decode_only) a->suppressed++;
    else a->forwarded++;
    bool report = now - a->last_report >= 5 * G_USEC_PER_SEC;
    if (report) a->last_report = now;
    uint32_t submitted = a->submitted, decoded = a->decoded;
    uint32_t suppressed = a->suppressed, forwarded = a->forwarded;
    unsigned depth = a->depth, max_depth = a->max_depth, queued = a->buffers;
    size_t queued_bytes = a->bytes;
    g_mutex_unlock(&a->lock);
    BackendNdl *owner = owner_begin(a);
    if (owner) {
        if (owner->config.video_frame_fn) {
            BackendNdlVideoFrameEvent event = {a->generation,
                (int64_t)(picture.original_pts / GST_MSECOND),
                picture.submitted_at_us, now, picture.decode_only};
            owner->config.video_frame_fn(owner->config.userdata, &event);
        }
        if (report) BACKEND_NDL_LOG(owner, BACKEND_NDL_LOG_INFO,
            "decoded pictures: submitted=%u decoded=%u suppressed=%u forwarded=%u pending=%u max_pending=%u queued=%u queued_bytes=%zu",
            submitted, decoded, suppressed, forwarded, depth, max_depth, queued, queued_bytes);
        owner_end(a);
    }
    if (picture.decode_only) return GST_PAD_PROBE_DROP;
    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GST_BUFFER_PTS(buffer) = picture.original_pts;
    return GST_PAD_PROBE_OK;
}

static void own(GstElement *element, Attempt *a) {
    if (g_object_get_qdata(G_OBJECT(element), owner_key)) return;
    g_object_set_qdata_full(G_OBJECT(element), owner_key, attempt_ref(a), attempt_unref);
}

static bool firmware_caller(gpointer address) {
    Dl_info info;
    if (!dladdr(address, &info) || !info.dli_fname) return false;
    const char *name = strrchr(info.dli_fname, '/');
    name = name ? name + 1 : info.dli_fname;
    return g_str_has_prefix(name, "libpf-1.0.so") ||
           g_str_has_prefix(name, "libplayerAPIs.so") ||
           g_str_has_prefix(name, "libNDL_directmedia_impl.so");
}

static bool construction_caller(gpointer caller) {
#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
    if (loading) return true;
#endif
    return firmware_caller(caller);
}

static Attempt *construction_begin(gpointer caller) {
    Attempt *a = construction_caller(caller) ? attempt_ref(loading) : NULL;
    if (!a && firmware_caller(caller)) {
        pthread_mutex_lock(&g_backend_ndl_registry_lock);
        if (g_backend_ndl_active) a = current(g_backend_ndl_active);
        pthread_mutex_unlock(&g_backend_ndl_registry_lock);
    }
    g_private_replace(&construction, attempt_ref(a));
    return a;
}

GstElement *gst_pipeline_new(const gchar *name) {
    static GstElement *(*real_new)(const gchar *);
    static gsize resolved;
    if (g_once_init_enter(&resolved)) {
        real_new = dlsym(RTLD_NEXT, "gst_pipeline_new");
        g_once_init_leave(&resolved, 1);
    }
    if (!real_new) return NULL;
    init();
    Attempt *a = construction_begin(__builtin_return_address(0));
    GstElement *element = real_new(name);
    if (a && element) {
        own(element, a);
        g_mutex_lock(&a->lock);
        GObject *previous = g_weak_ref_get(&a->root);
        if (!previous) g_weak_ref_set(&a->root, element);
        g_mutex_unlock(&a->lock);
        if (previous) {
            g_object_unref(previous);
            fail(a, "load attempt created more than one pipeline");
        }
    }
    attempt_unref(a);
    return element;
}

static void own_constructed(GstElement *element, gpointer caller) {
    Attempt *a = g_private_get(&construction);
    if (element && a && construction_caller(caller)) own(element, a);
}

GstElement *gst_element_factory_create(GstElementFactory *factory, const gchar *name) {
    static GstElement *(*real_create)(GstElementFactory *, const gchar *);
    static gsize resolved;
    if (g_once_init_enter(&resolved)) {
        real_create = dlsym(RTLD_NEXT, "gst_element_factory_create");
        g_once_init_leave(&resolved, 1);
    }
    if (!real_create) return NULL;
    GstElement *element = real_create(factory, name);
    own_constructed(element, __builtin_return_address(0));
    return element;
}

GstElement *gst_element_factory_make(const gchar *factory, const gchar *name) {
    static GstElement *(*real_make)(const gchar *, const gchar *);
    static gsize resolved;
    if (g_once_init_enter(&resolved)) {
        real_make = dlsym(RTLD_NEXT, "gst_element_factory_make");
        g_once_init_leave(&resolved, 1);
    }
    if (!real_make) return NULL;
    GstElement *element = real_make(factory, name);
    own_constructed(element, __builtin_return_address(0));
    return element;
}

static void add_probe(GstElement *element, const char *name, GstPadProbeType type,
                       GstPadProbeCallback probe, Attempt *a) {
    GstPad *pad = gst_element_get_static_pad(element, name);
    if (!pad) { fail(a, "owned element is missing its static pad"); return; }
    gst_pad_add_probe(pad, type, probe, attempt_ref(a), attempt_unref);
    gst_object_unref(pad);
}

void g_object_set(gpointer object, const gchar *first, ...) {
    va_list args;
    va_start(args, first);
    init();
    Attempt *a = G_IS_OBJECT(object) ? g_object_get_qdata(object, owner_key) : NULL;
    if (a) {
        g_mutex_lock(&a->lock);
        bool closed = a->closed;
        g_mutex_unlock(&a->lock);
        if (closed) a = NULL;
    }
    const char *type = a ? G_OBJECT_TYPE_NAME(object) : "";
    bool decoder = a && a->video_type &&
        ((!strcmp(type, "GstOMXH264DecBin") && a->codec == BACKEND_NDL_VIDEO_H264) ||
         (!strcmp(type, "GstOMXH265DecBin") && a->codec == BACKEND_NDL_VIDEO_H265));
    bool sink = a && a->video_type && !strcmp(type, "GstRvSinkBin");
    bool audio = a && a->audio_type && !strcmp(type, "GstRaSinkBin");
    if (first && !strcmp(first, "app-type") && (decoder || sink || audio)) {
        const char *value = va_arg(args, const char *);
        if (value && !strcmp(value, "DIRECTMEDIA")) {
            BackendNdlAppType selected = audio ? a->audio_type : a->video_type;
            GValue replacement = G_VALUE_INIT;
            g_value_init(&replacement, G_TYPE_STRING);
            g_value_set_static_string(&replacement, app_types[selected]);
            g_object_set_property(object, first, &replacement);
            g_value_unset(&replacement);
            const char *next = va_arg(args, const char *);
            if (next) g_object_set_valist(object, next, args);
            if (audio && selected == BACKEND_NDL_APP_RTC)
                g_object_set(object, "sync", FALSE, "async", FALSE, NULL);
            g_mutex_lock(&a->lock);
            bool attach = decoder && !a->decoder_bound && !a->decoder_attaching && !a->failed;
            if (attach) a->decoder_attaching = true;
            if (sink) a->sink_bound = true;
            g_mutex_unlock(&a->lock);
            if (attach) {
                if (tracking(a)) {
                    g_object_set(object, "qos", FALSE, NULL);
                    add_probe(object, "sink", GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
                        GST_PAD_PROBE_TYPE_EVENT_FLUSH, input_probe, a);
                    add_probe(object, "src", GST_PAD_PROBE_TYPE_BUFFER, output_probe, a);
                }
                g_mutex_lock(&a->lock);
                a->decoder_bound = !a->failed;
                a->decoder_attaching = false;
                g_mutex_unlock(&a->lock);
            }
            BackendNdl *owner = owner_begin(a);
            if (owner) {
                BACKEND_NDL_LOG(owner, BACKEND_NDL_LOG_INFO,
                    "GStreamer profile: %s app-type=%s generation=%u",
                    type, app_types[selected], a->generation);
                owner_end(a);
            }
            va_end(args);
            return;
        }
        /* The value was inspected, so rebuild the untouched list for forwarding. */
        va_end(args);
        va_start(args, first);
    }
    g_object_set_valist(object, first, args);
    va_end(args);
    if (!a || strcmp(type, "GstAppSrc")) return;
    const char *name = GST_OBJECT_NAME(object);
    bool video = a->video_type && !g_strcmp0(name, "video-src");
    bool sound = a->audio_type && !g_strcmp0(name, "audio-src");
    if (!video && !sound) return;
    if (!g_object_get_data(object, "backend-ndl-source")) {
        g_object_set_data(object, "backend-ndl-source", GINT_TO_POINTER(video ? 1 : 2));
        add_probe(object, "src", GST_PAD_PROBE_TYPE_BUFFER, timestamp_units, a);
        if (video) {
            g_mutex_lock(&a->lock);
            a->source_bound = true;
            g_weak_ref_set(&a->source, object);
            g_mutex_unlock(&a->lock);
        }
    }
    if (video) {
        gboolean block;
        guint64 bytes;
        g_object_get(object, "block", &block, "max-bytes", &bytes, NULL);
        if (block || bytes) g_object_set(object, "block", FALSE, "max-bytes", (guint64)0, NULL);
    }
}

gboolean gst_bus_post(GstBus *bus, GstMessage *message) {
    static gboolean (*real_post)(GstBus *, GstMessage *);
    static gsize resolved;
    if (g_once_init_enter(&resolved)) {
        real_post = dlsym(RTLD_NEXT, "gst_bus_post");
        g_once_init_leave(&resolved, 1);
    }
    init();
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GstObject *object = GST_MESSAGE_SRC(message);
        if (object) gst_object_ref(object);
        Attempt *a = NULL;
        bool audio = false;
        while (object) {
            if (!a) a = attempt_ref(g_object_get_qdata(G_OBJECT(object), owner_key));
            const char *type = G_OBJECT_TYPE_NAME(object);
            const char *klass = GST_IS_ELEMENT(object)
                ? gst_element_get_metadata(GST_ELEMENT(object), GST_ELEMENT_METADATA_KLASS) : NULL;
            audio |= !strcmp(type, "GstRaSinkBin") ||
                g_object_get_data(G_OBJECT(object), "backend-ndl-source") == GINT_TO_POINTER(2) ||
                (klass && strstr(klass, "Audio"));
            GstObject *parent = gst_object_get_parent(object);
            gst_object_unref(object);
            object = parent;
        }
        if (a) {
            if (!audio && a->video_type) fail(a, "owned video pipeline posted a GStreamer error");
            attempt_unref(a);
        }
    }
    if (real_post) return real_post(bus, message);
    if (message) gst_message_unref(message);
    return FALSE;
}

GstFlowReturn gst_app_src_push_buffer(GstAppSrc *source, GstBuffer *buffer) {
    static GstFlowReturn (*real_push)(GstAppSrc *, GstBuffer *);
    static gsize resolved;
    if (g_once_init_enter(&resolved)) {
        real_push = dlsym(RTLD_NEXT, "gst_app_src_push_buffer");
        g_once_init_leave(&resolved, 1);
    }
    init();
    Attempt *a = source ? g_object_get_qdata(G_OBJECT(source), owner_key) : NULL;
    bool video = a && g_object_get_data(G_OBJECT(source), "backend-ndl-source") == GINT_TO_POINTER(1);
    if (!video) {
        if (real_push) return real_push(source, buffer);
        if (buffer) gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    Submission *s = feeding;
    if (s && s->attempt == a) s->pushes++;
    else {
        /* A feed crossing reload belongs to its original attempt. */
        fail(s ? s->attempt : a, "source received a buffer outside its feed generation");
        if (buffer) gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }
    if (!buffer || !s || !real_push) {
        fail(a, "video buffer submission is unavailable");
        if (buffer) gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }
    size_t bytes = gst_buffer_get_size(buffer);
    g_mutex_lock(&a->lock);
    bool full = a->buffers >= QUEUE_BUFFERS || bytes > QUEUE_BYTES - a->bytes;
    bool reject = a->closed || a->failed || a->flushing || s->epoch != a->epoch || full || s->pushes != 1;
    if (!reject) {
        s->reserved = true;
        s->bytes = bytes;
        a->buffers++;
        a->bytes += bytes;
    }
    g_mutex_unlock(&a->lock);
    if (reject) {
        if (full || s->pushes != 1) fail(a, "video input exceeded queue or feed-identity capacity");
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }
    buffer = gst_buffer_make_writable(buffer);
    FeedMeta *meta = (FeedMeta *)gst_buffer_add_meta(buffer, feed_meta_info(), NULL);
    if (!meta) {
        fail(a, "cannot attach feed metadata");
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }
    meta->submission = submission_ref(s);
    GstFlowReturn result = real_push(source, buffer);
    if (result != GST_FLOW_OK) fail(a, "GStreamer rejected a queued video buffer");
    /* NDL unreferences failed pushes again; expose our latched error via the API. */
    return GST_FLOW_OK;
}
#else
bool backend_ndl_gst_open(BackendNdl *ctx) { (void)ctx; return true; }
void backend_ndl_gst_load_begin(BackendNdl *ctx, const BackendNdlMediaConfig *media) { (void)ctx; (void)media; }
void backend_ndl_gst_load_end(BackendNdl *ctx, bool success) { (void)ctx; (void)success; }
void backend_ndl_gst_unload(BackendNdl *ctx) { (void)ctx; }
void backend_ndl_gst_close(BackendNdl *ctx) { (void)ctx; }
void *backend_ndl_gst_feed_begin(BackendNdl *ctx, uint32_t flags) { (void)ctx; (void)flags; return NULL; }
BackendNdlResult backend_ndl_gst_feed_end(void *submission) { (void)submission; return BACKEND_NDL_OK; }
void backend_ndl_gst_status(const BackendNdl *ctx, BackendNdlVideoStatus *out) { (void)ctx; (void)out; }
void backend_ndl_gst_flush(BackendNdl *ctx) { (void)ctx; }
#endif

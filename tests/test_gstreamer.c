/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include "../src/backend_ndl_gstreamer.c"

typedef struct {
    GstElement element;
    gchar *app_type;
    gboolean sync, async, qos;
} FakeElement;

static void fake_set(GObject *object, guint id, const GValue *value, GParamSpec *spec) {
    (void)spec;
    FakeElement *e = (FakeElement *)object;
    if (id == 1) { g_free(e->app_type); e->app_type = g_value_dup_string(value); }
    else if (id == 2) e->sync = g_value_get_boolean(value);
    else if (id == 3) e->async = g_value_get_boolean(value);
    else if (id == 4) e->qos = g_value_get_boolean(value);
}
static void fake_get(GObject *object, guint id, GValue *value, GParamSpec *spec) {
    (void)spec;
    FakeElement *e = (FakeElement *)object;
    if (id == 1) g_value_set_string(value, e->app_type);
    else if (id == 2) g_value_set_boolean(value, e->sync);
    else if (id == 3) g_value_set_boolean(value, e->async);
    else if (id == 4) g_value_set_boolean(value, e->qos);
}
static void fake_finalize(GObject *object) {
    g_free(((FakeElement *)object)->app_type);
    G_OBJECT_CLASS(g_type_class_peek(GST_TYPE_ELEMENT))->finalize(object);
}
static void fake_class(gpointer klass, gpointer data) {
    (void)data;
    GObjectClass *object = klass;
    object->set_property = fake_set;
    object->get_property = fake_get;
    object->finalize = fake_finalize;
    g_object_class_install_property(object, 1, g_param_spec_string("app-type", "app-type", "app-type",
        NULL, G_PARAM_READWRITE));
    const char *names[] = {"sync", "async", "qos"};
    for (unsigned i = 0; i < 3; i++) g_object_class_install_property(object, 2 + i,
        g_param_spec_boolean(names[i], names[i], names[i], TRUE, G_PARAM_READWRITE));
    gst_element_class_set_static_metadata(klass, "test", "Generic", "test", "test");
}
static void fake_init(GTypeInstance *instance, gpointer klass) {
    (void)klass;
    GstElement *element = GST_ELEMENT(instance);
    gst_element_add_pad(element, gst_pad_new("sink", GST_PAD_SINK));
    gst_element_add_pad(element, gst_pad_new("src", GST_PAD_SRC));
}
static void register_fake(const char *name, const char *factory) {
    GType type = g_type_register_static_simple(GST_TYPE_ELEMENT, name, sizeof(GstElementClass),
        fake_class, sizeof(FakeElement), fake_init, 0);
    assert(gst_element_register(NULL, factory, GST_RANK_NONE, type));
}

static GstElement *pipeline, *source, *decoder, *video_sink, *audio_sink;
static unsigned fail_loads;
static bool use_factory_create = true;
static GstElement *make_element(const char *factory, const char *name) {
    if (!use_factory_create) return gst_element_factory_make(factory, name);
    GstElementFactory *entry = gst_element_factory_find(factory);
    assert(entry);
    GstElement *element = gst_element_factory_create(entry, name);
    gst_object_unref(entry);
    return element;
}
static int fake_load(NDL_DIRECTMEDIA_DATA_INFO_T *info, StateCallbackFP callback) {
    (void)info; (void)callback;
    if (fail_loads) { fail_loads--; return -1; }
    pipeline = gst_pipeline_new(NULL);
    source = make_element("appsrc", "video-src");
    decoder = make_element("ndl-test-decoder", NULL);
    video_sink = make_element("ndl-test-video-sink", NULL);
    audio_sink = make_element("ndl-test-audio-sink", NULL);
    assert(pipeline && source && decoder && video_sink && audio_sink);
    gst_bin_add_many(GST_BIN(pipeline), source, decoder, video_sink, audio_sink, NULL);
    g_object_set(source, "is-live", TRUE, "block", TRUE, "max-bytes", (guint64)1, NULL);
    g_object_set(decoder, "app-type", "DIRECTMEDIA", NULL);
    g_object_set(video_sink, "app-type", "DIRECTMEDIA", NULL);
    g_object_set(audio_sink, "app-type", "DIRECTMEDIA", NULL);
    assert(gst_element_set_state(source, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE);
    return 0;
}
static int fake_unload(void) {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_element_set_state(source, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = source = decoder = video_sink = audio_sink = NULL;
    }
    return 0;
}
static int fake_video(void *data, unsigned int size, long long pts) {
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_fill(buffer, 0, data, size);
    GST_BUFFER_PTS(buffer) = pts;
    return gst_app_src_push_buffer(GST_APP_SRC(source), buffer) == GST_FLOW_OK ? 0 : -1;
}
static int fake_init_ndl(const char *id) { (void)id; return 0; }
static int fake_set_window(const char *id) { (void)id; return 0; }
static const char *fake_error(void) { return "test failure"; }
static int fake_audio(void *data, unsigned int size, long long pts) {
    (void)data; (void)size; (void)pts; return 0;
}
static unsigned events;
static BackendNdlVideoFrameEvent last_event;
static void frame(void *data, const BackendNdlVideoFrameEvent *event) {
    (void)data;
    events++;
    last_event = *event;
}
static BackendNdl *open_backend(void) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test";
    config.video_frame_fn = frame;
    config.require_keyframe_after_reload = true;
    BackendNdlApi api = {0};
    api.DirectMediaGetError = fake_error;
    api.DirectMediaSetWindowId = fake_set_window;
    api.DirectMediaInitCurrent = fake_init_ndl;
    api.DirectMediaQuit = fake_unload;
    api.DirectMediaLoadCurrent = fake_load;
    api.DirectMediaUnload = fake_unload;
    api.DirectVideoPlay = fake_video;
    api.DirectAudioPlay = fake_audio;
    BackendNdl *ctx = backend_ndl_open_with_api(&config, BACKEND_NDL_ABI_V2_WINDOW_ID_INIT, &api);
    assert(ctx);
    return ctx;
}
static void configure(BackendNdl *ctx, BackendNdlAppType video, BackendNdlAppType audio) {
    BackendNdlVideoInfo v = {BACKEND_NDL_VIDEO_H264, 3840, 2160, video};
    BackendNdlAudioInfo a = {BACKEND_NDL_AUDIO_PCM_S16LE, 48000, 2, audio};
    assert(backend_ndl_set_media(ctx, &v, &a) == BACKEND_NDL_OK);
}
static BackendNdlVideoStatus status(BackendNdl *ctx) {
    BackendNdlVideoStatus out;
    assert(backend_ndl_get_video_status(ctx, &out) == BACKEND_NDL_OK);
    return out;
}
static void check_type(GstElement *e, const char *expected) {
    gchar *actual = NULL;
    g_object_get(e, "app-type", &actual, NULL);
    assert(!g_strcmp0(actual, expected));
    g_free(actual);
}
static void profiles(void) {
    BackendNdl *ctx = open_backend();
    for (unsigned i = 1; i < G_N_ELEMENTS(app_types); i++) {
        use_factory_create = (i % 2) != 0;
        configure(ctx, (BackendNdlAppType)i, BACKEND_NDL_APP_AIRPLAY);
        check_type(decoder, app_types[i]);
        check_type(video_sink, app_types[i]);
        check_type(audio_sink, "AIRPLAY");
        assert(status(ctx).supports_decode_only == (i != BACKEND_NDL_APP_DIRECTMEDIA));
    }
    for (unsigned i = 1; i < G_N_ELEMENTS(app_types); i++) {
        use_factory_create = (i % 2) == 0;
        configure(ctx, BACKEND_NDL_APP_RTC, (BackendNdlAppType)i);
        check_type(decoder, "RTC");
        check_type(audio_sink, app_types[i]);
        if (i == BACKEND_NDL_APP_RTC) {
            gboolean sync, async;
            g_object_get(audio_sink, "sync", &sync, "async", &async, NULL);
            assert(!sync && !async);
        }
    }
    use_factory_create = true;
    configure(ctx, BACKEND_NDL_APP_DEFAULT, BACKEND_NDL_APP_DEFAULT);
    check_type(decoder, "DIRECTMEDIA");
    assert(!status(ctx).supports_decode_only);
    const char byte = 0;
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 10, false, BACKEND_NDL_VIDEO_DECODE_ONLY) == BACKEND_NDL_UNSUPPORTED);
    BackendNdlVideoInfo invalid = {BACKEND_NDL_VIDEO_H264, 1, 1, (BackendNdlAppType)99};
    assert(backend_ndl_set_media(ctx, &invalid, NULL) == BACKEND_NDL_INVALID_ARGUMENT);
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    GstElement *other = gst_element_factory_make("appsrc", "video-src");
    g_object_set(other, "block", TRUE, "max-bytes", (guint64)1, NULL);
    gboolean block;
    g_object_get(other, "block", &block, NULL);
    assert(block && !g_object_get_qdata(G_OBJECT(other), owner_key));
    GstElement *other_decoder = gst_element_factory_make("ndl-test-decoder", NULL);
    g_object_set(other_decoder, "app-type", "DIRECTMEDIA", NULL);
    check_type(other_decoder, "DIRECTMEDIA");
    gst_object_unref(other_decoder);
    gst_object_unref(other);
    backend_ndl_close(ctx);
}

static GstBuffer *picture_buffer(BackendNdl *ctx, uint32_t flags, GstClockTime pts) {
    Submission *s = backend_ndl_gst_feed_begin(ctx, flags);
    assert(s);
    s->pushes = 1;
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, 5, NULL);
    FeedMeta *meta = (FeedMeta *)gst_buffer_add_meta(buffer, feed_meta_info(), NULL);
    meta->submission = submission_ref(s);
    assert(backend_ndl_gst_feed_end(s) == BACKEND_NDL_OK);
    GST_BUFFER_PTS(buffer) = pts;
    GstBuffer *copy = gst_buffer_copy_deep(buffer);
    assert(copy && get_meta(copy)->submission == get_meta(buffer)->submission);
    gst_buffer_unref(buffer);
    return copy;
}
static GstClockTime submit(BackendNdl *ctx, Attempt *a, uint32_t flags, GstClockTime pts,
                           GstPadProbeReturn expected) {
    GstPadProbeInfo info = {.type = GST_PAD_PROBE_TYPE_BUFFER,
                           .data = picture_buffer(ctx, flags, pts)};
    assert(input_probe(NULL, &info, a) == expected);
    GstClockTime token = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(&info));
    gst_buffer_unref(info.data);
    return token;
}
static void receive(Attempt *a, GstClockTime token, GstClockTime pts, GstPadProbeReturn expected) {
    GstBuffer *buffer = gst_buffer_new();
    GST_BUFFER_PTS(buffer) = token;
    GstPadProbeInfo info = {.type = GST_PAD_PROBE_TYPE_BUFFER, .data = buffer};
    assert(output_probe(NULL, &info, a) == expected);
    if (expected == GST_PAD_PROBE_OK) assert(GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(&info)) == pts);
    gst_buffer_unref(info.data);
}
static void pictures(void) {
    BackendNdl *ctx = open_backend();
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    Attempt *a = current(ctx);
    events = 0;
    GstClockTime pts = 10 * GST_SECOND;
    GstClockTime main = submit(ctx, a, 0, pts, GST_PAD_PROBE_OK);
    submit(ctx, a, BACKEND_NDL_VIDEO_CONFIG_ONLY, pts, GST_PAD_PROBE_OK);
    GstClockTime aux = submit(ctx, a, BACKEND_NDL_VIDEO_DECODE_ONLY, pts, GST_PAD_PROBE_OK);
    assert(main == pts && aux > main && a->depth == 2);
    receive(a, aux, pts, GST_PAD_PROBE_DROP);
    assert(events == 1 && last_event.decode_only && last_event.pts_ms == 10000);
    assert(last_event.decoded_at_us >= last_event.submitted_at_us);
    receive(a, main, pts, GST_PAD_PROBE_OK);
    assert(events == 2 && !last_event.decode_only);
    assert(a->decoded == 2 && a->suppressed == 1 && a->forwarded == 1 && a->depth == 0);
    main = submit(ctx, a, 0, 0, GST_PAD_PROBE_OK);
    GstBuffer *old = picture_buffer(ctx, 0, pts);
    backend_ndl_gst_flush(ctx);
    receive(a, main, 0, GST_PAD_PROBE_DROP);
    GstPadProbeInfo old_info = {.type = GST_PAD_PROBE_TYPE_BUFFER, .data = old};
    assert(input_probe(NULL, &old_info, a) == GST_PAD_PROBE_DROP && !a->failed);
    gst_buffer_unref(old_info.data);
    aux = submit(ctx, a, BACKEND_NDL_VIDEO_DECODE_ONLY, pts, GST_PAD_PROBE_OK);
    receive(a, aux, pts, GST_PAD_PROBE_DROP);
    GstBuffer *late = picture_buffer(ctx, 0, pts);
    receive(a, aux, pts, GST_PAD_PROBE_DROP);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_FAILED);
    uint32_t previous = a->generation;
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    assert(status(ctx).generation > previous && status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    Attempt *replacement = current(ctx);
    GstPadProbeInfo late_info = {.type = GST_PAD_PROBE_TYPE_BUFFER, .data = late};
    assert(input_probe(NULL, &late_info, replacement) == GST_PAD_PROBE_DROP);
    gst_buffer_unref(late_info.data);
    attempt_unref(replacement);
    fail(a, "late old-pipeline failure");
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    assert(events == 3);
    attempt_unref(a);
    a = current(ctx);
    for (unsigned i = 0; i < PENDING_PICTURES; i++) submit(ctx, a, i % 2, pts, GST_PAD_PROBE_OK);
    submit(ctx, a, BACKEND_NDL_VIDEO_CONFIG_ONLY, pts, GST_PAD_PROBE_OK);
    submit(ctx, a, 0, pts, GST_PAD_PROBE_DROP);
    assert(a->failed && a->depth == PENDING_PICTURES);
    attempt_unref(a);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    a = current(ctx);
    submit(ctx, a, 0, GST_CLOCK_TIME_NONE - GST_MSECOND, GST_PAD_PROBE_DROP);
    assert(a->failed);
    attempt_unref(a);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    a = current(ctx);
    fail_loads = 1;
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_ERROR);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY && status(ctx).generation > a->generation);
    fail(a, "late failure after rollback");
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    attempt_unref(a);
    backend_ndl_close(ctx);
}

static void counter_rollover(void) {
    BackendNdl *ctx = open_backend();
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    Attempt *a = current(ctx);
    GstClockTime pts = (UINT64_C(1) << 32) * GST_MSECOND;
    a->submitted = a->decoded = a->forwarded = UINT32_MAX;
    GstClockTime token = submit(ctx, a, 0, pts, GST_PAD_PROBE_OK);
    receive(a, token, pts, GST_PAD_PROBE_OK);
    BackendNdlVideoStatus stats = status(ctx);
    assert(stats.submitted == 0 && stats.decoded == 0 && stats.forwarded == 0);
    assert(last_event.pts_ms == INT64_C(4294967296));
    a->submitted = a->decoded = a->suppressed = UINT32_MAX;
    token = submit(ctx, a, BACKEND_NDL_VIDEO_DECODE_ONLY, pts, GST_PAD_PROBE_OK);
    receive(a, token, pts, GST_PAD_PROBE_DROP);
    stats = status(ctx);
    assert(stats.submitted == 0 && stats.decoded == 0 && stats.suppressed == 0);

    a->epoch = UINT32_MAX - 1;
    GstBuffer *old = picture_buffer(ctx, 0, pts);
    backend_ndl_gst_flush(ctx);
    assert(a->epoch == UINT32_MAX && !a->failed);
    GstPadProbeInfo info = {.type = GST_PAD_PROBE_TYPE_BUFFER, .data = old};
    assert(input_probe(NULL, &info, a) == GST_PAD_PROBE_DROP);
    gst_buffer_unref(info.data);
    old = picture_buffer(ctx, 0, pts);
    backend_ndl_gst_flush(ctx);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_FAILED);
    const char byte = 0;
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, true) == BACKEND_NDL_ERROR);

    const uint32_t max_generation = UINT32_MAX >> BACKEND_NDL_FEED_GENERATION_SHIFT;
    ctx->gst->generation = max_generation;
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    stats = status(ctx);
    assert(stats.generation == 1 && stats.state == BACKEND_NDL_VIDEO_PATH_READY);
    Attempt *replacement = current(ctx);
    assert(replacement->generation == a->generation);
    assert(replacement->epoch == 1);
    info.data = old;
    assert(input_probe(NULL, &info, replacement) == GST_PAD_PROBE_DROP);
    gst_buffer_unref(info.data);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    token = submit(ctx, replacement, 0, pts, GST_PAD_PROBE_OK);
    receive(replacement, token, pts, GST_PAD_PROBE_OK);
    assert(last_event.generation == 1);
    attempt_unref(replacement);
    attempt_unref(a);

    configure(ctx, BACKEND_NDL_APP_DEFAULT, BACKEND_NDL_APP_DEFAULT);
    const uint32_t generations[] = {max_generation, 1};
    for (unsigned i = 0; i < G_N_ELEMENTS(generations); i++) {
        uint32_t expected = generations[i];
        ctx->reload_generation = expected == 1 ? max_generation : expected - 1;
        atomic_store(&ctx->video_stats_epoch, UINT32_MAX);
        assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
        uint32_t state = atomic_load(&ctx->feed_state);
        assert(ctx->reload_generation == expected);
        assert(backend_ndl_feed_generation(state) == expected);
        assert(atomic_load(&ctx->video_stats_epoch) == 1);
        assert(status(ctx).generation == expected);
        assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
        atomic_store(&ctx->loaded_start_ms, backend_ndl_monotonic_ms() - 100);
        atomic_store(&ctx->video_feed_count, UINT32_MAX);
        atomic_store(&ctx->audio_feed_count, UINT32_MAX);
        assert(backend_ndl_media_time_ms(ctx) >= 100);
        assert(backend_ndl_feed_video(ctx, &byte, 1, BACKEND_NDL_PTS_AUTO, true) == BACKEND_NDL_OK);
        assert(backend_ndl_feed_audio(ctx, &byte, 1, BACKEND_NDL_PTS_AUTO) == BACKEND_NDL_OK);
        assert(atomic_load(&ctx->video_feed_count) == 0);
        assert(atomic_load(&ctx->audio_feed_count) == 0);
        BackendNdlRuntimeStats runtime;
        assert(backend_ndl_get_runtime_stats(ctx, &runtime) == BACKEND_NDL_OK);
        assert(runtime.video_play_samples == 1 && runtime.video_play_errors == 0);
    }
    backend_ndl_close(ctx);
}

static void queue_and_api(void) {
    BackendNdl *ctx = open_backend();
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    const char byte = 0;
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 1, true, BACKEND_NDL_VIDEO_CONFIG_ONLY) == BACKEND_NDL_INVALID_ARGUMENT);
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 1, false, 3) == BACKEND_NDL_INVALID_ARGUMENT);
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 1, true, 4) == BACKEND_NDL_INVALID_ARGUMENT);
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 1, false, BACKEND_NDL_VIDEO_CONFIG_ONLY) == BACKEND_NDL_NEED_KEYFRAME);
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, false) == BACKEND_NDL_NEED_KEYFRAME);
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, true) == BACKEND_NDL_OK);
    assert(backend_ndl_feed_video_ex(ctx, &byte, 1, 1, false, BACKEND_NDL_VIDEO_CONFIG_ONLY) == BACKEND_NDL_OK);
    for (unsigned i = 2; i < QUEUE_BUFFERS; i++)
        assert(backend_ndl_feed_video(ctx, &byte, 1, 1, false) == BACKEND_NDL_OK);
    assert(status(ctx).queued_buffers == QUEUE_BUFFERS);
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, false) == BACKEND_NDL_ERROR);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_FAILED);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    assert(status(ctx).queued_buffers == 0);
    void *bytes = g_malloc0(QUEUE_BYTES);
    assert(backend_ndl_feed_video(ctx, bytes, QUEUE_BYTES, 1, true) == BACKEND_NDL_OK);
    g_free(bytes);
    assert(status(ctx).queued_bytes == QUEUE_BYTES);
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, false) == BACKEND_NDL_ERROR);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    GstElement *retained = gst_object_ref(source);
    gst_element_set_state(source, GST_STATE_NULL);
    assert(backend_ndl_feed_video(ctx, &byte, 1, 1, true) == BACKEND_NDL_ERROR);
    backend_ndl_close(ctx);
    gst_object_unref(retained);
}

static void post_error(GstBus *bus, GstElement *element) {
    GError *error = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "intentional test error");
    gst_bus_post(bus, gst_message_new_error(GST_OBJECT(element), error, NULL));
    g_error_free(error);
}
static void bus_errors(void) {
    BackendNdl *ctx = open_backend();
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    GstElement *other = gst_element_factory_make("identity", NULL);
    post_error(bus, other);
    gst_object_unref(other);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    post_error(bus, audio_sink);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    post_error(bus, decoder);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_FAILED);
    GstElement *old = gst_object_ref(decoder);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    post_error(bus, old);
    assert(status(ctx).state == BACKEND_NDL_VIDEO_PATH_READY);
    gst_object_unref(old);
    gst_object_unref(bus);
    backend_ndl_close(ctx);
}

static GMutex callback_lock;
static GCond callback_changed;
static bool callback_entered, callback_release;
static gint close_done;
static void blocking_frame(void *data, const BackendNdlVideoFrameEvent *event) {
    (void)data; (void)event;
    g_mutex_lock(&callback_lock);
    callback_entered = true;
    g_cond_broadcast(&callback_changed);
    while (!callback_release) g_cond_wait(&callback_changed, &callback_lock);
    g_mutex_unlock(&callback_lock);
}
static gpointer produce_output(gpointer data) {
    Attempt *a = data;
    receive(a, a->last_token, 10 * GST_SECOND, GST_PAD_PROBE_OK);
    return NULL;
}
static gpointer close_backend(gpointer data) {
    backend_ndl_close(data);
    g_atomic_int_set(&close_done, 1);
    return NULL;
}
static void closing_and_metadata_errors(void) {
    BackendNdl *ctx = open_backend();
    configure(ctx, BACKEND_NDL_APP_RTC, BACKEND_NDL_APP_RTC);
    Attempt *a = current(ctx);
    GstBuffer *buffer = picture_buffer(ctx, 0, 10);
    GstPadProbeInfo info = {.type = GST_PAD_PROBE_TYPE_BUFFER, .data = gst_buffer_ref(buffer)};
    assert(timestamp_units(NULL, &info, a) == GST_PAD_PROBE_OK);
    assert(GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(&info)) == 10 * GST_MSECOND);
    assert(GST_BUFFER_PTS(buffer) == 10);
    gst_buffer_unref(info.data);
    info.data = buffer;
    GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
    assert(timestamp_units(NULL, &info, a) == GST_PAD_PROBE_DROP && a->failed);
    gst_buffer_unref(info.data);
    attempt_unref(a);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    a = current(ctx);
    info.data = gst_buffer_new();
    assert(input_probe(NULL, &info, a) == GST_PAD_PROBE_DROP && a->failed);
    gst_buffer_unref(info.data);
    attempt_unref(a);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    a = current(ctx);
    buffer = picture_buffer(ctx, 0, 10 * GST_SECOND);
    GstBuffer *copy = gst_buffer_copy_deep(buffer);
    info.data = buffer;
    assert(input_probe(NULL, &info, a) == GST_PAD_PROBE_OK);
    gst_buffer_unref(info.data);
    info.data = copy;
    assert(input_probe(NULL, &info, a) == GST_PAD_PROBE_DROP && a->failed);
    gst_buffer_unref(info.data);
    attempt_unref(a);
    assert(backend_ndl_reload(ctx) == BACKEND_NDL_OK);
    a = current(ctx);
    ctx->config.video_frame_fn = blocking_frame;
    submit(ctx, a, 0, 10 * GST_SECOND, GST_PAD_PROBE_OK);
    g_mutex_init(&callback_lock);
    g_cond_init(&callback_changed);
    GThread *producer = g_thread_new("output", produce_output, a);
    g_mutex_lock(&callback_lock);
    while (!callback_entered) g_cond_wait(&callback_changed, &callback_lock);
    g_mutex_unlock(&callback_lock);
    GThread *closer = g_thread_new("close", close_backend, ctx);
    gint64 deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    bool retired = false;
    while (!retired && g_get_monotonic_time() < deadline) {
        g_mutex_lock(&a->lock);
        retired = a->closed;
        g_mutex_unlock(&a->lock);
        if (!retired) g_usleep(1000);
    }
    assert(retired && !g_atomic_int_get(&close_done));
    g_mutex_lock(&callback_lock);
    callback_release = true;
    g_cond_broadcast(&callback_changed);
    g_mutex_unlock(&callback_lock);
    g_thread_join(producer);
    g_thread_join(closer);
    assert(g_atomic_int_get(&close_done));
    receive(a, a->last_token + GST_MSECOND, 0, GST_PAD_PROBE_DROP);
    attempt_unref(a);
    g_cond_clear(&callback_changed);
    g_mutex_clear(&callback_lock);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    register_fake("GstOMXH264DecBin", "ndl-test-decoder");
    register_fake("GstRvSinkBin", "ndl-test-video-sink");
    register_fake("GstRaSinkBin", "ndl-test-audio-sink");
    profiles();
    pictures();
    counter_rollover();
    queue_and_api();
    closing_and_metadata_errors();
    bus_errors();
    g_private_replace(&construction, NULL);
    gst_deinit();
    return 0;
}

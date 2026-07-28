/* SPDX-License-Identifier: MIT */
#include "backend_ndl_api.h"

#include <gtest/gtest.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#define FAKE_MAX_PLAYS 64

typedef struct FakeNdl {
    unsigned sequence;
    unsigned window_sequence;
    unsigned init_sequence;
    int get_error_calls;
    int set_window_calls;
    int init_calls;
    int quit_calls;
    int app_state_calls;
    int load_calls;
    int unload_calls;
    int video_play_calls;
    int video_area_calls;
    int video_hdr_calls;
    int video_threshold_calls;
    int video_queue_calls;
    int video_flush_calls;
    int audio_play_calls;
    int audio_total_calls;
    int audio_available_calls;
    int audio_multichannel_calls;
    int audio_register_calls;
    int effect_load_calls;
    int effect_unload_calls;
    int effect_play_calls;
    int effect_available_calls;

    bool fail_init;
    bool fail_load;
    bool fail_unload;
    int fail_load_remaining;
    bool fail_video_play;
    bool fail_audio_play;
    bool fail_effect_unload;
    bool fail_effect_play;
    bool legacy_init_callback_was_null;
    int audio_available;
    int audio_total;
    unsigned int effect_available;
    unsigned int effect_preferred;
    int video_queue;
    int multichannel_query;

    char app_id[128];
    char window_id[128];
    NDL_DIRECTMEDIA_APP_STATE last_app_state;
    NDL_DIRECTMEDIA_DATA_INFO_T last_load;
    BackendNdlLegacyMediaInfo last_legacy_load;
    NDL_DIRECTAUDIO_PCM_INFO_T last_effect_pcm;
    NDL_DIRECTVIDEO_HDR_INFO_T last_hdr;
    int last_area[4];
    int last_threshold;
    long long last_video_pts;
    unsigned int audio_sizes[FAKE_MAX_PLAYS];
    unsigned char audio_prefix[FAKE_MAX_PLAYS][4];
    long long audio_pts[FAKE_MAX_PLAYS];
    StateCallbackFP media_callback;
    MultiChannelPCMCallbackFP multichannel_callback;
} FakeNdl;

static FakeNdl g_fake;

static void fake_reset(void) {
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.audio_available = 1 << 20;
    g_fake.audio_total = 2 << 20;
    g_fake.effect_available = 4096;
    g_fake.effect_preferred = 512;
    g_fake.video_queue = 3;
    g_fake.multichannel_query = 3;
}

static const char *fake_get_error(void) {
    g_fake.get_error_calls++;
    return "fake error";
}

static int fake_set_window(const char *window_id) {
    g_fake.set_window_calls++;
    g_fake.window_sequence = ++g_fake.sequence;
    snprintf(g_fake.window_id, sizeof(g_fake.window_id), "%s", window_id ? window_id : "");
    return 0;
}

static int fake_init(const char *app_id) {
    g_fake.init_calls++;
    g_fake.init_sequence = ++g_fake.sequence;
    snprintf(g_fake.app_id, sizeof(g_fake.app_id), "%s", app_id ? app_id : "");
    return g_fake.fail_init ? -1 : 0;
}

static int fake_legacy_init(const char *app_id,
                            BackendNdlLegacyResourceReleasedFn callback) {
    g_fake.legacy_init_callback_was_null = callback == nullptr;
    return fake_init(app_id);
}

static int fake_quit(void) { g_fake.quit_calls++; return 0; }
static int fake_set_app_state(NDL_DIRECTMEDIA_APP_STATE state) {
    g_fake.app_state_calls++;
    g_fake.last_app_state = state;
    return 0;
}
static int fake_load(NDL_DIRECTMEDIA_DATA_INFO_T *data, StateCallbackFP cb) {
    g_fake.load_calls++;
    g_fake.last_load = *data;
    g_fake.media_callback = cb;
    if (g_fake.fail_load_remaining > 0) {
        g_fake.fail_load_remaining--;
        return -1;
    }
    return g_fake.fail_load ? -1 : 0;
}
static int fake_legacy_load(BackendNdlLegacyMediaInfo *data,
                            BackendNdlLegacyStateCallbackFn cb) {
    g_fake.load_calls++;
    g_fake.last_legacy_load = *data;
    g_fake.media_callback = cb;
    if (g_fake.fail_load_remaining > 0) {
        g_fake.fail_load_remaining--;
        return -1;
    }
    return g_fake.fail_load ? -1 : 0;
}
static int fake_unload(void) {
    g_fake.unload_calls++;
    return g_fake.fail_unload ? -1 : 0;
}
static int fake_audio_play(void *data, unsigned int size, long long pts) {
    if (g_fake.audio_play_calls < FAKE_MAX_PLAYS) {
        g_fake.audio_sizes[g_fake.audio_play_calls] = size;
        g_fake.audio_pts[g_fake.audio_play_calls] = pts;
        if (data && size) {
            size_t copy = size < 4u ? size : 4u;
            memcpy(g_fake.audio_prefix[g_fake.audio_play_calls], data, copy);
        }
    }
    g_fake.audio_play_calls++;
    return g_fake.fail_audio_play ? -1 : 0;
}
static int fake_audio_total(int *total) {
    g_fake.audio_total_calls++;
    *total = g_fake.audio_total;
    return 0;
}
static int fake_audio_available(int *available) {
    g_fake.audio_available_calls++;
    *available = g_fake.audio_available;
    return 0;
}
static int fake_multichannel(int *status) {
    g_fake.audio_multichannel_calls++;
    *status = g_fake.multichannel_query;
    return 0;
}
static int fake_register_multichannel(MultiChannelPCMCallbackFP cb) {
    g_fake.audio_register_calls++;
    g_fake.multichannel_callback = cb;
    return 0;
}
static int fake_video_play(void *data, unsigned int size, long long pts) {
    (void)data;
    (void)size;
    g_fake.video_play_calls++;
    g_fake.last_video_pts = pts;
    return g_fake.fail_video_play ? -1 : 0;
}
static int fake_video_area(int left, int top, int width, int height) {
    g_fake.video_area_calls++;
    g_fake.last_area[0] = left;
    g_fake.last_area[1] = top;
    g_fake.last_area[2] = width;
    g_fake.last_area[3] = height;
    return 0;
}
static int fake_video_hdr(NDL_DIRECTVIDEO_HDR_INFO_T info) {
    g_fake.video_hdr_calls++;
    g_fake.last_hdr = info;
    return 0;
}
static int fake_video_threshold(int threshold) {
    g_fake.video_threshold_calls++;
    g_fake.last_threshold = threshold;
    return 0;
}
static int fake_video_queue(int *length) {
    g_fake.video_queue_calls++;
    *length = g_fake.video_queue;
    return 0;
}
static int fake_video_flush(void) { g_fake.video_flush_calls++; return 0; }
static int fake_effect_load(NDL_DIRECTAUDIO_PCM_INFO_T *pcm, unsigned int *preferred) {
    g_fake.effect_load_calls++;
    g_fake.last_effect_pcm = *pcm;
    *preferred = g_fake.effect_preferred;
    return 0;
}
static int fake_effect_unload(void) {
    g_fake.effect_unload_calls++;
    return g_fake.fail_effect_unload ? -1 : 0;
}
static int fake_effect_play(void *data, unsigned int size) {
    (void)data;
    (void)size;
    g_fake.effect_play_calls++;
    return g_fake.fail_effect_play ? -1 : 0;
}
static int fake_effect_available(unsigned int *available) {
    g_fake.effect_available_calls++;
    *available = g_fake.effect_available;
    return 0;
}
static BackendNdlApi fake_api_full(void) {
    BackendNdlApi api;
    memset(&api, 0, sizeof(api));
    api.DirectMediaGetError = fake_get_error;
    api.DirectMediaSetWindowId = fake_set_window;
    api.DirectMediaInitCurrent = fake_init;
    api.DirectMediaQuit = fake_quit;
    api.DirectMediaSetAppState = fake_set_app_state;
    api.DirectMediaLoadCurrent = fake_load;
    api.DirectMediaUnload = fake_unload;
    api.DirectAudioPlay = fake_audio_play;
    api.DirectAudioGetTotalBufferSize = fake_audio_total;
    api.DirectAudioGetAvailableBufferSize = fake_audio_available;
    api.DirectAudioSupportMultiChannel = fake_multichannel;
    api.DirectAudioRegisterCallback = fake_register_multichannel;
    api.DirectVideoPlay = fake_video_play;
    api.DirectVideoSetArea = fake_video_area;
    api.DirectVideoSetHDRInfo = fake_video_hdr;
    api.DirectVideoSetFrameDropThreshold = fake_video_threshold;
    api.DirectVideoGetRenderBufferLength = fake_video_queue;
    api.DirectVideoFlushRenderBuffer = fake_video_flush;
    api.DirectEffectLoad = fake_effect_load;
    api.DirectEffectUnload = fake_effect_unload;
    api.DirectEffectPlay = fake_effect_play;
    api.DirectEffectGetAvailableBufferSize = fake_effect_available;
    return api;
}

static BackendNdlApi fake_api_legacy(void) {
    BackendNdlApi api = fake_api_full();
    api.DirectMediaSetWindowId = nullptr;
    api.DirectMediaInitCurrent = nullptr;
    api.DirectMediaLoadCurrent = nullptr;
    api.DirectMediaInitCallback = fake_legacy_init;
    api.DirectMediaLoadCallback = fake_legacy_load;
    return api;
}

static BackendNdlConfig test_config(void) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.backend.ndl";
    config.require_keyframe_after_reload = true;
    config.prime_pcm_after_load = true;
    return config;
}

static BackendNdl *open_test_backend(const BackendNdlApi *api) {
    BackendNdlConfig config = test_config();
    return backend_ndl_open_with_api(&config, BACKEND_NDL_ABI_V2_WINDOW_ID_INIT, api);
}

static BackendNdl *open_legacy_backend(const BackendNdlApi *api,
                                       const char *sdk_version = "5.0.0") {
    BackendNdlConfig config = test_config();
    config.webos_sdk_version = sdk_version;
    return backend_ndl_open_with_api(&config, BACKEND_NDL_ABI_V2_CALLBACK_INIT, api);
}

struct BackendNdlCloser {
    void operator()(BackendNdl *ctx) const { backend_ndl_close(ctx); }
};

using BackendNdlHandle = std::unique_ptr<BackendNdl, BackendNdlCloser>;

static const BackendNdlVideoInfo k_h264 = {
    .codec = BACKEND_NDL_VIDEO_H264,
    .width = 1920,
    .height = 1080,
};
static const BackendNdlVideoInfo k_vp9 = {
    .codec = BACKEND_NDL_VIDEO_VP9,
    .width = 3840,
    .height = 2160,
};
static const BackendNdlAudioInfo k_stereo = {
    .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
    .sample_rate_hz = 48000,
    .channels = 2,
};
static const BackendNdlPcmInfo k_effect_stereo = {
    .sample_rate_hz = 48000,
    .channels = 2,
};
static const uint8_t k_data[1920] = {0, 0, 0, 1, 0x65};

typedef struct Capture {
    int logs;
    int events;
    int event_type;
    int multichannel_events;
    BackendNdlMultichannelStatus multichannel_status;
} Capture;

static void capture_log(void *userdata, BackendNdlLogLevel level, const char *message) {
    (void)level;
    (void)message;
    ((Capture *)userdata)->logs++;
}
static void capture_event(void *userdata, int type, int64_t number, const char *text) {
    (void)number;
    (void)text;
    Capture *capture = (Capture *)userdata;
    capture->events++;
    capture->event_type = type;
}
static void capture_multichannel(void *userdata, BackendNdlMultichannelStatus status,
                                 const char *detail) {
    (void)detail;
    Capture *capture = (Capture *)userdata;
    capture->multichannel_events++;
    capture->multichannel_status = status;
}

TEST(BackendNdl, DefaultsOpenAndCallbacks) {
    BackendNdlConfig defaults{};
    backend_ndl_config_defaults(&defaults);
    EXPECT_EQ(defaults.app_id, nullptr);
    EXPECT_EQ(defaults.window_id, nullptr);
    EXPECT_EQ(defaults.webos_sdk_version, nullptr);
    EXPECT_EQ(defaults.minimum_log_level, BACKEND_NDL_LOG_INFO);
    EXPECT_TRUE(defaults.guard_audio_overflow);

    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlConfig config = test_config();
    config.window_id = "window-42";
    Capture capture{};
    config.log_fn = capture_log;
    config.media_event_fn = capture_event;
    config.multichannel_changed_fn = capture_multichannel;
    config.userdata = &capture;
    BackendNdlHandle ctx(backend_ndl_open_with_api(
        &config, BACKEND_NDL_ABI_V2_WINDOW_ID_INIT, &api));
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(g_fake.set_window_calls, 1);
    EXPECT_STREQ(g_fake.window_id, "window-42");
    EXPECT_LT(g_fake.window_sequence, g_fake.init_sequence);
    EXPECT_EQ(g_fake.init_calls, 1);
    EXPECT_STREQ(g_fake.app_id, "test.backend.ndl");
    EXPECT_EQ(backend_ndl_get_abi_profile(ctx.get()),
              BACKEND_NDL_ABI_V2_WINDOW_ID_INIT);
    EXPECT_EQ(g_fake.audio_register_calls, 1);
    EXPECT_EQ(g_fake.audio_multichannel_calls, 1);

    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_OK);
    ASSERT_NE(g_fake.media_callback, nullptr);
    g_fake.media_callback(0x16, 7, "ready");
    EXPECT_EQ(capture.events, 1);
    EXPECT_EQ(capture.event_type, 0x16);

    ASSERT_NE(g_fake.multichannel_callback, nullptr);
    g_fake.multichannel_callback(1, "pcm");
    EXPECT_EQ(capture.multichannel_events, 1);
    EXPECT_EQ(capture.multichannel_status,
              BACKEND_NDL_MULTICHANNEL_OUTPUT_MODE_BLOCKED);

    StateCallbackFP late_media = g_fake.media_callback;
    MultiChannelPCMCallbackFP late_multi = g_fake.multichannel_callback;
    ctx.reset();
    EXPECT_EQ(g_fake.quit_calls, 1);

    late_media(0x17, 0, "late");
    late_multi(2, "late");
    EXPECT_EQ(capture.events, 1);
    EXPECT_EQ(capture.multichannel_events, 1);
}

static BackendNdlCapabilities full_capabilities(void) {
    return BACKEND_NDL_CAP_APP_STATE | BACKEND_NDL_CAP_WINDOW_ID |
           BACKEND_NDL_CAP_VIDEO_PLAY |
           BACKEND_NDL_CAP_VIDEO_AREA | BACKEND_NDL_CAP_VIDEO_FRAME_DROP |
           BACKEND_NDL_CAP_VIDEO_HDR | BACKEND_NDL_CAP_VIDEO_QUEUE_LENGTH |
           BACKEND_NDL_CAP_VIDEO_FLUSH | BACKEND_NDL_CAP_AUDIO_PLAY |
           BACKEND_NDL_CAP_AUDIO_AVAILABLE | BACKEND_NDL_CAP_AUDIO_TOTAL |
           BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY |
           BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_CALLBACK | BACKEND_NDL_CAP_EFFECT;
}

TEST(BackendNdl, CapabilitiesAndOptionalTracks) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    {
        BackendNdlHandle ctx(open_test_backend(&api));
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(backend_ndl_get_capabilities(ctx.get()), full_capabilities());
    }

    fake_reset();
    api = fake_api_full();
    api.DirectAudioPlay = nullptr;
    {
        BackendNdlHandle ctx(open_test_backend(&api));
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(backend_ndl_get_capabilities(ctx.get()) & BACKEND_NDL_CAP_AUDIO_PLAY, 0u);
        EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_OK);
        EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &k_stereo),
                  BACKEND_NDL_UNAVAILABLE);
    }

    fake_reset();
    api = fake_api_full();
    api.DirectVideoPlay = nullptr;
    {
        BackendNdlHandle ctx(open_test_backend(&api));
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &k_stereo), BACKEND_NDL_OK);
        EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr),
                  BACKEND_NDL_UNAVAILABLE);
    }

    fake_reset();
    api = fake_api_full();
    api.DirectMediaSetWindowId = nullptr;
    BackendNdlHandle rejected(open_test_backend(&api));
    EXPECT_EQ(rejected, nullptr);
    EXPECT_EQ(g_fake.init_calls, 0);
}

TEST(BackendNdl, MediaLayoutRatesAndGating) {
    static const struct {
        BackendNdlVideoCodec codec;
        NDL_DIRECTVIDEO_SRC_TYPE source;
    } codecs[] = {
        {BACKEND_NDL_VIDEO_H264, NDL_DIRECTVIDEO_SRC_TYPE_H264},
        {BACKEND_NDL_VIDEO_H265, NDL_DIRECTVIDEO_SRC_TYPE_HEVC},
        {BACKEND_NDL_VIDEO_VP9, NDL_DIRECTVIDEO_SRC_TYPE_VP9},
        {BACKEND_NDL_VIDEO_AV1, NDL_DIRECTVIDEO_SRC_TYPE_AV1},
    };
    static const struct {
        uint32_t hz;
        NDL_DIRECTAUDIO_SAMPLERATE_T code;
    } rates[] = {
        {48000, NDL_DIRECTAUDIO_SAMPLERATE_48K},
        {44100, NDL_DIRECTAUDIO_SAMPLERATE_44_1K},
        {32000, NDL_DIRECTAUDIO_SAMPLERATE_32K},
        {24000, NDL_DIRECTAUDIO_SAMPLERATE_24K},
        {16000, NDL_DIRECTAUDIO_SAMPLERATE_16K},
        {12000, NDL_DIRECTAUDIO_SAMPLERATE_12K},
        {8000, NDL_DIRECTAUDIO_SAMPLERATE_8K},
        {22050, NDL_DIRECTAUDIO_SAMPLERATE_22_05K},
    };

    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.last_load.videoDataInfo.source, NDL_DIRECTVIDEO_SRC_TYPE_H264);
    EXPECT_EQ(g_fake.last_load.videoDataInfo.width, 1920);
    EXPECT_EQ(g_fake.last_load.audioDataInfo.audioCodec, NDL_DIRECTAUDIO_SRC_TYPE_PCM);
    EXPECT_STREQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.format, "S16LE");
    EXPECT_STREQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.layout, "interleaved");
    EXPECT_STREQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.channelMode, "stereo");
    EXPECT_EQ(g_fake.audio_play_calls, 1);
    EXPECT_EQ(g_fake.audio_sizes[0], 4u);

    for (size_t i = 0; i < sizeof(codecs) / sizeof(codecs[0]); i++) {
        SCOPED_TRACE(::testing::Message() << "codec index " << i);
        BackendNdlVideoInfo video = {codecs[i].codec, 1280, 720};
        ASSERT_EQ(backend_ndl_set_media(ctx.get(), &video, nullptr), BACKEND_NDL_OK);
        EXPECT_EQ(g_fake.last_load.videoDataInfo.source, codecs[i].source);
    }

    BackendNdlAudioInfo mono = {
        .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
        .sample_rate_hz = 48000,
        .channels = 1,
    };
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &mono), BACKEND_NDL_OK);
    EXPECT_STREQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.channelMode, "mono");

    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        SCOPED_TRACE(::testing::Message() << "sample rate " << rates[i].hz);
        BackendNdlAudioInfo pcm = {
            .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
            .sample_rate_hz = rates[i].hz,
            .channels = 2,
        };
        ASSERT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &pcm), BACKEND_NDL_OK);
        EXPECT_EQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.sampleRate,
                  rates[i].code);
        EXPECT_TRUE(backend_ndl_pcm_sample_rate_supported(rates[i].hz));
    }
    EXPECT_FALSE(backend_ndl_pcm_sample_rate_supported(11025));

    BackendNdlAudioInfo surround = {
        .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
        .sample_rate_hz = 48000,
        .channels = 6,
    };
    ASSERT_NE(g_fake.multichannel_callback, nullptr);
    g_fake.multichannel_callback(1, "blocked");
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &surround), BACKEND_NDL_UNSUPPORTED);
    g_fake.multichannel_callback(2, "ready");
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &surround), BACKEND_NDL_OK);
    EXPECT_STREQ(g_fake.last_load.audioDataInfo.audio_data.pcmInfo.channelMode,
                 "6-channel");

    for (int raw = 0; raw <= 3; raw++) {
        SCOPED_TRACE(::testing::Message() << "multichannel state " << raw);
        BackendNdlMultichannelStatus status = BACKEND_NDL_MULTICHANNEL_UNKNOWN;
        g_fake.multichannel_query = raw;
        EXPECT_EQ(backend_ndl_get_multichannel_status(ctx.get(), &status), BACKEND_NDL_OK);
        EXPECT_EQ(status, static_cast<BackendNdlMultichannelStatus>(raw));
    }
}

TEST(BackendNdl, LegacyProfileLayoutRatesAndCapabilityMask) {
    static const struct {
        uint32_t hz;
        int code;
    } rates[] = {
        {48000, BACKEND_NDL_LEGACY_PCM_48K},
        {44100, BACKEND_NDL_LEGACY_PCM_44_1K},
        {32000, BACKEND_NDL_LEGACY_PCM_32K},
        {24000, BACKEND_NDL_LEGACY_PCM_24K},
        {22050, BACKEND_NDL_LEGACY_PCM_22_05K},
        {16000, BACKEND_NDL_LEGACY_PCM_16K},
        {12000, BACKEND_NDL_LEGACY_PCM_12K},
        {8000, BACKEND_NDL_LEGACY_PCM_8K},
    };

    fake_reset();
    BackendNdlApi api = fake_api_legacy();
    BackendNdlHandle ctx(open_legacy_backend(&api));
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(backend_ndl_get_abi_profile(ctx.get()),
              BACKEND_NDL_ABI_V2_CALLBACK_INIT);
    EXPECT_STREQ(backend_ndl_abi_profile_name(backend_ndl_get_abi_profile(ctx.get())),
                 "v2-callback-init");
    EXPECT_TRUE(g_fake.legacy_init_callback_was_null);
    EXPECT_EQ(g_fake.audio_multichannel_calls, 0);
    EXPECT_EQ(g_fake.audio_register_calls, 0);

    BackendNdlCapabilities caps = backend_ndl_get_capabilities(ctx.get());
    EXPECT_EQ(caps & BACKEND_NDL_CAP_WINDOW_ID, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_DL_LIFECYCLE, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_VIDEO_HDR, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_EFFECT, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY, 0u);
    EXPECT_NE(caps & BACKEND_NDL_CAP_VIDEO_PLAY, 0u);
    EXPECT_NE(caps & BACKEND_NDL_CAP_AUDIO_PLAY, 0u);

    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.last_legacy_load.video.width, 1920);
    EXPECT_EQ(g_fake.last_legacy_load.video.height, 1080);
    EXPECT_EQ(g_fake.last_legacy_load.video.type, BACKEND_NDL_LEGACY_VIDEO_H264);
    EXPECT_EQ(g_fake.last_legacy_load.audio.pcm.type, BACKEND_NDL_LEGACY_AUDIO_PCM);
    EXPECT_STREQ(g_fake.last_legacy_load.audio.pcm.format, "S16LE");
    EXPECT_STREQ(g_fake.last_legacy_load.audio.pcm.layout, "interleaved");
    EXPECT_STREQ(g_fake.last_legacy_load.audio.pcm.channel_mode, "stereo");

    for (const auto &rate : rates) {
        SCOPED_TRACE(::testing::Message() << "legacy sample rate " << rate.hz);
        BackendNdlAudioInfo pcm = {
            .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
            .sample_rate_hz = rate.hz,
            .channels = 2,
        };
        ASSERT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &pcm), BACKEND_NDL_OK);
        EXPECT_EQ(g_fake.last_legacy_load.audio.pcm.sample_rate, rate.code);
    }

    BackendNdlVideoInfo vp9 = k_vp9;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &vp9, nullptr), BACKEND_NDL_UNSUPPORTED);
    BackendNdlAudioInfo surround = {
        .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
        .sample_rate_hz = 48000,
        .channels = 6,
    };
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &surround),
              BACKEND_NDL_UNSUPPORTED);
    BackendNdlHdrInfo hdr{};
    EXPECT_EQ(backend_ndl_set_video_hdr_info(ctx.get(), &hdr), BACKEND_NDL_UNAVAILABLE);
    uint32_t preferred = 0;
    EXPECT_EQ(backend_ndl_effect_load(ctx.get(), &k_effect_stereo, &preferred),
              BACKEND_NDL_UNAVAILABLE);

    ctx.reset();

    fake_reset();
    api = fake_api_legacy();
    BackendNdlConfig window_config = test_config();
    window_config.window_id = "unsupported-window";
    BackendNdlHandle rejected(backend_ndl_open_with_api(
        &window_config, BACKEND_NDL_ABI_V2_CALLBACK_INIT, &api));
    EXPECT_EQ(rejected, nullptr);
    EXPECT_EQ(g_fake.init_calls, 0);

    fake_reset();
    api = fake_api_legacy();
    BackendNdlConfig old_config = test_config();
    old_config.webos_sdk_version = "4.0.0";
    rejected.reset(backend_ndl_open_with_api(
        &old_config, BACKEND_NDL_ABI_V2_CALLBACK_INIT, &api));
    EXPECT_EQ(rejected, nullptr);
    EXPECT_EQ(g_fake.init_calls, 0);
}

TEST(BackendNdl, OpusLayoutAndSilencePrimeForCurrentAndLegacyAbi) {
    const BackendNdlAudioInfo opus = {
        .codec = BACKEND_NDL_AUDIO_OPUS,
        .sample_rate_hz = 48000,
        .channels = 2,
    };
    static const unsigned char silence[] = {0xec, 0xff, 0xfe};

    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle current(open_test_backend(&api));
    ASSERT_NE(current, nullptr);
    ASSERT_EQ(backend_ndl_set_media(current.get(), &k_h264, &opus), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.last_load.audioDataInfo.audioCodec, NDL_DIRECTAUDIO_SRC_TYPE_OPUS);
    EXPECT_EQ(g_fake.last_load.audioDataInfo.audio_data.opusInfo.channels, 2);
    EXPECT_DOUBLE_EQ(g_fake.last_load.audioDataInfo.audio_data.opusInfo.sampleRate, 48.0);
    EXPECT_EQ(g_fake.last_load.audioDataInfo.audio_data.opusInfo.streamHeader, nullptr);
    ASSERT_EQ(g_fake.audio_play_calls, 1);
    EXPECT_EQ(g_fake.audio_sizes[0], sizeof(silence));
    EXPECT_EQ(memcmp(g_fake.audio_prefix[0], silence, sizeof(silence)), 0);
    current.reset();

    fake_reset();
    api = fake_api_legacy();
    BackendNdlHandle legacy(open_legacy_backend(&api));
    ASSERT_NE(legacy, nullptr);
    ASSERT_EQ(backend_ndl_set_media(legacy.get(), &k_h264, &opus), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.last_legacy_load.audio.opus.type, BACKEND_NDL_LEGACY_AUDIO_OPUS);
    EXPECT_EQ(g_fake.last_legacy_load.audio.opus.channels, 2);
    EXPECT_DOUBLE_EQ(g_fake.last_legacy_load.audio.opus.sample_rate, 48.0);
    EXPECT_EQ(g_fake.last_legacy_load.audio.opus.stream_header, nullptr);
    ASSERT_EQ(g_fake.audio_play_calls, 1);
    EXPECT_EQ(g_fake.audio_sizes[0], sizeof(silence));
    EXPECT_EQ(memcmp(g_fake.audio_prefix[0], silence, sizeof(silence)), 0);
}

TEST(BackendNdl, OpusPrimeRejectionRestoresPreviousMedia) {
    const BackendNdlAudioInfo opus = {
        .codec = BACKEND_NDL_AUDIO_OPUS,
        .sample_rate_hz = 48000,
        .channels = 2,
    };
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);

    g_fake.fail_audio_play = true;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &opus), BACKEND_NDL_UNSUPPORTED);
    g_fake.fail_audio_play = false;

    EXPECT_EQ(g_fake.last_load.audioDataInfo.audioCodec, NDL_DIRECTAUDIO_SRC_TYPE_PCM);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false),
              BACKEND_NDL_NEED_KEYFRAME);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, BACKEND_NDL_PTS_AUTO),
              BACKEND_NDL_OK);
}

TEST(BackendNdl, FailedFirstOpusPrimeUnloadCanBeRetried) {
    const BackendNdlAudioInfo opus = {
        .codec = BACKEND_NDL_AUDIO_OPUS,
        .sample_rate_hz = 48000,
        .channels = 2,
    };
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);

    g_fake.fail_audio_play = true;
    g_fake.fail_unload = true;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &opus),
              BACKEND_NDL_UNSUPPORTED);
    EXPECT_EQ(g_fake.load_calls, 1);
    EXPECT_EQ(g_fake.unload_calls, 1);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0),
              BACKEND_NDL_NOT_READY);

    g_fake.fail_audio_play = false;
    g_fake.fail_unload = false;
    EXPECT_EQ(backend_ndl_unload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.unload_calls, 2);
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &k_stereo),
              BACKEND_NDL_OK);
}

TEST(BackendNdl, VideoAudioAndEffectControls) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(backend_ndl_set_video_area(ctx.get(), 10, 20, 1280, 720), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_area_calls, 0);
    EXPECT_EQ(backend_ndl_set_video_frame_drop_threshold(ctx.get(), 2), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_threshold_calls, 0);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_vp9, &k_stereo), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_area_calls, 1);
    EXPECT_EQ(g_fake.video_threshold_calls, 1);

    BackendNdlHdrInfo hdr{};
    hdr.max_content_light_level = 1000;
    hdr.matrix_coeffs = 9;
    EXPECT_EQ(backend_ndl_set_video_hdr_info(ctx.get(), &hdr), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_hdr_calls, 1);
    EXPECT_EQ(g_fake.last_hdr.maxContentLightLevel, 1000u);
    EXPECT_EQ(backend_ndl_reload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_area_calls, 2);
    EXPECT_EQ(g_fake.video_threshold_calls, 2);
    EXPECT_EQ(g_fake.video_hdr_calls, 2);

    int value = 0;
    EXPECT_EQ(backend_ndl_get_video_render_buffer_length(ctx.get(), &value), BACKEND_NDL_OK);
    EXPECT_EQ(value, 3);
    EXPECT_EQ(backend_ndl_flush_video_render_buffer(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.video_flush_calls, 1);
    EXPECT_EQ(backend_ndl_get_audio_available(ctx.get(), &value), BACKEND_NDL_OK);
    EXPECT_EQ(value, g_fake.audio_available);
    EXPECT_EQ(backend_ndl_get_audio_total(ctx.get(), &value), BACKEND_NDL_OK);
    EXPECT_EQ(value, g_fake.audio_total);
    EXPECT_EQ(backend_ndl_set_foreground(ctx.get(), false), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.last_app_state, NDL_DIRECTMEDIA_APP_STATE_BACKGROUND);

    uint32_t preferred = 0;
    EXPECT_EQ(backend_ndl_effect_load(ctx.get(), &k_effect_stereo, &preferred), BACKEND_NDL_OK);
    EXPECT_EQ(preferred, g_fake.effect_preferred);
    EXPECT_STREQ(g_fake.last_effect_pcm.layout, "interleaved");
    uint32_t available = 0;
    EXPECT_EQ(backend_ndl_get_effect_available(ctx.get(), &available), BACKEND_NDL_OK);
    EXPECT_EQ(available, g_fake.effect_available);
    g_fake.effect_available = 8;
    EXPECT_EQ(backend_ndl_effect_feed(ctx.get(), k_data, 16), BACKEND_NDL_OVERFLOW);
    g_fake.effect_available = 4096;
    EXPECT_EQ(backend_ndl_effect_feed(ctx.get(), k_data, 16), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.effect_play_calls, 1);
    EXPECT_EQ(backend_ndl_effect_unload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.effect_unload_calls, 1);
    EXPECT_EQ(backend_ndl_effect_feed(ctx.get(), k_data, 16), BACKEND_NDL_NOT_READY);

    ctx.reset();

    fake_reset();
    api = fake_api_full();
    api.DirectEffectPlay = nullptr;
    ctx.reset(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(backend_ndl_get_capabilities(ctx.get()) & BACKEND_NDL_CAP_EFFECT, 0u);
    EXPECT_EQ(backend_ndl_effect_load(ctx.get(), &k_effect_stereo, &preferred),
              BACKEND_NDL_UNAVAILABLE);
}

TEST(BackendNdl, FeedsAndRollback) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false),
              BACKEND_NDL_NEED_KEYFRAME);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, BACKEND_NDL_PTS_AUTO, true),
              BACKEND_NDL_OK);
    EXPECT_GE(g_fake.last_video_pts, 0);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false), BACKEND_NDL_OK);

    int plays = g_fake.audio_play_calls;
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, BACKEND_NDL_PTS_AUTO),
              BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.audio_play_calls, plays + 1);
    g_fake.audio_available = 8;
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_OVERFLOW);
    g_fake.audio_available = 1 << 20;
    g_fake.fail_audio_play = true;
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_ERROR);
    g_fake.fail_audio_play = false;
    g_fake.fail_video_play = true;
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, true), BACKEND_NDL_ERROR);
    g_fake.fail_video_play = false;

    g_fake.fail_load_remaining = 1;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_ERROR);
    /* The previous combined config was restored; both tracks are live and the
     * video gate is re-armed. */
    EXPECT_GE(g_fake.load_calls, 3);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false),
              BACKEND_NDL_NEED_KEYFRAME);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_OK);

    int load_calls = g_fake.load_calls;
    g_fake.fail_load_remaining = 2;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_ERROR);
    EXPECT_EQ(g_fake.load_calls, load_calls + 2);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, true),
              BACKEND_NDL_NOT_READY);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_NOT_READY);

    EXPECT_EQ(backend_ndl_reload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.load_calls, load_calls + 3);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false),
              BACKEND_NDL_NEED_KEYFRAME);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_OK);

    EXPECT_EQ(backend_ndl_unload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_NOT_READY);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, true),
              BACKEND_NDL_NOT_READY);
}

TEST(BackendNdl, FailedMediaUnloadKeepsPreviousTracksReady) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);
    ASSERT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, true), BACKEND_NDL_OK);

    int load_calls = g_fake.load_calls;
    g_fake.fail_unload = true;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_ERROR);
    EXPECT_EQ(g_fake.load_calls, load_calls);
    EXPECT_EQ(backend_ndl_feed_video(ctx.get(), k_data, 16, 0, false), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_OK);

    g_fake.fail_unload = false;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, nullptr), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.load_calls, load_calls + 1);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), k_data, 16, 0), BACKEND_NDL_NOT_READY);
}

TEST(BackendNdl, FailedEffectUnloadKeepsPreviousEffectReady) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);

    uint32_t preferred = 0;
    ASSERT_EQ(backend_ndl_effect_load(ctx.get(), &k_effect_stereo, &preferred),
              BACKEND_NDL_OK);
    ASSERT_EQ(g_fake.effect_load_calls, 1);

    g_fake.fail_effect_unload = true;
    EXPECT_EQ(backend_ndl_effect_load(ctx.get(), &k_effect_stereo, &preferred),
              BACKEND_NDL_ERROR);
    EXPECT_EQ(g_fake.effect_unload_calls, 1);
    EXPECT_EQ(g_fake.effect_load_calls, 1);
    EXPECT_EQ(backend_ndl_effect_feed(ctx.get(), k_data, 16), BACKEND_NDL_OK);

    g_fake.fail_effect_unload = false;
    EXPECT_EQ(backend_ndl_effect_unload(ctx.get()), BACKEND_NDL_OK);
    EXPECT_EQ(g_fake.effect_unload_calls, 2);
    EXPECT_EQ(backend_ndl_effect_feed(ctx.get(), k_data, 16), BACKEND_NDL_NOT_READY);
}

TEST(BackendNdl, EmergencyReleaseQuitsOnceWhileOpen) {
    EXPECT_FALSE(backend_ndl_emergency_release());

    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(backend_ndl_set_media(ctx.get(), &k_h264, &k_stereo), BACKEND_NDL_OK);
    ASSERT_EQ(g_fake.unload_calls, 0);

    EXPECT_TRUE(backend_ndl_emergency_release());
    EXPECT_EQ(g_fake.unload_calls, 1);
    EXPECT_EQ(g_fake.quit_calls, 1);
    EXPECT_FALSE(backend_ndl_emergency_release());
    EXPECT_EQ(g_fake.quit_calls, 1);

    /* Real emergency callers terminate here; tests still free the context. The
     * orderly close loses the claim and must not touch the facade again. */
    ctx.reset();
    EXPECT_EQ(g_fake.quit_calls, 1);
    EXPECT_EQ(g_fake.unload_calls, 1);
    EXPECT_FALSE(backend_ndl_emergency_release());

    /* A reopen re-arms the release. */
    fake_reset();
    BackendNdlApi api_reopen = fake_api_full();
    ctx.reset(open_test_backend(&api_reopen));
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(backend_ndl_emergency_release());
    EXPECT_EQ(g_fake.quit_calls, 1);
    ctx.reset();
    EXPECT_EQ(g_fake.quit_calls, 1);

    /* An orderly close disarms it. */
    fake_reset();
    BackendNdlApi api_final = fake_api_full();
    ctx.reset(open_test_backend(&api_final));
    ASSERT_NE(ctx, nullptr);
    ctx.reset();
    EXPECT_EQ(g_fake.quit_calls, 1);
    EXPECT_FALSE(backend_ndl_emergency_release());
    EXPECT_EQ(g_fake.quit_calls, 1);
}

TEST(BackendNdl, ValidationAndNames) {
    fake_reset();
    BackendNdlApi api = fake_api_full();
    BackendNdlHandle ctx(open_test_backend(&api));
    ASSERT_NE(ctx, nullptr);
    BackendNdlAudioInfo bad_rate = {
        .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
        .sample_rate_hz = 11025,
        .channels = 2,
    };
    BackendNdlAudioInfo bad_channels = {
        .codec = BACKEND_NDL_AUDIO_PCM_S16LE,
        .sample_rate_hz = 48000,
        .channels = 3,
    };
    BackendNdlVideoInfo bad_video = k_h264;
    bad_video.width = 0;
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &bad_rate), BACKEND_NDL_UNSUPPORTED);
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), nullptr, &bad_channels),
              BACKEND_NDL_UNSUPPORTED);
    EXPECT_EQ(backend_ndl_set_media(ctx.get(), &bad_video, nullptr),
              BACKEND_NDL_INVALID_ARGUMENT);
    EXPECT_EQ(backend_ndl_feed_audio(ctx.get(), nullptr, 1, 0),
              BACKEND_NDL_INVALID_ARGUMENT);
    EXPECT_STREQ(backend_ndl_result_name(BACKEND_NDL_NEED_KEYFRAME), "NEED_KEYFRAME");
    EXPECT_STREQ(backend_ndl_log_level_name(BACKEND_NDL_LOG_WARN), "WARN");
}

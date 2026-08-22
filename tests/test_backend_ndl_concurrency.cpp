/* SPDX-License-Identifier: MIT */
#include "backend_ndl_api.h"

#include <gtest/gtest.h>

#include <atomic>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <thread>

typedef struct FakeConcurrent {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool block_plays;
    bool release_plays;
    bool fail_plays;
    int entered_plays;
    std::atomic<bool> fail_unload;
    std::atomic<int> load_calls;
    std::atomic<int> unload_calls;
    std::atomic<int> quit_calls;
    std::atomic<int> audio_calls;
    std::atomic<int> video_calls;
} FakeConcurrent;

static FakeConcurrent g_fake;
static BackendNdlResult g_stats_hook_reload_result;
static bool g_block_first_stats_writer;
static bool g_release_first_stats_writer;
static int g_stats_writers_entered;

static void reload_after_video_stats_begin(BackendNdl *ctx) {
    g_stats_hook_reload_result = backend_ndl_reload(ctx);
}

static void block_first_video_stats_writer(BackendNdl *ctx) {
    (void)ctx;
    pthread_mutex_lock(&g_fake.lock);
    int writer = ++g_stats_writers_entered;
    pthread_cond_broadcast(&g_fake.cond);
    while (g_block_first_stats_writer && writer == 1 &&
           !g_release_first_stats_writer) {
        pthread_cond_wait(&g_fake.cond, &g_fake.lock);
    }
    pthread_mutex_unlock(&g_fake.lock);
}

static const char *fake_error(void) { return "concurrent fake error"; }
static int fake_window(const char *window) { return window ? 0 : -1; }
static int fake_init(const char *app_id) { return app_id ? 0 : -1; }
static int fake_legacy_init(const char *app_id,
                            BackendNdlLegacyResourceReleasedFn callback) {
    return app_id && callback == nullptr ? 0 : -1;
}
static int fake_quit(void) { g_fake.quit_calls.fetch_add(1); return 0; }
static int fake_load(NDL_DIRECTMEDIA_DATA_INFO_T *info, StateCallbackFP cb) {
    (void)info;
    (void)cb;
    g_fake.load_calls.fetch_add(1);
    return 0;
}
static int fake_legacy_load(BackendNdlLegacyMediaInfo *info,
                            BackendNdlLegacyStateCallbackFn cb) {
    (void)info;
    (void)cb;
    g_fake.load_calls.fetch_add(1);
    return 0;
}
static int fake_unload(void) {
    g_fake.unload_calls.fetch_add(1);
    return g_fake.fail_unload.load() ? -1 : 0;
}
static int fake_available(int *available) { *available = 1 << 20; return 0; }
static int fake_multichannel(int *status) { *status = 3; return 0; }
static int fake_register(MultiChannelPCMCallbackFP cb) { (void)cb; return 0; }

static int fake_play_common(std::atomic<int> *counter) {
    counter->fetch_add(1);
    pthread_mutex_lock(&g_fake.lock);
    if (g_fake.block_plays) {
        g_fake.entered_plays++;
        pthread_cond_broadcast(&g_fake.cond);
        while (!g_fake.release_plays) pthread_cond_wait(&g_fake.cond, &g_fake.lock);
    }
    bool fail = g_fake.fail_plays;
    pthread_mutex_unlock(&g_fake.lock);
    return fail ? -1 : 0;
}
static int fake_audio(void *data, unsigned int size, long long pts) {
    (void)data; (void)size; (void)pts; return fake_play_common(&g_fake.audio_calls);
}
static int fake_video(void *data, unsigned int size, long long pts) {
    (void)data; (void)size; (void)pts; return fake_play_common(&g_fake.video_calls);
}

static BackendNdlApi fake_api(BackendNdlAbiProfile profile) {
    BackendNdlApi api;
    memset(&api, 0, sizeof(api));
    api.DirectMediaGetError = fake_error;
    api.DirectMediaSetWindowId = profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT
                                     ? fake_window
                                     : nullptr;
    api.DirectMediaInitCurrent = profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT
                                     ? fake_init
                                     : nullptr;
    api.DirectMediaInitCallback = profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                                      ? fake_legacy_init
                                      : nullptr;
    api.DirectMediaQuit = fake_quit;
    api.DirectMediaLoadCurrent = profile == BACKEND_NDL_ABI_V2_WINDOW_ID_INIT
                                     ? fake_load
                                     : nullptr;
    api.DirectMediaLoadCallback = profile == BACKEND_NDL_ABI_V2_CALLBACK_INIT
                                      ? fake_legacy_load
                                      : nullptr;
    api.DirectMediaUnload = fake_unload;
    api.DirectAudioPlay = fake_audio;
    api.DirectAudioGetAvailableBufferSize = fake_available;
    api.DirectAudioSupportMultiChannel = fake_multichannel;
    api.DirectAudioRegisterCallback = fake_register;
    api.DirectVideoPlay = fake_video;
    return api;
}

static void fake_reset(void) {
    g_fake.block_plays = false;
    g_fake.release_plays = false;
    g_fake.fail_plays = false;
    g_fake.entered_plays = 0;
    g_block_first_stats_writer = false;
    g_release_first_stats_writer = false;
    g_stats_writers_entered = 0;
    pthread_mutex_init(&g_fake.lock, nullptr);
    pthread_cond_init(&g_fake.cond, nullptr);
    g_fake.load_calls.store(0);
    g_fake.unload_calls.store(0);
    g_fake.quit_calls.store(0);
    g_fake.audio_calls.store(0);
    g_fake.video_calls.store(0);
    g_fake.fail_unload.store(false);
}

static void fake_destroy(void) {
    pthread_cond_destroy(&g_fake.cond);
    pthread_mutex_destroy(&g_fake.lock);
}

class FakeConcurrentGuard {
    public:
    FakeConcurrentGuard() {
        backend_ndl_test_set_after_video_stats_begin(nullptr);
        g_stats_hook_reload_result = BACKEND_NDL_ERROR;
        fake_reset();
    }
    ~FakeConcurrentGuard() {
        backend_ndl_test_set_after_video_stats_begin(nullptr);
        fake_destroy();
    }

    FakeConcurrentGuard(const FakeConcurrentGuard &) = delete;
    FakeConcurrentGuard &operator=(const FakeConcurrentGuard &) = delete;
};

struct BackendNdlCloser {
    void operator()(BackendNdl *ctx) const { backend_ndl_close(ctx); }
};

using BackendNdlHandle = std::unique_ptr<BackendNdl, BackendNdlCloser>;

static BackendNdl *open_backend(bool keyframe_gate, BackendNdlAbiProfile profile) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.concurrent.ndl";
    config.require_keyframe_after_reload = keyframe_gate;
    BackendNdlApi api = fake_api(profile);
    BackendNdl *ctx = backend_ndl_open_with_api(&config, profile, &api);
    BackendNdlVideoInfo video = {BACKEND_NDL_VIDEO_H264, 1920, 1080};
    BackendNdlAudioInfo audio = {BACKEND_NDL_AUDIO_PCM_S16LE, 48000, 2};
    if (!ctx || backend_ndl_set_media(ctx, &video, &audio) != BACKEND_NDL_OK) {
        backend_ndl_close(ctx);
        return nullptr;
    }
    return ctx;
}

typedef struct FeedArgs {
    BackendNdl *ctx;
    bool video;
    BackendNdlResult result;
} FeedArgs;

static void feed_thread(FeedArgs *args) {
    uint8_t data[16] = {0};
    args->result = args->video
                       ? backend_ndl_feed_video(args->ctx, data, sizeof(data),
                                                BACKEND_NDL_PTS_AUTO, true)
                       : backend_ndl_feed_audio(args->ctx, data, sizeof(data),
                                                BACKEND_NDL_PTS_AUTO);
}

static void reload_thread(FeedArgs *args) {
    args->result = backend_ndl_reload(args->ctx);
}

static bool wait_for_value(const int *value, int target) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&g_fake.lock);
    while (*value < target) {
        if (pthread_cond_timedwait(&g_fake.cond, &g_fake.lock, &deadline) != 0) {
            pthread_mutex_unlock(&g_fake.lock);
            return false;
        }
    }
    pthread_mutex_unlock(&g_fake.lock);
    return true;
}

static void set_play_behavior(bool block, bool fail) {
    pthread_mutex_lock(&g_fake.lock);
    g_fake.block_plays = block;
    g_fake.fail_plays = fail;
    pthread_mutex_unlock(&g_fake.lock);
}

static void release_plays(void) {
    pthread_mutex_lock(&g_fake.lock);
    g_fake.release_plays = true;
    pthread_cond_broadcast(&g_fake.cond);
    pthread_mutex_unlock(&g_fake.lock);
}

static void release_first_stats_writer(void) {
    pthread_mutex_lock(&g_fake.lock);
    g_release_first_stats_writer = true;
    pthread_cond_broadcast(&g_fake.cond);
    pthread_mutex_unlock(&g_fake.lock);
}

static bool wait_for_reload_after(int unload_before) {
    for (int i = 0; i < 100000 && g_fake.unload_calls.load() == unload_before; i++) {
        sched_yield();
    }
    return g_fake.unload_calls.load() > unload_before;
}

class BackendNdlConcurrencyTest : public ::testing::TestWithParam<BackendNdlAbiProfile> {};

TEST_P(BackendNdlConcurrencyTest, AudioAndVideoFeedsOverlap) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    set_play_behavior(true, false);

    FeedArgs audio = {ctx.get(), false, BACKEND_NDL_ERROR};
    FeedArgs video = {ctx.get(), true, BACKEND_NDL_ERROR};
    std::thread audio_thread(feed_thread, &audio);
    std::thread video_thread(feed_thread, &video);
    bool overlapped = wait_for_value(&g_fake.entered_plays, 2);
    release_plays();
    audio_thread.join();
    video_thread.join();

    EXPECT_TRUE(overlapped);
    EXPECT_EQ(audio.result, BACKEND_NDL_OK);
    EXPECT_EQ(video.result, BACKEND_NDL_OK);
}

TEST_P(BackendNdlConcurrencyTest, ReloadCrossesFeedGeneration) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(true, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    uint8_t data[16] = {0};
    ASSERT_EQ(backend_ndl_feed_video(ctx.get(), data, sizeof(data), 0, true),
              BACKEND_NDL_OK);

    set_play_behavior(true, true);
    FeedArgs feed = {ctx.get(), true, BACKEND_NDL_ERROR};
    FeedArgs reload = {ctx.get(), false, BACKEND_NDL_ERROR};
    std::thread feed_id(feed_thread, &feed);
    bool entered = wait_for_value(&g_fake.entered_plays, 1);
    if (!entered) {
        release_plays();
        feed_id.join();
        FAIL() << "video feed did not enter the fake DirectVideoPlay call";
    }

    int unload_before = g_fake.unload_calls.load();
    std::thread reload_id(reload_thread, &reload);
    bool reload_overlapped = wait_for_reload_after(unload_before);
    release_plays();
    feed_id.join();
    reload_id.join();

    set_play_behavior(false, false);
    BackendNdlResult delta = backend_ndl_feed_video(ctx.get(), data, sizeof(data), 0, false);
    EXPECT_TRUE(reload_overlapped);
    EXPECT_EQ(reload.result, BACKEND_NDL_OK);
    EXPECT_EQ(feed.result, BACKEND_NDL_NEED_KEYFRAME);
    EXPECT_EQ(delta, BACKEND_NDL_NEED_KEYFRAME);
}

TEST_P(BackendNdlConcurrencyTest, ReloadExcludesFeedFromPreviousStatsEpoch) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(true, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    uint8_t data[16] = {0};

    backend_ndl_test_set_after_video_stats_begin(reload_after_video_stats_begin);
    BackendNdlResult crossed =
        backend_ndl_feed_video(ctx.get(), data, sizeof(data), 0, true);
    backend_ndl_test_set_after_video_stats_begin(nullptr);

    EXPECT_EQ(g_stats_hook_reload_result, BACKEND_NDL_OK);
    EXPECT_EQ(crossed, BACKEND_NDL_NEED_KEYFRAME);
    BackendNdlRuntimeStats stats{};
    ASSERT_EQ(backend_ndl_get_runtime_stats(ctx.get(), &stats), BACKEND_NDL_OK);
    EXPECT_EQ(stats.video_play_samples, 0u);
    EXPECT_EQ(stats.video_play_us_ewma, 0u);
    EXPECT_EQ(stats.video_play_us_max, 0u);
    EXPECT_EQ(stats.video_play_errors, 0u);

    ASSERT_EQ(backend_ndl_feed_video(ctx.get(), data, sizeof(data), 0, true),
              BACKEND_NDL_OK);
    ASSERT_EQ(backend_ndl_get_runtime_stats(ctx.get(), &stats), BACKEND_NDL_OK);
    EXPECT_EQ(stats.video_play_samples, 1u);
    EXPECT_EQ(stats.video_play_errors, 0u);
    EXPECT_GE(stats.video_play_us_max, stats.video_play_us_ewma);
}

TEST_P(BackendNdlConcurrencyTest, ConcurrentVideoFeedsBothReachRuntimeStats) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);

    pthread_mutex_lock(&g_fake.lock);
    g_block_first_stats_writer = true;
    pthread_mutex_unlock(&g_fake.lock);
    backend_ndl_test_set_after_video_stats_begin(block_first_video_stats_writer);

    FeedArgs first = {ctx.get(), true, BACKEND_NDL_ERROR};
    FeedArgs second = {ctx.get(), true, BACKEND_NDL_ERROR};
    std::thread first_id(feed_thread, &first);
    bool first_blocked = wait_for_value(&g_stats_writers_entered, 1);
    if (!first_blocked) {
        release_first_stats_writer();
        first_id.join();
        FAIL() << "first video stats writer did not reach the test hook";
    }

    std::thread second_id(feed_thread, &second);
    second_id.join();
    release_first_stats_writer();
    first_id.join();
    backend_ndl_test_set_after_video_stats_begin(nullptr);

    EXPECT_EQ(first.result, BACKEND_NDL_OK);
    EXPECT_EQ(second.result, BACKEND_NDL_OK);
    EXPECT_EQ(g_stats_writers_entered, 2);
    BackendNdlRuntimeStats stats{};
    ASSERT_EQ(backend_ndl_get_runtime_stats(ctx.get(), &stats), BACKEND_NDL_OK);
    EXPECT_EQ(stats.video_play_samples, 2u);
    EXPECT_EQ(stats.video_play_errors, 0u);
    EXPECT_GE(stats.video_play_us_max, stats.video_play_us_ewma);
}

TEST_P(BackendNdlConcurrencyTest, FailedUnloadKeepsCrossedFeedInRuntimeStats) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    set_play_behavior(true, false);

    FeedArgs feed = {ctx.get(), true, BACKEND_NDL_ERROR};
    FeedArgs reload = {ctx.get(), false, BACKEND_NDL_OK};
    std::thread feed_id(feed_thread, &feed);
    bool entered = wait_for_value(&g_fake.entered_plays, 1);
    if (!entered) {
        release_plays();
        feed_id.join();
        FAIL() << "video feed did not enter the fake DirectVideoPlay call";
    }

    g_fake.fail_unload.store(true);
    int unload_before = g_fake.unload_calls.load();
    std::thread reload_id(reload_thread, &reload);
    bool reload_overlapped = wait_for_reload_after(unload_before);
    reload_id.join();
    release_plays();
    feed_id.join();
    g_fake.fail_unload.store(false);

    EXPECT_TRUE(reload_overlapped);
    EXPECT_EQ(reload.result, BACKEND_NDL_ERROR);
    EXPECT_EQ(feed.result, BACKEND_NDL_NEED_KEYFRAME);
    BackendNdlRuntimeStats stats{};
    ASSERT_EQ(backend_ndl_get_runtime_stats(ctx.get(), &stats), BACKEND_NDL_OK);
    EXPECT_EQ(stats.video_play_samples, 1u);
    EXPECT_EQ(stats.video_play_errors, 0u);
}

TEST_P(BackendNdlConcurrencyTest, AudioErrorCrossedByReloadIsTransient) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    set_play_behavior(true, true);

    FeedArgs feed = {ctx.get(), false, BACKEND_NDL_ERROR};
    FeedArgs reload = {ctx.get(), false, BACKEND_NDL_ERROR};
    std::thread feed_id(feed_thread, &feed);
    bool entered = wait_for_value(&g_fake.entered_plays, 1);
    if (!entered) {
        release_plays();
        feed_id.join();
        FAIL() << "audio feed did not enter the fake DirectAudioPlay call";
    }

    int unload_before = g_fake.unload_calls.load();
    std::thread reload_id(reload_thread, &reload);
    bool reload_overlapped = wait_for_reload_after(unload_before);
    release_plays();
    feed_id.join();
    reload_id.join();

    EXPECT_TRUE(reload_overlapped);
    EXPECT_EQ(reload.result, BACKEND_NDL_OK);
    EXPECT_EQ(feed.result, BACKEND_NDL_NOT_READY);
}

typedef struct StressArgs {
    BackendNdl *ctx;
    bool video;
    std::atomic<int> failures;
} StressArgs;

static void stress_feed(StressArgs *args) {
    uint8_t data[16] = {0};
    for (int i = 0; i < 10000; i++) {
        BackendNdlResult result = args->video
                                      ? backend_ndl_feed_video(args->ctx, data, sizeof(data),
                                                               BACKEND_NDL_PTS_AUTO, true)
                                      : backend_ndl_feed_audio(args->ctx, data, sizeof(data),
                                                               BACKEND_NDL_PTS_AUTO);
        if (result != BACKEND_NDL_OK && result != BACKEND_NDL_NOT_READY &&
            result != BACKEND_NDL_NEED_KEYFRAME) {
            args->failures.fetch_add(1);
        }
    }
}

static void stress_reload(StressArgs *args) {
    for (int i = 0; i < 500; i++) {
        if (backend_ndl_reload(args->ctx) != BACKEND_NDL_OK) {
            args->failures.fetch_add(1);
        }
    }
}

TEST_P(BackendNdlConcurrencyTest, FeedAndReloadStress) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, GetParam()));
    ASSERT_NE(ctx.get(), nullptr);
    StressArgs audio = {ctx.get(), false, 0};
    StressArgs video = {ctx.get(), true, 0};
    StressArgs reload = {ctx.get(), false, 0};
    std::thread audio_thread(stress_feed, &audio);
    std::thread video_thread(stress_feed, &video);
    std::thread reload_thread_id(stress_reload, &reload);
    audio_thread.join();
    video_thread.join();
    reload_thread_id.join();

    EXPECT_EQ(audio.failures.load(), 0);
    EXPECT_EQ(video.failures.load(), 0);
    EXPECT_EQ(reload.failures.load(), 0);
}

TEST(BackendNdlEmergencyRace, SingleWinnerAndCloseLosesClaim) {
    FakeConcurrentGuard fake;
    BackendNdlHandle ctx(open_backend(false, BACKEND_NDL_ABI_V2_WINDOW_ID_INIT));
    ASSERT_NE(ctx, nullptr);

    constexpr int kCallers = 8;
    std::atomic<int> winners{0};
    std::atomic<bool> go{false};
    std::thread threads[kCallers];
    for (auto &t : threads) {
        t = std::thread([&] {
            while (!go.load()) sched_yield();
            if (backend_ndl_emergency_release()) winners.fetch_add(1);
        });
    }
    go.store(true);
    for (auto &t : threads) t.join();

    EXPECT_EQ(winners.load(), 1);
    EXPECT_EQ(g_fake.quit_calls.load(), 1);
    int unloads_after_release = g_fake.unload_calls.load();

    /* The orderly close lost the claim: no second Quit, no facade calls. */
    ctx.reset();
    EXPECT_EQ(g_fake.quit_calls.load(), 1);
    EXPECT_EQ(g_fake.unload_calls.load(), unloads_after_release);
}

INSTANTIATE_TEST_SUITE_P(
    AbiProfiles, BackendNdlConcurrencyTest,
    ::testing::Values(BACKEND_NDL_ABI_V2_CALLBACK_INIT,
                      BACKEND_NDL_ABI_V2_WINDOW_ID_INIT));

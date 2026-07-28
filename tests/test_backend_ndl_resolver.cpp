/* SPDX-License-Identifier: MIT */
#include <backend_ndl/backend_ndl.h>

#include <gtest/gtest.h>

#include <stdint.h>

#ifndef BACKEND_NDL_FAKE_FACADE_PATH
#error "BACKEND_NDL_FAKE_FACADE_PATH is required"
#endif
#ifndef BACKEND_NDL_FAKE_LEGACY_FACADE_PATH
#error "BACKEND_NDL_FAKE_LEGACY_FACADE_PATH is required"
#endif
#ifndef BACKEND_NDL_FAKE_INCOMPLETE_FACADE_PATH
#error "BACKEND_NDL_FAKE_INCOMPLETE_FACADE_PATH is required"
#endif

TEST(BackendNdlResolver, LoadsAndUsesAllFacadeGroups) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.resolver.ndl";
    config.window_id = "resolver-window";
    config.library_path = BACKEND_NDL_FAKE_FACADE_PATH;
    config.require_keyframe_after_reload = true;

    BackendNdl *ctx = backend_ndl_open(&config);
    ASSERT_NE(ctx, nullptr);
    constexpr BackendNdlCapabilities expected_capabilities =
        BACKEND_NDL_CAP_APP_STATE | BACKEND_NDL_CAP_WINDOW_ID |
        BACKEND_NDL_CAP_VIDEO_PLAY |
        BACKEND_NDL_CAP_VIDEO_AREA | BACKEND_NDL_CAP_VIDEO_FRAME_DROP |
        BACKEND_NDL_CAP_VIDEO_HDR | BACKEND_NDL_CAP_VIDEO_QUEUE_LENGTH |
        BACKEND_NDL_CAP_VIDEO_FLUSH | BACKEND_NDL_CAP_AUDIO_PLAY |
        BACKEND_NDL_CAP_AUDIO_AVAILABLE | BACKEND_NDL_CAP_AUDIO_TOTAL |
        BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_QUERY |
        BACKEND_NDL_CAP_AUDIO_MULTICHANNEL_CALLBACK | BACKEND_NDL_CAP_EFFECT;
    EXPECT_EQ(backend_ndl_get_capabilities(ctx), expected_capabilities);
    EXPECT_EQ(backend_ndl_get_abi_profile(ctx), BACKEND_NDL_ABI_V2_WINDOW_ID_INIT);

    BackendNdlVideoInfo video = {BACKEND_NDL_VIDEO_H264, 1920, 1080};
    BackendNdlPcmInfo pcm = {48000, 2};
    uint8_t data[16] = {0};
    EXPECT_EQ(backend_ndl_set_media(ctx, &video, &pcm), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_video(ctx, data, sizeof(data), BACKEND_NDL_PTS_AUTO, true),
              BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_audio(ctx, data, sizeof(data), BACKEND_NDL_PTS_AUTO),
              BACKEND_NDL_OK);

    uint32_t preferred = 0;
    EXPECT_EQ(backend_ndl_effect_load(ctx, &pcm, &preferred), BACKEND_NDL_OK);
    EXPECT_EQ(preferred, 256u);
    EXPECT_EQ(backend_ndl_effect_feed(ctx, data, sizeof(data)), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_effect_unload(ctx), BACKEND_NDL_OK);
    backend_ndl_close(ctx);
}

TEST(BackendNdlResolver, LoadsLegacyCallbackProfile) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.resolver.legacy";
    config.webos_sdk_version = "5.0.0";
    config.library_path = BACKEND_NDL_FAKE_LEGACY_FACADE_PATH;
    config.require_keyframe_after_reload = true;

    BackendNdl *ctx = backend_ndl_open(&config);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(backend_ndl_get_abi_profile(ctx), BACKEND_NDL_ABI_V2_CALLBACK_INIT);
    BackendNdlCapabilities caps = backend_ndl_get_capabilities(ctx);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_WINDOW_ID, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_DL_LIFECYCLE, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_VIDEO_HDR, 0u);
    EXPECT_EQ(caps & BACKEND_NDL_CAP_EFFECT, 0u);
    EXPECT_NE(caps & BACKEND_NDL_CAP_VIDEO_PLAY, 0u);
    EXPECT_NE(caps & BACKEND_NDL_CAP_AUDIO_PLAY, 0u);

    BackendNdlVideoInfo video = {BACKEND_NDL_VIDEO_H264, 1280, 720};
    BackendNdlPcmInfo pcm = {48000, 2};
    uint8_t data[16] = {0};
    EXPECT_EQ(backend_ndl_set_media(ctx, &video, &pcm), BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_video(ctx, data, sizeof(data), BACKEND_NDL_PTS_AUTO, true),
              BACKEND_NDL_OK);
    EXPECT_EQ(backend_ndl_feed_audio(ctx, data, sizeof(data), BACKEND_NDL_PTS_AUTO),
              BACKEND_NDL_OK);
    backend_ndl_close(ctx);
}

TEST(BackendNdlResolver, RejectsIncompleteProfileBeforeInit) {
    BackendNdlConfig config;
    backend_ndl_config_defaults(&config);
    config.app_id = "test.resolver.incomplete";
    config.library_path = BACKEND_NDL_FAKE_INCOMPLETE_FACADE_PATH;
    EXPECT_EQ(backend_ndl_open(&config), nullptr);
}

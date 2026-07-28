/* SPDX-License-Identifier: MIT */

#include "backend_ndl_window_id_abi.h"

#if !defined(BACKEND_NDL_NDK_CONFORMANCE_HEADER)
#error "BACKEND_NDL_NDK_CONFORMANCE_HEADER is required"
#endif

/* Keep the official declarations in a separate namespace and give their C
 * symbols private test names so they can coexist with the production ABI. */
#define NDL_DirectMediaGetError BackendNdlOfficial_DirectMediaGetError
#define NDL_DirectMediaSetWindowId BackendNdlOfficial_DirectMediaSetWindowId
#define NDL_DirectMediaInit BackendNdlOfficial_DirectMediaInit
#define NDL_DirectMediaQuit BackendNdlOfficial_DirectMediaQuit
#define NDL_DirectMediaSetAppState BackendNdlOfficial_DirectMediaSetAppState
#define NDL_DirectMediaLoad BackendNdlOfficial_DirectMediaLoad
#define NDL_DirectMediaUnload BackendNdlOfficial_DirectMediaUnload
#define NDL_DirectAudioPlay BackendNdlOfficial_DirectAudioPlay
#define NDL_DirectAudioGetTotalBufferSize BackendNdlOfficial_DirectAudioGetTotalBufferSize
#define NDL_DirectAudioGetAvailableBufferSize \
    BackendNdlOfficial_DirectAudioGetAvailableBufferSize
#define NDL_DirectAudioSupportMultiChannel BackendNdlOfficial_DirectAudioSupportMultiChannel
#define NDL_DirectAudioRegisterCallback BackendNdlOfficial_DirectAudioRegisterCallback
#define NDL_DirectVideoPlay BackendNdlOfficial_DirectVideoPlay
#define NDL_DirectVideoSetArea BackendNdlOfficial_DirectVideoSetArea
#define NDL_DirectVideoSetHDRInfo BackendNdlOfficial_DirectVideoSetHDRInfo
#define NDL_DirectVideoSetFrameDropThreshold BackendNdlOfficial_DirectVideoSetFrameDropThreshold
#define NDL_DirectVideoGetRenderBufferLength BackendNdlOfficial_DirectVideoGetRenderBufferLength
#define NDL_DirectVideoFlushRenderBuffer BackendNdlOfficial_DirectVideoFlushRenderBuffer
#define NDL_DirectEffectLoad BackendNdlOfficial_DirectEffectLoad
#define NDL_DirectEffectUnload BackendNdlOfficial_DirectEffectUnload
#define NDL_DirectEffectPlay BackendNdlOfficial_DirectEffectPlay
#define NDL_DirectEffectGetAvailableBufferSize \
    BackendNdlOfficial_DirectEffectGetAvailableBufferSize

namespace backend_ndl_official {
#include BACKEND_NDL_NDK_CONFORMANCE_HEADER
}

#undef NDL_DirectMediaGetError
#undef NDL_DirectMediaSetWindowId
#undef NDL_DirectMediaInit
#undef NDL_DirectMediaQuit
#undef NDL_DirectMediaSetAppState
#undef NDL_DirectMediaLoad
#undef NDL_DirectMediaUnload
#undef NDL_DirectAudioPlay
#undef NDL_DirectAudioGetTotalBufferSize
#undef NDL_DirectAudioGetAvailableBufferSize
#undef NDL_DirectAudioSupportMultiChannel
#undef NDL_DirectAudioRegisterCallback
#undef NDL_DirectVideoPlay
#undef NDL_DirectVideoSetArea
#undef NDL_DirectVideoSetHDRInfo
#undef NDL_DirectVideoSetFrameDropThreshold
#undef NDL_DirectVideoGetRenderBufferLength
#undef NDL_DirectVideoFlushRenderBuffer
#undef NDL_DirectEffectLoad
#undef NDL_DirectEffectUnload
#undef NDL_DirectEffectPlay
#undef NDL_DirectEffectGetAvailableBufferSize

#include <gtest/gtest.h>

#include <cstddef>
#include <type_traits>

namespace official = backend_ndl_official;

#define BACKEND_NDL_EXPECT_ENUM(name) \
    static_assert(static_cast<int>(name) == static_cast<int>(official::name))

BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTVIDEO_SRC_TYPE_NONE);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTVIDEO_SRC_TYPE_H264);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTVIDEO_SRC_TYPE_HEVC);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTVIDEO_SRC_TYPE_VP9);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTVIDEO_SRC_TYPE_AV1);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SRC_TYPE_NONE);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SRC_TYPE_PCM);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SRC_TYPE_MP3);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SRC_TYPE_OPUS);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_BYPASS);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_48K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_44_1K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_32K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_24K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_16K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_12K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_8K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_22_05K);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTAUDIO_SAMPLERATE_MAX);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTMEDIA_APP_STATE_FOREGROUND);
BACKEND_NDL_EXPECT_ENUM(NDL_DIRECTMEDIA_APP_STATE_BACKGROUND);

#undef BACKEND_NDL_EXPECT_ENUM

#define BACKEND_NDL_EXPECT_LAYOUT(runtime_type, official_type) \
    static_assert(sizeof(runtime_type) == sizeof(official_type)); \
    static_assert(alignof(runtime_type) == alignof(official_type))

#define BACKEND_NDL_EXPECT_OFFSET(runtime_type, official_type, member) \
    static_assert(offsetof(runtime_type, member) == offsetof(official_type, member))

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTVIDEO_SRC_TYPE, official::NDL_DIRECTVIDEO_SRC_TYPE);
BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTVIDEO_DATA_INFO_T,
                          official::NDL_DIRECTVIDEO_DATA_INFO_T);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTVIDEO_DATA_INFO_T,
                          official::NDL_DIRECTVIDEO_DATA_INFO_T, width);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTVIDEO_DATA_INFO_T,
                          official::NDL_DIRECTVIDEO_DATA_INFO_T, height);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTVIDEO_DATA_INFO_T,
                          official::NDL_DIRECTVIDEO_DATA_INFO_T, source);

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTAUDIO_SRC_TYPE, official::NDL_DIRECTAUDIO_SRC_TYPE);
BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTAUDIO_SAMPLERATE_T,
                          official::NDL_DIRECTAUDIO_SAMPLERATE_T);
BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTAUDIO_PCM_INFO_T,
                          official::NDL_DIRECTAUDIO_PCM_INFO_T);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_PCM_INFO_T,
                          official::NDL_DIRECTAUDIO_PCM_INFO_T, format);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_PCM_INFO_T,
                          official::NDL_DIRECTAUDIO_PCM_INFO_T, layout);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_PCM_INFO_T,
                          official::NDL_DIRECTAUDIO_PCM_INFO_T, channelMode);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_PCM_INFO_T,
                          official::NDL_DIRECTAUDIO_PCM_INFO_T, sampleRate);

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTAUDIO_OPUS_INFO_T,
                          official::NDL_DIRECTAUDIO_OPUS_INFO_T);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_OPUS_INFO_T,
                          official::NDL_DIRECTAUDIO_OPUS_INFO_T, channels);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_OPUS_INFO_T,
                          official::NDL_DIRECTAUDIO_OPUS_INFO_T, sampleRate);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_OPUS_INFO_T,
                          official::NDL_DIRECTAUDIO_OPUS_INFO_T, streamHeader);

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTAUDIO_DATA_INFO_T,
                          official::NDL_DIRECTAUDIO_DATA_INFO_T);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_DATA_INFO_T,
                          official::NDL_DIRECTAUDIO_DATA_INFO_T, audioCodec);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTAUDIO_DATA_INFO_T,
                          official::NDL_DIRECTAUDIO_DATA_INFO_T, audio_data);
BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTMEDIA_DATA_INFO_T,
                          official::NDL_DIRECTMEDIA_DATA_INFO_T);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTMEDIA_DATA_INFO_T,
                          official::NDL_DIRECTMEDIA_DATA_INFO_T, videoDataInfo);
BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTMEDIA_DATA_INFO_T,
                          official::NDL_DIRECTMEDIA_DATA_INFO_T, audioDataInfo);

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTVIDEO_HDR_INFO_T,
                          official::NDL_DIRECTVIDEO_HDR_INFO_T);
#define BACKEND_NDL_EXPECT_HDR_OFFSET(member) \
    BACKEND_NDL_EXPECT_OFFSET(NDL_DIRECTVIDEO_HDR_INFO_T, \
                              official::NDL_DIRECTVIDEO_HDR_INFO_T, member)
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesGx);
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesGy);
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesBx);
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesBy);
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesRx);
BACKEND_NDL_EXPECT_HDR_OFFSET(displayPrimariesRy);
BACKEND_NDL_EXPECT_HDR_OFFSET(whitePointX);
BACKEND_NDL_EXPECT_HDR_OFFSET(whitePointY);
BACKEND_NDL_EXPECT_HDR_OFFSET(maxDisplayMasteringLuminance);
BACKEND_NDL_EXPECT_HDR_OFFSET(minDisplayMasteringLuminance);
BACKEND_NDL_EXPECT_HDR_OFFSET(maxContentLightLevel);
BACKEND_NDL_EXPECT_HDR_OFFSET(maxFrameAverageLightLevel);
BACKEND_NDL_EXPECT_HDR_OFFSET(transferCharacteristics);
BACKEND_NDL_EXPECT_HDR_OFFSET(colorPrimaries);
BACKEND_NDL_EXPECT_HDR_OFFSET(matrixCoeffs);
#undef BACKEND_NDL_EXPECT_HDR_OFFSET

BACKEND_NDL_EXPECT_LAYOUT(NDL_DIRECTMEDIA_APP_STATE,
                          official::NDL_DIRECTMEDIA_APP_STATE);

#undef BACKEND_NDL_EXPECT_OFFSET
#undef BACKEND_NDL_EXPECT_LAYOUT

static_assert(std::is_same_v<StateCallbackFP, official::StateCallbackFP>);
static_assert(
    std::is_same_v<MultiChannelPCMCallbackFP, official::MultiChannelPCMCallbackFP>);

#define BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(name) \
    static_assert(std::is_same_v<decltype(&NDL_##name), \
                                 decltype(&official::BackendNdlOfficial_##name)>)

BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectMediaGetError);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectMediaSetWindowId);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectMediaInit);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectMediaQuit);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectMediaUnload);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectAudioPlay);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectAudioGetTotalBufferSize);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectAudioGetAvailableBufferSize);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectAudioSupportMultiChannel);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectVideoPlay);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectVideoSetArea);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectVideoSetFrameDropThreshold);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectVideoGetRenderBufferLength);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectVideoFlushRenderBuffer);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectEffectUnload);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectEffectPlay);
BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE(DirectEffectGetAvailableBufferSize);

#undef BACKEND_NDL_EXPECT_PRIMITIVE_SIGNATURE

static_assert(std::is_same_v<decltype(&NDL_DirectMediaSetAppState),
                             int (*)(NDL_DIRECTMEDIA_APP_STATE)>);
static_assert(std::is_same_v<decltype(&official::BackendNdlOfficial_DirectMediaSetAppState),
                             int (*)(official::NDL_DIRECTMEDIA_APP_STATE)>);
static_assert(std::is_same_v<decltype(&NDL_DirectMediaLoad),
                             int (*)(NDL_DIRECTMEDIA_DATA_INFO_T *, StateCallbackFP)>);
static_assert(std::is_same_v<decltype(&official::BackendNdlOfficial_DirectMediaLoad),
                             int (*)(official::NDL_DIRECTMEDIA_DATA_INFO_T *,
                                     official::StateCallbackFP)>);
static_assert(std::is_same_v<decltype(&NDL_DirectAudioRegisterCallback),
                             int (*)(MultiChannelPCMCallbackFP)>);
static_assert(
    std::is_same_v<decltype(&official::BackendNdlOfficial_DirectAudioRegisterCallback),
                   int (*)(official::MultiChannelPCMCallbackFP)>);
static_assert(std::is_same_v<decltype(&NDL_DirectVideoSetHDRInfo),
                             int (*)(NDL_DIRECTVIDEO_HDR_INFO_T)>);
static_assert(std::is_same_v<decltype(&official::BackendNdlOfficial_DirectVideoSetHDRInfo),
                             int (*)(official::NDL_DIRECTVIDEO_HDR_INFO_T)>);
static_assert(std::is_same_v<decltype(&NDL_DirectEffectLoad),
                             int (*)(NDL_DIRECTAUDIO_PCM_INFO_T *, unsigned int *)>);
static_assert(std::is_same_v<decltype(&official::BackendNdlOfficial_DirectEffectLoad),
                             int (*)(official::NDL_DIRECTAUDIO_PCM_INFO_T *,
                                     unsigned int *)>);

TEST(BackendNdlNdkConformance, WindowIdAbiMatchesOfficialHeader) {
    SUCCEED();
}

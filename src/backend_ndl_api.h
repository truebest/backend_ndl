/* SPDX-License-Identifier: MIT */
#ifndef BACKEND_NDL_API_H
#define BACKEND_NDL_API_H

#include <backend_ndl/backend_ndl.h>

#if defined(BACKEND_NDL_ENABLED) && BACKEND_NDL_ENABLED
#include "backend_ndl_window_id_abi.h"
#include "backend_ndl_legacy_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The private window-id ABI declarations define the 22 DirectMedia
 * function-pointer types. Three loader lifecycle helpers exported by the
 * facade remain described here. Tests inject this table through
 * backend_ndl_open_with_api(); it is not installed. */
#ifdef __cplusplus
#define BACKEND_NDL_API_FUNCTION(name) decltype(&NDL_##name)
#else
#define BACKEND_NDL_API_FUNCTION(name) __typeof__(&NDL_##name)
#endif

typedef struct BackendNdlApi {
    BACKEND_NDL_API_FUNCTION(DirectMediaGetError) DirectMediaGetError;
    BACKEND_NDL_API_FUNCTION(DirectMediaSetWindowId) DirectMediaSetWindowId;
    BACKEND_NDL_API_FUNCTION(DirectMediaInit) DirectMediaInitCurrent;
    BackendNdlLegacyInitFn DirectMediaInitLegacy;
    BACKEND_NDL_API_FUNCTION(DirectMediaQuit) DirectMediaQuit;
    BACKEND_NDL_API_FUNCTION(DirectMediaSetAppState) DirectMediaSetAppState;
    BACKEND_NDL_API_FUNCTION(DirectMediaLoad) DirectMediaLoadCurrent;
    BackendNdlLegacyLoadFn DirectMediaLoadLegacy;
    BACKEND_NDL_API_FUNCTION(DirectMediaUnload) DirectMediaUnload;

    BACKEND_NDL_API_FUNCTION(DirectAudioPlay) DirectAudioPlay;
    BACKEND_NDL_API_FUNCTION(DirectAudioGetTotalBufferSize) DirectAudioGetTotalBufferSize;
    BACKEND_NDL_API_FUNCTION(DirectAudioGetAvailableBufferSize) DirectAudioGetAvailableBufferSize;
    BACKEND_NDL_API_FUNCTION(DirectAudioSupportMultiChannel) DirectAudioSupportMultiChannel;
    BACKEND_NDL_API_FUNCTION(DirectAudioRegisterCallback) DirectAudioRegisterCallback;

    BACKEND_NDL_API_FUNCTION(DirectVideoPlay) DirectVideoPlay;
    BACKEND_NDL_API_FUNCTION(DirectVideoSetArea) DirectVideoSetArea;
    BACKEND_NDL_API_FUNCTION(DirectVideoSetHDRInfo) DirectVideoSetHDRInfo;
    BACKEND_NDL_API_FUNCTION(DirectVideoSetFrameDropThreshold) DirectVideoSetFrameDropThreshold;
    BACKEND_NDL_API_FUNCTION(DirectVideoGetRenderBufferLength) DirectVideoGetRenderBufferLength;
    BACKEND_NDL_API_FUNCTION(DirectVideoFlushRenderBuffer) DirectVideoFlushRenderBuffer;

    BACKEND_NDL_API_FUNCTION(DirectEffectLoad) DirectEffectLoad;
    BACKEND_NDL_API_FUNCTION(DirectEffectUnload) DirectEffectUnload;
    BACKEND_NDL_API_FUNCTION(DirectEffectPlay) DirectEffectPlay;
    BACKEND_NDL_API_FUNCTION(DirectEffectGetAvailableBufferSize) DirectEffectGetAvailableBufferSize;

    bool (*DirectMedia_DL_Initialize)(void);
    void (*DirectMedia_DL_Finalize)(void);
    bool (*DirectMedia_DL_IsInitialized)(void);
} BackendNdlApi;

#undef BACKEND_NDL_API_FUNCTION

#if defined(BACKEND_NDL_TESTING) && BACKEND_NDL_TESTING
BackendNdl *backend_ndl_open_with_api(const BackendNdlConfig *config,
                                      BackendNdlAbiProfile profile,
                                      const BackendNdlApi *api);
#endif

#ifdef __cplusplus
}
#endif
#endif

#endif

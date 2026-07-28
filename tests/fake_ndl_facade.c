/* SPDX-License-Identifier: MIT */
#include "backend_ndl_window_id_abi.h"

#include <stdbool.h>

static bool g_dl_initialized;
static bool g_loaded;
static bool g_effect_loaded;

const char *NDL_DirectMediaGetError(void) { return "fake facade error"; }
int NDL_DirectMediaSetWindowId(const char *window_id) { return window_id ? 0 : -1; }
int NDL_DirectMediaInit(const char *app_id) { return app_id && app_id[0] ? 0 : -1; }
int NDL_DirectMediaQuit(void) { return 0; }
int NDL_DirectMediaSetAppState(NDL_DIRECTMEDIA_APP_STATE state) { (void)state; return 0; }
int NDL_DirectMediaLoad(NDL_DIRECTMEDIA_DATA_INFO_T *data, StateCallbackFP cb) {
    if (!data) return -1;
    g_loaded = true;
    if (cb) cb(0x16, 0, "loaded");
    return 0;
}
int NDL_DirectMediaUnload(void) { g_loaded = false; return 0; }
int NDL_DirectAudioPlay(void *buffer, unsigned int size, long long pts) {
    (void)buffer; (void)size; (void)pts; return g_loaded ? 0 : -1;
}
int NDL_DirectAudioGetTotalBufferSize(int *total) {
    if (!total) return -1;
    *total = 8192;
    return 0;
}
int NDL_DirectAudioGetAvailableBufferSize(int *available) {
    if (!available) return -1;
    *available = 4096;
    return 0;
}
int NDL_DirectAudioSupportMultiChannel(int *status) {
    if (!status) return -1;
    *status = 3;
    return 0;
}
int NDL_DirectAudioRegisterCallback(MultiChannelPCMCallbackFP cb) { (void)cb; return 0; }
int NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts) {
    (void)buffer; (void)size; (void)pts; return g_loaded ? 0 : -1;
}
int NDL_DirectVideoSetArea(int left, int top, int width, int height) {
    (void)left; (void)top; return width > 0 && height > 0 ? 0 : -1;
}
int NDL_DirectVideoSetHDRInfo(NDL_DIRECTVIDEO_HDR_INFO_T info) { (void)info; return 0; }
int NDL_DirectVideoSetFrameDropThreshold(int threshold) { return threshold >= 0 ? 0 : -1; }
int NDL_DirectVideoGetRenderBufferLength(int *length) {
    if (!length) return -1;
    *length = 0;
    return 0;
}
int NDL_DirectVideoFlushRenderBuffer(void) { return 0; }
int NDL_DirectEffectLoad(NDL_DIRECTAUDIO_PCM_INFO_T *pcm, unsigned int *preferred_size) {
    if (!pcm || !preferred_size) return -1;
    *preferred_size = 256;
    g_effect_loaded = true;
    return 0;
}
int NDL_DirectEffectUnload(void) { g_effect_loaded = false; return 0; }
int NDL_DirectEffectPlay(void *buffer, unsigned int size) {
    (void)buffer; (void)size; return g_effect_loaded ? 0 : -1;
}
int NDL_DirectEffectGetAvailableBufferSize(unsigned int *available) {
    if (!available) return -1;
    *available = 4096;
    return 0;
}
bool NDL_DirectMedia_DL_Initialize(void) { g_dl_initialized = true; return true; }
void NDL_DirectMedia_DL_Finalize(void) { g_dl_initialized = false; }
bool NDL_DirectMedia_DL_IsInitialized(void) { return g_dl_initialized; }

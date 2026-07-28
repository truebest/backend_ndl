/* SPDX-License-Identifier: MIT */
#include "backend_ndl_legacy_abi.h"

#include <stdbool.h>

static bool g_loaded;

const char *NDL_DirectMediaGetError(void) { return "fake legacy facade error"; }
int NDL_DirectMediaInit(const char *app_id, BackendNdlLegacyResourceReleasedFn callback) {
    return app_id && app_id[0] && callback == NULL ? 0 : -1;
}
int NDL_DirectMediaQuit(void) { return 0; }
int NDL_DirectMediaSetAppState(int state) { return state == 0 || state == 1 ? 0 : -1; }
int NDL_DirectMediaLoad(BackendNdlLegacyMediaInfo *data,
                        BackendNdlLegacyStateCallbackFn callback) {
    if (!data || (data->video.type != 0 && data->video.type != BACKEND_NDL_LEGACY_VIDEO_H264) ||
        (data->audio.type != 0 && data->audio.type != BACKEND_NDL_LEGACY_AUDIO_PCM)) {
        return -1;
    }
    g_loaded = true;
    if (callback) callback(0x16, 0, "legacy-loaded");
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
int NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts) {
    (void)buffer; (void)size; (void)pts; return g_loaded ? 0 : -1;
}
int NDL_DirectVideoSetArea(int left, int top, int width, int height) {
    (void)left; (void)top; return width > 0 && height > 0 ? 0 : -1;
}
int NDL_DirectVideoSetFrameDropThreshold(int threshold) { return threshold >= 0 ? 0 : -1; }
int NDL_DirectVideoGetRenderBufferLength(int *length) {
    if (!length) return -1;
    *length = 0;
    return 0;
}
int NDL_DirectVideoFlushRenderBuffer(void) { return 0; }

bool NDL_DirectMedia_DL_Initialize(void) { return false; }
void NDL_DirectMedia_DL_Finalize(void) { }
bool NDL_DirectMedia_DL_IsInitialized(void) { return false; }

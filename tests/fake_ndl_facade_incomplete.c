/* SPDX-License-Identifier: MIT */

const char *NDL_DirectMediaGetError(void) { return "incomplete facade"; }
int NDL_DirectMediaInit(const char *app_id, void (*callback)(const char *)) {
    (void)app_id;
    (void)callback;
    return 0;
}
int NDL_DirectMediaQuit(void) { return 0; }
int NDL_DirectMediaUnload(void) { return 0; }

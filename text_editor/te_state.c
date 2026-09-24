#include "te_editor.h"

const editor_file_t te_initial_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
editor_file_t te_global_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
clipboard_t te_clipboard = {.last_copied = NULL};
te_background_state_t te_background = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.thread_started = 0,
	.active = 0,
	.result = 0,
	.progress = 0,
	.total = 0,
	.operation = "idle"
};

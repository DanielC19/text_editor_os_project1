#include "te_editor.h"

const editor_file_t te_initial_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
editor_file_t te_global_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
clipboard_t te_clipboard = {.last_copied = NULL};

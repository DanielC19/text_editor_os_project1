#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Linked-list design:
 * - Each `word_node_t` holds a single word (string) and a pointer to the next word.
 * - Each `line_node_t` holds a linked list of `word_node_t` and a pointer to the next line.
 * Lines are 1-based for user commands. Lines and words use dynamic allocation.
 */
typedef struct word_node {
	char *word;
	struct word_node *next;
} word_node_t;

typedef struct line_node {
	word_node_t *words;
	struct line_node *next;
} line_node_t;

typedef struct {
	int fd;              /* open file descriptor, -1 when none */
	char *path;          /* owned, NULL when none */
	off_t size;          /* cached file size */
	line_node_t *lines;  /* head of linked list of lines */
	size_t line_count;   /* number of lines (for quick checks) */
} editor_file_t;

// Clipboard: stores the most recently copied line
typedef struct {
	line_node_t *last_copied;
} clipboard_t;

// Global editor state
static editor_file_t g_file = { .fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0 };
static clipboard_t g_clipboard = { .last_copied = NULL };

/* -- Minimal stub implementations -- */
int cmd_open_text_editor(int argc, char **argv)
{
	/* TODO: implement open() with O_RDWR|O_CREAT and initialize ef
	 * Behavior notes (from your answers):
	 * - integrate with existing shell.h / main.c calling conventions
	 * - open with O_RDWR | O_CREAT, use dynamic allocations for path
	 * - set ef->lines to NULL and ef->line_count = 0 initially
	 */
	errno = ENOSYS;
	return -1;
}

int te_close(editor_file_t *ef)
{
	/* TODO: close fd, free path, and free lines/words structures */
	(void)ef;
	return 0;
}

int te_open(const char *arg)
{
	/* parse and call editor_open_file */
	(void)arg;
	printf("[TODO] open: %s\n", arg ? arg : "(null)");
	return 0;
}

int te_print(const char *arg)
{
	(void)arg;
	printf("[TODO] print: %s\n", arg ? arg : "(all)");
	return 0;
}

int te_append(const char *text)
{
	(void)text;
	printf("[TODO] append: %s\n", text ? text : "(empty)");
	return 0;
}

int te_delete(const char *arg)
{
	(void)arg;
	printf("[TODO] delete line: %s\n", arg ? arg : "(none)");
	return 0;
}

int te_insert(const char *arg1, const char *arg2)
{
	(void)arg1; (void)arg2;
	printf("[TODO] insert at %s: %s\n", arg1 ? arg1 : "?", arg2 ? arg2 : "");
	return 0;
}

int te_search(const char *word)
{
	(void)word;
	printf("[TODO] search: %s\n", word ? word : "");
	return 0;
}

int te_metadata(void)
{
	printf("[TODO] metadata\n");
	return 0;
}

int te_copy(const char *arg)
{
	(void)arg;
	printf("[TODO] copy line: %s\n", arg ? arg : "");
	return 0;
}

int te_paste(const char *arg)
{
	(void)arg;
	printf("[TODO] paste at: %s\n", arg ? arg : "");
	return 0;
}

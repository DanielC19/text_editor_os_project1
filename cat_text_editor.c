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
static editor_file_t global_state = { .fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0 };
static clipboard_t g_clipboard = { .last_copied = NULL };

static int parse_line_editor(const char *line);

int te_close(editor_file_t *ef)
{
	global_state.fd = -1;

	return 0;
}

int te_open(const char *arg)
{
	global_state.fd = open(arg, O_RDWR | O_CREAT, 0644);

	return 0;
}

int te_save(editor_file_t *ef)
{
	(void)ef;
	printf("[TODO] save\n");
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

int parse_line_editor(const char *line)
{
	static char *argv[10];
	char buffer[256];
	int argc = 0;

	strncpy(buffer, line, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = '\0';

	char *token = strtok(buffer, " \t\n");
	while (token != NULL && argc < 10) {
		argv[argc++] = token;
		token = strtok(NULL, " \t\n");
	}

	if (argc == 0) return 0;

	const char *cmd = argv[0];

	if (strcmp(cmd, "q") == 0) {
		te_close(&global_state);
	} else if (strcmp(cmd, "w") == 0) {
		te_save(&global_state);
	} else if (strcmp(cmd, "p") == 0) {
		te_print(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "a") == 0) {
		te_append(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "d") == 0) {
		te_delete(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "i") == 0) {
		te_insert(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
	} else if (strcmp(cmd, "s") == 0) {
		te_search(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "m") == 0) {
		te_metadata();
	} else if (strcmp(cmd, "y") == 0) {
		te_copy(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "x") == 0) {
		te_paste(argc > 1 ? argv[1] : NULL);
	} else {
		printf("Unknown command: %s\n", cmd);
	}

	return argc;
}

int cmd_open_text_editor(int argc, char **argv)
{
	char line[256];

	if (argc < 2) {
		printf("Error: Se requiere un nombre de archivo para abrir.\n");
		return -1;
	}
	const char *filename = argv[1];
	te_open(filename);

	while (global_state.fd != -1) {
        /* Imprimir prompt cian interactivo */
        printf("\033[1;36m editor> " COLOR_RESET);
        fflush(stdout); /* Asegurar que se muestre en pantalla antes de bloquear en fgets */

        /* Leer línea de entrada. Retorna NULL en EOF (Ctrl+D) */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Tokenizar línea leída */
        int argc = parse_line_editor(line);
        if (argc == 0) {
            continue; /* Ignorar comandos vacíos */
        }
	}

    return 0;
}

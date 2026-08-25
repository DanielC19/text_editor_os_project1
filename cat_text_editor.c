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
static void line_free(line_node_t *line);
static line_node_t *line_create_from_text(const char *text);

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

static void print_line_words(const line_node_t *line)
{
	const word_node_t *w = line->words;
	int first = 1;

	while (w != NULL) {
		if (!first)
			printf(" ");
		printf("%s", w->word ? w->word : "");
		first = 0;
		w = w->next;
	}
	printf("\n");
}

/* Print line n (1-based). Without arg, walk the whole line list. */
int te_print(const char *arg)
{
	line_node_t *cur = global_state.lines;
	size_t idx = 1;

	if (arg == NULL || *arg == '\0') {
		while (cur != NULL) {
			printf("%zu\t", idx);
			print_line_words(cur);
			cur = cur->next;
			idx++;
		}
		return 0;
	}

	char *end = NULL;
	long n = strtol(arg, &end, 10);
	if (end == arg || *end != '\0' || n < 1) {
		printf("Error: número de línea inválido: %s\n", arg);
		return -1;
	}
	if (global_state.line_count == 0 || (size_t)n > global_state.line_count) {
		printf("Error: línea %ld fuera de rango (máx. %zu)\n",
		       n, global_state.line_count);
		return -1;
	}

	while (cur != NULL && idx < (size_t)n) {
		cur = cur->next;
		idx++;
	}
	if (cur == NULL) {
		printf("Error: línea %ld no encontrada\n", n);
		return -1;
	}
	printf("%ld\t", n);
	print_line_words(cur);
	return 0;
}

/* Append text as a new last line. Empty buffer -> that line becomes the first. */
int te_append(const char *text)
{
	line_node_t *new_line = line_create_from_text(text);

	if (!new_line) {
		printf("Error: no hay memoria para agregar la línea\n");
		return -1;
	}

	if (global_state.lines == NULL) {
		global_state.lines = new_line;
	} else {
		line_node_t *tail = global_state.lines;

		while (tail->next != NULL)
			tail = tail->next;
		tail->next = new_line;
	}
	global_state.line_count++;
	return 0;
}

/* Delete line n (1-based). Subsequent lines shift up. Disk write is still te_save's job. */
int te_delete(const char *arg)
{
	char *end = NULL;
	long n;
	line_node_t *victim;
	size_t i;

	if (arg == NULL || *arg == '\0') {
		printf("Error: uso: d <n>\n");
		return -1;
	}

	n = strtol(arg, &end, 10);
	if (end == arg || *end != '\0' || n < 1) {
		printf("Error: número de línea inválido: %s\n", arg);
		return -1;
	}
	if (global_state.line_count == 0 || (size_t)n > global_state.line_count) {
		printf("Error: línea %ld fuera de rango (máx. %zu)\n",
		       n, global_state.line_count);
		return -1;
	}

	if (n == 1) {
		victim = global_state.lines;
		global_state.lines = victim->next;
	} else {
		line_node_t *prev = global_state.lines;

		for (i = 1; i < (size_t)n - 1; i++)
			prev = prev->next;
		victim = prev->next;
		prev->next = victim->next;
	}

	victim->next = NULL;
	line_free(victim);
	global_state.line_count--;
	return 0;
}

static word_node_t *word_create(const char *s)
{
	word_node_t *w = malloc(sizeof(*w));
	if (!w) return NULL;

	size_t len = strlen(s);
	w->word = malloc(len + 1);
	if (!w->word) {
		free(w);
		return NULL;
	}
	memcpy(w->word, s, len + 1);
	w->next = NULL;
	return w;
}

static void line_free(line_node_t *line)
{
	if (!line) return;
	word_node_t *w = line->words;
	while (w) {
		word_node_t *next = w->next;
		free(w->word);
		free(w);
		w = next;
	}
	free(line);
}

/* Build a line whose words are the tokens of `text` (spaces/tabs). Empty text -> empty line. */
static line_node_t *line_create_from_text(const char *text)
{
	line_node_t *line = malloc(sizeof(*line));
	if (!line) return NULL;
	line->words = NULL;
	line->next = NULL;

	if (!text || *text == '\0')
		return line;

	char *copy = malloc(strlen(text) + 1);
	if (!copy) {
		free(line);
		return NULL;
	}
	memcpy(copy, text, strlen(text) + 1);

	word_node_t *tail = NULL;
	char *tok = strtok(copy, " \t");
	while (tok != NULL) {
		word_node_t *w = word_create(tok);
		if (!w) {
			free(copy);
			line_free(line);
			return NULL;
		}
		if (line->words == NULL)
			line->words = w;
		else
			tail->next = w;
		tail = w;
		tok = strtok(NULL, " \t");
	}
	free(copy);
	return line;
}



/* Insert `text` as a new line at 1-based index n (existing lines n..end shift down).
 * Valid n is 1 .. line_count+1 (the latter appends). Disk write is still te_save's job. */
int te_insert(const char *arg1, const char *arg2)
{

	char *end = NULL;
	long n;
	line_node_t *new_line;

	if (arg1 == NULL || *arg1 == '\0') {
		printf("Error: uso: i <n> [texto]\n");
		return -1;
	}

	n = strtol(arg1, &end, 10);
	if (end == arg1 || *end != '\0' || n < 1) {
		printf("Error: número de línea inválido: %s\n", arg1);
		return -1;
	}

	if ((size_t)n > global_state.line_count + 1) {
		printf("Error: línea %ld fuera de rango (máx. %zu)\n",
		       n, global_state.line_count + 1);
		return -1;
	}

	new_line = line_create_from_text(arg2);
	if (!new_line) {
		printf("Error: no hay memoria para insertar la línea\n");
		return -1;
	}

	if (n == 1) {
		new_line->next = global_state.lines;
		global_state.lines = new_line;
	} else {
		line_node_t *prev = global_state.lines;
		long i;

		for (i = 1; i < n - 1; i++)
			prev = prev->next;
		new_line->next = prev->next;
		prev->next = new_line;
	}

	global_state.line_count++;
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
	} else if (strcmp(cmd, "p") == 0) { //Print line n or all lines
		te_print(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "a") == 0) { /* Append as last line: a [texto...] */
		char text[256];
		text[0] = '\0';
		if (argc > 1) {
			size_t used = 0;
			int i;
			for (i = 1; i < argc; i++) {
				size_t tok_len = strlen(argv[i]);
				if (i > 1) {
					if (used + 1 >= sizeof(text)) break;
					text[used++] = ' ';
					text[used] = '\0';
				}
				if (used + tok_len >= sizeof(text)) break;
				memcpy(text + used, argv[i], tok_len + 1);
				used += tok_len;
			}
		}
		te_append(argc > 1 ? text : NULL);
	} else if (strcmp(cmd, "d") == 0) {//Delete line n
		te_delete(argc > 1 ? argv[1] : NULL);
	} else if (strcmp(cmd, "i") == 0) { /* Insert at line n: i <n> [texto...] */
		char text[256];
		text[0] = '\0';
		if (argc > 2) {
			size_t used = 0;
			int i;
			for (i = 2; i < argc; i++) {
				size_t tok_len = strlen(argv[i]);
				if (i > 2) {
					if (used + 1 >= sizeof(text)) break;
					text[used++] = ' ';
					text[used] = '\0';
				}
				if (used + tok_len >= sizeof(text)) break;
				memcpy(text + used, argv[i], tok_len + 1);
				used += tok_len;
			}
		}
		te_insert(argc > 1 ? argv[1] : NULL, argc > 2 ? text : NULL);
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

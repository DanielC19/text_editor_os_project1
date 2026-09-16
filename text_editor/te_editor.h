#ifndef TE_EDITOR_H
#define TE_EDITOR_H

#include "../shell.h"
#include <stddef.h>
#include <sys/types.h>

typedef struct word_node
{
    char *word;
    struct word_node *next;
} word_node_t;

typedef struct line_node
{
    word_node_t *words;
    struct line_node *next;
} line_node_t;

typedef struct
{
    int fd;
    char *path;
    off_t size;
    line_node_t *lines;
    size_t line_count;
} editor_file_t;

typedef struct
{
    line_node_t *last_copied;
} clipboard_t;

extern const editor_file_t te_initial_state;
extern editor_file_t te_global_state;
extern clipboard_t te_clipboard;

int te_is_stored_space_char(char c);
word_node_t *te_word_create(const char *s);
void te_free_word_list(word_node_t *word);
void te_line_free(line_node_t *line);
void te_free_line_list(line_node_t *line);
line_node_t *te_line_create_from_text(const char *text);
int te_append_word_to_line(line_node_t *line, const char *word_buf, size_t len);
int te_append_line_to_document(editor_file_t *ef, line_node_t *line);
line_node_t *te_clone_line(const line_node_t *src);

int te_open(const char *arg);
int te_close(void);
int te_save(void);
int te_load_file_into_nodes(editor_file_t *ef);

int te_print(const char *arg);
int te_append(const char *text);
int te_delete(const char *arg);
int te_insert(const char *arg1, const char *arg2);
int te_search(const char *word);
int te_metadata(void);
int te_copy(const char *arg);
int te_paste(const char *arg);

int te_parse_line_editor(const char *line);

int cmd_open_text_editor(int argc, char **argv);
int cmd_help_text_editor(int argc, char **argv);

#endif /* TE_EDITOR_H */

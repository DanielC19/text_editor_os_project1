#include "te_editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void te_print_line_words(const line_node_t *line)
{
    for (const word_node_t *w = line->words; w != NULL; w = w->next)
        printf("%s", w->word ? w->word : "");
    printf("\n");
}

int te_print(const char *arg)
{
    line_node_t *cur = te_global_state.lines;
    size_t idx = 1;

    if (arg == NULL || *arg == '\0')
    {
        while (cur != NULL)
        {
            printf(COLOR_RESULT "%zu" COLOR_RESET "\t", idx);
            te_print_line_words(cur);
            cur = cur->next;
            idx++;
        }
        return 0;
    }

    char *end = NULL;
    long n = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || n < 1)
    {
        printf("Error: invalid line number: %s\n", arg);
        return -1;
    }
    if (te_global_state.line_count == 0 || (size_t)n > te_global_state.line_count)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, te_global_state.line_count);
        return -1;
    }

    while (cur != NULL && idx < (size_t)n)
    {
        cur = cur->next;
        idx++;
    }

    if (cur == NULL)
    {
        printf("Error: line %ld not found\n", n);
        return -1;
    }

    printf(COLOR_RESULT "%ld" COLOR_RESET "\t", n);
    te_print_line_words(cur);
    return 0;
}

int te_append(const char *text)
{
    line_node_t *new_line = te_line_create_from_text(text);
    if (!new_line)
    {
        printf("Error: out of memory to append line\n");
        return -1;
    }

    te_append_line_to_document(&te_global_state, new_line);
    return 0;
}

int te_delete(const char *arg)
{
    char *end = NULL;
    long n;
    line_node_t *victim;

    if (arg == NULL || *arg == '\0')
    {
        printf("Error: usage: d <n>\n");
        return -1;
    }

    n = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || n < 1)
    {
        printf("Error: invalid line number: %s\n", arg);
        return -1;
    }
    if (te_global_state.line_count == 0 || (size_t)n > te_global_state.line_count)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, te_global_state.line_count);
        return -1;
    }

    if (n == 1)
    {
        victim = te_global_state.lines;
        te_global_state.lines = victim->next;
    }
    else
    {
        line_node_t *prev = te_global_state.lines;
        for (size_t i = 1; i < (size_t)n - 1; i++)
            prev = prev->next;
        victim = prev->next;
        prev->next = victim->next;
    }

    victim->next = NULL;
    te_line_free(victim);
    te_global_state.line_count--;
    return 0;
}

int te_insert(const char *arg1, const char *arg2)
{
    char *end = NULL;
    long n;
    line_node_t *new_line;

    if (arg1 == NULL || *arg1 == '\0')
    {
        printf("Error: usage: i <n> [text]\n");
        return -1;
    }

    n = strtol(arg1, &end, 10);
    if (end == arg1 || *end != '\0' || n < 1)
    {
        printf("Error: invalid line number: %s\n", arg1);
        return -1;
    }

    if ((size_t)n > te_global_state.line_count + 1)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, te_global_state.line_count + 1);
        return -1;
    }

    new_line = te_line_create_from_text(arg2);
    if (!new_line)
    {
        printf("Error: out of memory to insert line\n");
        return -1;
    }

    if (n == 1)
    {
        new_line->next = te_global_state.lines;
        te_global_state.lines = new_line;
    }
    else
    {
        line_node_t *prev = te_global_state.lines;
        for (long i = 1; i < n - 1; i++)
            prev = prev->next;
        new_line->next = prev->next;
        prev->next = new_line;
    }

    te_global_state.line_count++;
    return 0;
}

int te_search(const char *word)
{
    line_node_t *cur;
    size_t line_no = 1;
    int found_any = 0;

    if (word == NULL || *word == '\0')
    {
        printf("Error: usage: s <palabra>\n");
        return -1;
    }

    cur = te_global_state.lines;
    while (cur != NULL)
    {
        word_node_t *w = cur->words;
        int count_in_line = 0;

        while (w != NULL)
        {
            if (w->word != NULL && strcmp(w->word, word) == 0)
                count_in_line++;
            w = w->next;
        }

        if (count_in_line > 0)
        {
            printf("Found '%s' in line %zu (%d time%s)\n",
                   word,
                   line_no,
                   count_in_line,
                   count_in_line == 1 ? "" : "s");
            found_any = 1;
        }

        cur = cur->next;
        line_no++;
    }

    if (!found_any)
    {
        printf("Word '%s' not found.\n", word);
        return 1;
    }

    return 0;
}

int te_metadata(void)
{
    struct stat st;
    size_t mem_chars = 0;
    size_t mem_lines = 0;
    int has_unsaved_changes = 0;

    if (te_global_state.fd == -1 || te_global_state.path == NULL)
    {
        printf("Error: no file open. Use 'editor <archivo>' first.\n");
        return -1;
    }

    if (fstat(te_global_state.fd, &st) == -1)
    {
        perror("Error reading file metadata");
        return -1;
    }

    for (line_node_t *cur = te_global_state.lines; cur != NULL; cur = cur->next)
    {
        mem_lines++;
        for (word_node_t *w = cur->words; w != NULL; w = w->next)
        {
            if (w->word != NULL)
                mem_chars += strlen(w->word);
        }
        mem_chars += 1;
    }

    if (mem_chars != (size_t)st.st_size)
        has_unsaved_changes = 1;

    printf("--- Metadata del archivo ---\n");
    printf("[Disk / saved file]\n");
    printf("  Path: %s\n", te_global_state.path);
    printf("  Size: %ld bytes\n", (long)st.st_size);
    printf("  Permissions (octal): %o\n", st.st_mode & 0777);
    printf("  Inode: %ld\n", (long)st.st_ino);
    printf("  Modified: %s", ctime(&st.st_mtime));

    printf("[Memory / editor state]\n");
    printf("  Lines in memory: %zu\n", mem_lines);
    printf("  Estimated characters in memory: %zu\n", mem_chars);
    printf("  Unsaved changes: %s\n", has_unsaved_changes ? "YES" : "NO");
    printf("----------------------------\n");

    return 0;
}

int te_copy(const char *arg)
{
    char *end = NULL;
    long n;
    line_node_t *cur = te_global_state.lines;
    size_t idx = 1;

    if (arg == NULL || *arg == '\0')
    {
        printf("Error: usage: y <n>\n");
        return -1;
    }

    n = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || n < 1)
    {
        printf("Error: invalid line number: %s\n", arg);
        return -1;
    }

    if (te_global_state.line_count == 0 || (size_t)n > te_global_state.line_count)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, te_global_state.line_count);
        return -1;
    }

    while (cur != NULL && idx < (size_t)n)
    {
        cur = cur->next;
        idx++;
    }

    if (te_clipboard.last_copied != NULL)
    {
        te_free_line_list(te_clipboard.last_copied);
        te_clipboard.last_copied = NULL;
    }

    te_clipboard.last_copied = te_clone_line(cur);
    if (te_clipboard.last_copied == NULL)
    {
        printf("Error: out of memory while copying line\n");
        return -1;
    }

    printf("Line %ld copied to clipboard.\n", n);
    return 0;
}

int te_paste(const char *arg)
{
    char *end = NULL;
    long n;
    line_node_t *new_line;

    if (arg == NULL || *arg == '\0')
    {
        printf("Error: usage: x <n>\n");
        return -1;
    }

    if (te_clipboard.last_copied == NULL)
    {
        printf("Error: clipboard empty. Use y <n> first.\n");
        return -1;
    }

    n = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || n < 1)
    {
        printf("Error: invalid line number: %s\n", arg);
        return -1;
    }

    if ((size_t)n > te_global_state.line_count + 1)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, te_global_state.line_count + 1);
        return -1;
    }

    new_line = te_clone_line(te_clipboard.last_copied);
    if (new_line == NULL)
    {
        printf("Error: out of memory while pasting line\n");
        return -1;
    }

    if (n == 1)
    {
        new_line->next = te_global_state.lines;
        te_global_state.lines = new_line;
    }
    else
    {
        line_node_t *prev = te_global_state.lines;
        for (long i = 1; i < n - 1; i++)
            prev = prev->next;
        new_line->next = prev->next;
        prev->next = new_line;
    }

    te_global_state.line_count++;
    printf("Line pasted at position %ld.\n", n);
    return 0;
}

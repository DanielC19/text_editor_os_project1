#include "te_editor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int te_close(void)
{
    if (te_global_state.fd != -1)
    {
        close(te_global_state.fd);
        if (te_global_state.path)
            free(te_global_state.path);

        te_free_line_list(te_global_state.lines);
        te_global_state = te_initial_state;
    }

    if (te_clipboard.last_copied != NULL)
    {
        te_free_line_list(te_clipboard.last_copied);
        te_clipboard.last_copied = NULL;
    }

    return 0;
}

int te_open(const char *arg)
{
    if (te_global_state.fd != -1)
        te_close();

    te_global_state.fd = open(arg, O_RDWR | O_CREAT, 0644);
    if (te_global_state.fd == -1)
    {
        perror("Error opening file");
        return -1;
    }

    te_global_state.path = strdup(arg);
    if (te_global_state.path == NULL)
    {
        perror("Error reading file path");
        te_close();
        return -1;
    }

    te_global_state.size = lseek(te_global_state.fd, 0, SEEK_END);

    if (te_load_file_into_nodes(&te_global_state) < 0)
    {
        perror("Error loading file into memory");
        te_close();
        return -1;
    }

    return 0;
}

int te_save(void)
{
    if (te_global_state.fd == -1)
    {
        printf("Error: No file currently open to save.\n");
        return -1;
    }

    if (ftruncate(te_global_state.fd, 0) == -1)
    {
        perror("Error truncating file");
        return -1;
    }

    if (lseek(te_global_state.fd, 0, SEEK_SET) == (off_t)-1)
    {
        perror("Error seeking file pointer");
        return -1;
    }

    for (line_node_t *cur_line = te_global_state.lines; cur_line != NULL; cur_line = cur_line->next)
    {
        for (word_node_t *cur_word = cur_line->words; cur_word != NULL; cur_word = cur_word->next)
        {
            if (cur_word->word != NULL)
                write(te_global_state.fd, cur_word->word, strlen(cur_word->word));
        }

        write(te_global_state.fd, "\n", 1);
    }

    te_global_state.size = lseek(te_global_state.fd, 0, SEEK_END);
    printf("File '%s' saved successfully.\n", te_global_state.path);
    return 0;
}

int te_load_file_into_nodes(editor_file_t *ef)
{
    char buffer[4096];
    ssize_t bytes_read;
    line_node_t *current_line = NULL;
    char *word_buf = NULL;
    size_t word_len = 0;
    size_t word_cap = 0;

    if (ef == NULL || ef->fd < 0)
        return -1;

    lseek(ef->fd, 0, SEEK_SET);
    ef->lines = NULL;
    ef->line_count = 0;

    current_line = calloc(1, sizeof(*current_line));
    if (current_line == NULL)
        return -1;

    while ((bytes_read = read(ef->fd, buffer, sizeof(buffer))) > 0)
    {
        for (ssize_t i = 0; i < bytes_read; i++)
        {
            unsigned char c = (unsigned char)buffer[i];
            int c_is_space;
            int run_is_space;

            if (c == '\r' || c == '\v' || c == '\f')
                continue;

            if (c == '\n')
            {
                if (word_len > 0)
                {
                    if (te_append_word_to_line(current_line, word_buf, word_len) < 0)
                    {
                        free(word_buf);
                        te_free_line_list(current_line);
                        return -1;
                    }
                    word_len = 0;
                }

                if (te_append_line_to_document(ef, current_line) < 0)
                {
                    free(word_buf);
                    te_free_line_list(current_line);
                    return -1;
                }

                current_line = calloc(1, sizeof(*current_line));
                if (!current_line)
                {
                    free(word_buf);
                    te_free_line_list(ef->lines);
                    ef->lines = NULL;
                    ef->line_count = 0;
                    return -1;
                }
                continue;
            }

            c_is_space = te_is_stored_space_char((char)c);
            run_is_space = (word_len > 0) && te_is_stored_space_char(word_buf[0]);

            if (word_len > 0 && c_is_space != run_is_space)
            {
                if (te_append_word_to_line(current_line, word_buf, word_len) < 0)
                {
                    free(word_buf);
                    te_free_line_list(current_line);
                    return -1;
                }
                word_len = 0;
            }

            if (word_len + 1 > word_cap)
            {
                size_t new_cap = (word_cap == 0) ? 32 : word_cap * 2;
                char *new_buf = realloc(word_buf, new_cap);
                if (!new_buf)
                {
                    free(word_buf);
                    te_free_line_list(current_line);
                    te_free_line_list(ef->lines);
                    ef->lines = NULL;
                    ef->line_count = 0;
                    return -1;
                }
                word_buf = new_buf;
                word_cap = new_cap;
            }

            word_buf[word_len++] = (char)c;
        }
    }

    if (bytes_read < 0)
    {
        free(word_buf);
        te_free_line_list(current_line);
        te_free_line_list(ef->lines);
        ef->lines = NULL;
        ef->line_count = 0;
        return -1;
    }

    if (word_len > 0)
    {
        if (te_append_word_to_line(current_line, word_buf, word_len) < 0)
        {
            free(word_buf);
            te_free_line_list(current_line);
            te_free_line_list(ef->lines);
            ef->lines = NULL;
            ef->line_count = 0;
            return -1;
        }
    }

    if (current_line->words != NULL)
    {
        if (te_append_line_to_document(ef, current_line) < 0)
        {
            free(word_buf);
            te_free_line_list(current_line);
            te_free_line_list(ef->lines);
            ef->lines = NULL;
            ef->line_count = 0;
            return -1;
        }
    }
    else
    {
        free(current_line);
    }

    free(word_buf);
    ef->size = lseek(ef->fd, 0, SEEK_END);
    lseek(ef->fd, 0, SEEK_SET);
    return 0;
}

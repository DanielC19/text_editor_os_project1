#include "te_editor.h"

#include <stdlib.h>
#include <string.h>

int te_is_stored_space_char(char c)
{
    return c == ' ' || c == '\t';
}

word_node_t *te_word_create(const char *s)
{
    word_node_t *w = malloc(sizeof(*w));
    size_t len;

    if (!w)
        return NULL;

    len = strlen(s);
    w->word = malloc(len + 1);
    if (!w->word)
    {
        free(w);
        return NULL;
    }

    memcpy(w->word, s, len + 1);
    w->next = NULL;
    return w;
}

void te_free_word_list(word_node_t *word)
{
    while (word != NULL)
    {
        word_node_t *next = word->next;
        free(word->word);
        free(word);
        word = next;
    }
}

void te_line_free(line_node_t *line)
{
    if (!line)
        return;

    te_free_word_list(line->words);
    free(line);
}

void te_free_line_list(line_node_t *line)
{
    while (line != NULL)
    {
        line_node_t *next = line->next;
        te_line_free(line);
        line = next;
    }
}

int te_append_word_to_line(line_node_t *line, const char *word_buf, size_t len)
{
    char *temp = malloc(len + 1);
    word_node_t *w;

    if (!temp)
        return -1;

    memcpy(temp, word_buf, len);
    temp[len] = '\0';

    w = te_word_create(temp);
    free(temp);
    if (!w)
        return -1;

    if (!line->words)
    {
        line->words = w;
    }
    else
    {
        word_node_t *tail = line->words;
        while (tail->next)
            tail = tail->next;
        tail->next = w;
    }

    return 0;
}

line_node_t *te_line_create_from_text(const char *text)
{
    line_node_t *line = calloc(1, sizeof(*line));
    size_t i = 0;

    if (!line)
        return NULL;

    if (!text || *text == '\0')
        return line;

    while (text[i] != '\0' && text[i] != '\n')
    {
        size_t start = i;

        if (te_is_stored_space_char(text[i]))
        {
            while (te_is_stored_space_char(text[i]))
                i++;
        }
        else
        {
            while (text[i] != '\0' && text[i] != '\n' && !te_is_stored_space_char(text[i]))
                i++;
        }

        if (te_append_word_to_line(line, text + start, i - start) < 0)
        {
            te_line_free(line);
            return NULL;
        }
    }

    return line;
}

int te_append_line_to_document(editor_file_t *ef, line_node_t *line)
{
    line->next = NULL;

    if (ef->lines == NULL)
    {
        ef->lines = line;
    }
    else
    {
        line_node_t *tail = ef->lines;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = line;
    }

    ef->line_count++;
    return 0;
}

line_node_t *te_clone_line(const line_node_t *src)
{
    line_node_t *copy = calloc(1, sizeof(*copy));
    word_node_t *tail = NULL;

    if (!copy)
        return NULL;

    for (word_node_t *w = src->words; w != NULL; w = w->next)
    {
        word_node_t *new_word = te_word_create(w->word ? w->word : "");
        if (!new_word)
        {
            te_free_word_list(copy->words);
            free(copy);
            return NULL;
        }

        if (copy->words == NULL)
            copy->words = new_word;
        else
            tail->next = new_word;

        tail = new_word;
    }

    return copy;
}

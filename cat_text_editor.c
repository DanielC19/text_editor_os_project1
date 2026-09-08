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

#define COLOR_RESET "\033[0m"

/* ========================================================================= *
 *  DATA STRUCTURES
 * ========================================================================= */

/* Linked-list design:
 * - Each `word_node_t` holds a single word (string) and a pointer to the next word.
 * - Each `line_node_t` holds a linked list of `word_node_t` and a pointer to the next line.
 * Lines are 1-based for user commands. Lines and words use dynamic allocation.
 */
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
    int fd;             /* open file descriptor, -1 when none */
    char *path;         /* owned, NULL when none */
    off_t size;         /* cached file size */
    line_node_t *lines; /* head of linked list of lines */
    size_t line_count;  /* number of lines (for quick checks) */
} editor_file_t;

/* Clipboard: stores the path of the most recently copied file */
typedef struct
{
    char *last_copied_path;
} clipboard_t;

/* ========================================================================= *
 *  GLOBAL STATE
 * ========================================================================= */

static const editor_file_t initial_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
static editor_file_t global_state = {.fd = -1, .path = NULL, .size = 0, .lines = NULL, .line_count = 0};
static clipboard_t g_clipboard = {.last_copied_path = NULL};

/* ========================================================================= *
 *  INTERNAL PROTOTYPES
 * ========================================================================= */

static int parse_line_editor(const char *line);
static int load_file_into_nodes(editor_file_t *ef);
static int append_line_to_document(editor_file_t *ef, line_node_t *line);
static int append_word_to_line(line_node_t *line, const char *word_buf, size_t len);

static word_node_t *word_create(const char *s);
static line_node_t *line_create_from_text(const char *text);
static void line_free(line_node_t *line);
static void free_word_list(word_node_t *word);
static void free_line_list(line_node_t *line);

static int te_open(const char *arg);
static int te_close(void);
static int te_save(void);
static int te_print(const char *arg);
static int te_append(const char *text);
static int te_delete(const char *arg);
static int te_insert(const char *arg1, const char *arg2);
static int te_search(const char *word);
static int te_metadata(void);
static int te_copy(const char *arg);
static int te_paste(const char *arg);
static int te_copy_file_with_permissions(const char *src_path, const char *dst_path);

/* ========================================================================= *
 *  MEMORY AND LIST MANAGEMENT
 * ========================================================================= */

static word_node_t *word_create(const char *s)
{
    word_node_t *w = malloc(sizeof(*w));
    if (!w)
        return NULL;

    size_t len = strlen(s);
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

static void free_word_list(word_node_t *word)
{
    while (word != NULL)
    {
        word_node_t *next = word->next;
        free(word->word);
        free(word);
        word = next;
    }
}

static void line_free(line_node_t *line)
{
    if (!line)
        return;
    free_word_list(line->words);
    free(line);
}

static void free_line_list(line_node_t *line)
{
    while (line != NULL)
    {
        line_node_t *next = line->next;
        line_free(line);
        line = next;
    }
}

/* Build a line whose words are the tokens of `text` (spaces/tabs). Empty text -> empty line. */
static line_node_t *line_create_from_text(const char *text)
{
    line_node_t *line = calloc(1, sizeof(*line));
    if (!line)
        return NULL;

    if (!text || *text == '\0')
        return line;

    char *copy = strdup(text);
    if (!copy)
    {
        free(line);
        return NULL;
    }

    word_node_t *tail = NULL;
    char *tok = strtok(copy, " \t");
    while (tok != NULL)
    {
        word_node_t *w = word_create(tok);
        if (!w)
        {
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

static int append_word_to_line(line_node_t *line, const char *word_buf, size_t len)
{
    char *temp = malloc(len + 1);
    if (!temp)
        return -1;
    memcpy(temp, word_buf, len);
    temp[len] = '\0';

    word_node_t *w = word_create(temp);
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

static int append_line_to_document(editor_file_t *ef, line_node_t *line)
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

/* ========================================================================= *
 *  FILE I/O
 * ========================================================================= */

static int te_close(void)
{
    if (global_state.fd != -1)
    {
        close(global_state.fd);
        if (global_state.path)
        {
            free(global_state.path);
        }
        free_line_list(global_state.lines);
        global_state = initial_state;
    }
    return 0;
}

static int te_copy_file_with_permissions(const char *src_path, const char *dst_path)
{
    char buffer[4096];
    struct stat src_st;
    struct stat dst_st;
    ssize_t bytes_read;
    int src_fd;
    int dst_fd;

    if (src_path == NULL || *src_path == '\0' || dst_path == NULL || *dst_path == '\0')
    {
        printf("Error: invalid source or destination path.\n");
        return -1;
    }

    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1)
    {
        perror("Error opening source file");
        return -1;
    }

    if (fstat(src_fd, &src_st) == -1)
    {
        perror("Error reading source metadata");
        close(src_fd);
        return -1;
    }

    if (stat(dst_path, &dst_st) == 0)
    {
        if (dst_st.st_dev == src_st.st_dev && dst_st.st_ino == src_st.st_ino)
        {
            printf("Info: source and destination are the same file. Ignored.\n");
            close(src_fd);
            return 0;
        }
    }
    else if (errno != ENOENT)
    {
        perror("Error checking destination file");
        close(src_fd);
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode & 07777);
    if (dst_fd == -1)
    {
        perror("Error opening destination file");
        close(src_fd);
        return -1;
    }

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0)
    {
        ssize_t off = 0;
        while (off < bytes_read)
        {
            ssize_t bytes_written = write(dst_fd, buffer + off, (size_t)(bytes_read - off));
            if (bytes_written == -1)
            {
                perror("Error writing destination file");
                close(dst_fd);
                close(src_fd);
                return -1;
            }
            off += bytes_written;
        }
    }

    if (bytes_read == -1)
    {
        perror("Error reading source file");
        close(dst_fd);
        close(src_fd);
        return -1;
    }

    if (fchmod(dst_fd, src_st.st_mode & 07777) == -1)
    {
        perror("Error setting destination permissions");
        close(dst_fd);
        close(src_fd);
        return -1;
    }

    if (close(dst_fd) == -1)
    {
        perror("Error closing destination file");
        close(src_fd);
        return -1;
    }

    if (close(src_fd) == -1)
    {
        perror("Error closing source file");
        return -1;
    }

    return 0;
}

static int te_open(const char *arg)
{
    /* If a file is already open, close it first to avoid leaks */
    if (global_state.fd != -1)
    {
        te_close();
    }

    /* Open the file with read/write permissions. Create it if it doesn't exist. */
    global_state.fd = open(arg, O_RDWR | O_CREAT, 0644);
    if (global_state.fd == -1)
    {
        perror("Error opening file");
        return -1;
    }

    /* Save the file path in the global state */
    global_state.path = strdup(arg);
    if (global_state.path == NULL)
    {
        perror("Error reading file path");
        te_close();
        return -1;
    }

    /* Calculate the initial file size */
    global_state.size = lseek(global_state.fd, 0, SEEK_END);

    /* Parse the file content into the linked list structures */
    if (load_file_into_nodes(&global_state) < 0)
    {
        perror("Error loading file into memory");
        te_close();
        return -1;
    }

    return 0;
}

static int te_save(void)
{
    if (global_state.fd == -1)
    {
        printf("Error: No file currently open to save.\n");
        return -1;
    }

    /* Truncate the file to 0 bytes to overwrite cleanly */
    if (ftruncate(global_state.fd, 0) == -1)
    {
        perror("Error truncating file");
        return -1;
    }

    /* Move the file cursor back to the beginning (byte 0) */
    if (lseek(global_state.fd, 0, SEEK_SET) == (off_t)-1)
    {
        perror("Error seeking file pointer");
        return -1;
    }

    /* Traverse the linked list and write to the file */
    line_node_t *cur_line = global_state.lines;
    while (cur_line != NULL)
    {
        word_node_t *cur_word = cur_line->words;
        int is_first_word = 1;

        while (cur_word != NULL)
        {
            /* Write a space before each word (except the first one in the line) */
            if (!is_first_word)
            {
                write(global_state.fd, " ", 1);
            }

            if (cur_word->word != NULL)
            {
                write(global_state.fd, cur_word->word, strlen(cur_word->word));
            }

            is_first_word = 0;
            cur_word = cur_word->next;
        }

        /* Write a newline at the end of each line */
        write(global_state.fd, "\n", 1);
        cur_line = cur_line->next;
    }

    /* Update the cached size in the global state */
    global_state.size = lseek(global_state.fd, 0, SEEK_END);
    printf("File '%s' saved successfully.\n", global_state.path);

    return 0;
}

static int load_file_into_nodes(editor_file_t *ef)
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

            if (c == '\n' || c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f')
            {
                if (word_len > 0)
                {
                    if (append_word_to_line(current_line, word_buf, word_len) < 0)
                    {
                        free(word_buf);
                        free_line_list(current_line);
                        return -1;
                    }
                    word_len = 0;
                }

                if (c == '\n')
                {
                    if (append_line_to_document(ef, current_line) < 0)
                    {
                        free(word_buf);
                        free_line_list(current_line);
                        return -1;
                    }
                    current_line = calloc(1, sizeof(*current_line));
                    if (!current_line)
                    {
                        free(word_buf);
                        free_line_list(ef->lines);
                        ef->lines = NULL;
                        ef->line_count = 0;
                        return -1;
                    }
                }
            }
            else
            {
                if (word_len + 1 > word_cap)
                {
                    size_t new_cap = (word_cap == 0) ? 32 : word_cap * 2;
                    char *new_buf = realloc(word_buf, new_cap);
                    if (!new_buf)
                    {
                        free(word_buf);
                        free_line_list(current_line);
                        free_line_list(ef->lines);
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
    }

    if (bytes_read < 0)
    {
        free(word_buf);
        free_line_list(current_line);
        free_line_list(ef->lines);
        ef->lines = NULL;
        ef->line_count = 0;
        return -1;
    }

    if (word_len > 0)
    {
        if (append_word_to_line(current_line, word_buf, word_len) < 0)
        {
            free(word_buf);
            free_line_list(current_line);
            free_line_list(ef->lines);
            ef->lines = NULL;
            ef->line_count = 0;
            return -1;
        }
    }

    if (current_line->words != NULL || ef->lines == NULL)
    {
        if (append_line_to_document(ef, current_line) < 0)
        {
            free(word_buf);
            free_line_list(current_line);
            free_line_list(ef->lines);
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

/* ========================================================================= *
 *  EDITOR COMMANDS
 * ========================================================================= */

static void print_line_words(const line_node_t *line)
{
    const word_node_t *w = line->words;
    int first = 1;
    while (w != NULL)
    {
        if (!first)
            printf(" ");
        printf("%s", w->word ? w->word : "");
        first = 0;
        w = w->next;
    }
    printf("\n");
}

/* Print line n (1-based). Without arg, walk the whole line list. */
static int te_print(const char *arg)
{
    line_node_t *cur = global_state.lines;
    size_t idx = 1;

    if (arg == NULL || *arg == '\0')
    {
        while (cur != NULL)
        {
            printf("%zu\t", idx);
            print_line_words(cur);
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
    if (global_state.line_count == 0 || (size_t)n > global_state.line_count)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, global_state.line_count);
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
    printf("%ld\t", n);
    print_line_words(cur);
    return 0;
}

/* Append text as a new last line. Empty buffer -> that line becomes the first. */
static int te_append(const char *text)
{
    line_node_t *new_line = line_create_from_text(text);
    if (!new_line)
    {
        printf("Error: out of memory to append line\n");
        return -1;
    }
    append_line_to_document(&global_state, new_line);
    return 0;
}

/* Delete line n (1-based). Subsequent lines shift up. Disk write is deferred to te_save. */
static int te_delete(const char *arg)
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
    if (global_state.line_count == 0 || (size_t)n > global_state.line_count)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, global_state.line_count);
        return -1;
    }

    if (n == 1)
    {
        victim = global_state.lines;
        global_state.lines = victim->next;
    }
    else
    {
        line_node_t *prev = global_state.lines;
        for (size_t i = 1; i < (size_t)n - 1; i++)
            prev = prev->next;
        victim = prev->next;
        prev->next = victim->next;
    }

    victim->next = NULL;
    line_free(victim);
    global_state.line_count--;
    return 0;
}

/* Insert `text` as a new line at 1-based index n (existing lines n..end shift down).
 * Valid n is 1 .. line_count+1 (the latter appends). Disk write is deferred to te_save. */
static int te_insert(const char *arg1, const char *arg2)
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

    if ((size_t)n > global_state.line_count + 1)
    {
        printf("Error: line %ld out of range (max %zu)\n", n, global_state.line_count + 1);
        return -1;
    }

    new_line = line_create_from_text(arg2);
    if (!new_line)
    {
        printf("Error: out of memory to insert line\n");
        return -1;
    }

    if (n == 1)
    {
        new_line->next = global_state.lines;
        global_state.lines = new_line;
    }
    else
    {
        line_node_t *prev = global_state.lines;
        for (long i = 1; i < n - 1; i++)
            prev = prev->next;
        new_line->next = prev->next;
        prev->next = new_line;
    }

    global_state.line_count++;
    return 0;
}

static int te_search(const char *word)
{
    line_node_t *cur;
    size_t line_no = 1;
    int found_any = 0;

    if (word == NULL || *word == '\0')
    {
        printf("Error: usage: s <palabra>\n");
        return -1;
    }

    cur = global_state.lines;
    while (cur != NULL)
    {
        word_node_t *w = cur->words;
        int count_in_line = 0;

        while (w != NULL)
        {
            if (w->word != NULL && strcmp(w->word, word) == 0)
            {
                count_in_line++;
            }
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

static int te_metadata(void)
{
    struct stat st;

    if (global_state.fd == -1 || global_state.path == NULL)
    {
        printf("Error: no file open. Use 'editor <archivo>' first.\n");
        return -1;
    }

    if (fstat(global_state.fd, &st) == -1)
    {
        perror("Error reading file metadata");
        return -1;
    }

    printf("--- File Metadata ---\n");
    printf("Path: %s\n", global_state.path);
    printf("Size: %ld bytes\n", (long)st.st_size);
    printf("Permissions (octal): %o\n", st.st_mode & 0777);
    printf("Inode: %ld\n", (long)st.st_ino);
    printf("Modified: %s", ctime(&st.st_mtime));
    printf("---------------------\n");
    return 0;
}

static int te_copy(const char *arg)
{
    struct stat st;

    if (arg == NULL || *arg == '\0')
    {
        printf("Error: usage: y <archivo_origen>\n");
        return -1;
    }

    if (stat(arg, &st) == -1)
    {
        perror("Error accessing source file");
        return -1;
    }

    if (g_clipboard.last_copied_path != NULL)
    {
        free(g_clipboard.last_copied_path);
        g_clipboard.last_copied_path = NULL;
    }

    g_clipboard.last_copied_path = strdup(arg);
    if (g_clipboard.last_copied_path == NULL)
    {
        printf("Error: out of memory while copying file to clipboard\n");
        return -1;
    }

    printf("File '%s' copied to clipboard.\n", g_clipboard.last_copied_path);
    return 0;
}

static int te_paste(const char *arg)
{
    if (arg == NULL || *arg == '\0')
    {
        printf("Error: usage: x <destino>\n");
        return -1;
    }

    if (g_clipboard.last_copied_path == NULL)
    {
        printf("Error: clipboard is empty. Use y <archivo_origen> first.\n");
        return -1;
    }

    if (te_copy_file_with_permissions(g_clipboard.last_copied_path, arg) < 0)
    {
        return -1;
    }

    printf("Pasted '%s' into '%s' preserving permissions.\n", g_clipboard.last_copied_path, arg);
    return 0;
}

/* ========================================================================= *
 *  PARSER AND SHELL INTERFACE
 * ========================================================================= */

static int parse_line_editor(const char *line)
{
    static char *argv[10];
    char buffer[256];
    int argc = 0;

    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, " \t\n");
    while (token != NULL && argc < 10)
    {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }

    if (argc == 0)
        return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, "q") == 0)
    {
        te_close();
    }
    else if (strcmp(cmd, "w") == 0)
    {
        te_save();
    }
    else if (strcmp(cmd, "p") == 0)
    {
        te_print(argc > 1 ? argv[1] : NULL);
    }
    else if (strcmp(cmd, "a") == 0)
    {
        char text[256] = "";
        if (argc > 1)
        {
            size_t used = 0;
            for (int i = 1; i < argc; i++)
            {
                size_t tok_len = strlen(argv[i]);
                if (i > 1 && used + 1 < sizeof(text))
                    text[used++] = ' ';
                if (used + tok_len < sizeof(text))
                {
                    memcpy(text + used, argv[i], tok_len);
                    used += tok_len;
                }
            }
            text[used] = '\0';
        }
        te_append(argc > 1 ? text : NULL);
    }
    else if (strcmp(cmd, "d") == 0)
    {
        te_delete(argc > 1 ? argv[1] : NULL);
    }
    else if (strcmp(cmd, "i") == 0)
    {
        char text[256] = "";
        if (argc > 2)
        {
            size_t used = 0;
            for (int i = 2; i < argc; i++)
            {
                size_t tok_len = strlen(argv[i]);
                if (i > 2 && used + 1 < sizeof(text))
                    text[used++] = ' ';
                if (used + tok_len < sizeof(text))
                {
                    memcpy(text + used, argv[i], tok_len);
                    used += tok_len;
                }
            }
            text[used] = '\0';
        }
        te_insert(argc > 1 ? argv[1] : NULL, argc > 2 ? text : NULL);
    }
    else if (strcmp(cmd, "s") == 0)
    {
        te_search(argc > 1 ? argv[1] : NULL);
    }
    else if (strcmp(cmd, "m") == 0)
    {
        te_metadata();
    }
    else if (strcmp(cmd, "y") == 0)
    {
        te_copy(argc > 1 ? argv[1] : NULL);
    }
    else if (strcmp(cmd, "x") == 0)
    {
        te_paste(argc > 1 ? argv[1] : NULL);
    }
    else
    {
        printf("Unknown command: %s\n", cmd);
    }

    return argc;
}

int cmd_open_text_editor(int argc, char **argv)
{
    char line[256];

    if (argc < 2)
    {
        printf("Error: A single filename is required to open.\n");
        return -1;
    }

    if (te_open(argv[1]) < 0)
    {
        return -1;
    }

    while (global_state.fd != -1)
    {
        /* Print cyan interactive prompt */
        printf("\033[1;36m editor> " COLOR_RESET);
        fflush(stdout);

        /* Read input line. Returns NULL on EOF (Ctrl+D) */
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            printf("\n");
            break;
        }

        /* Tokenize and execute the read line */
        parse_line_editor(line);
    }

    return 0;
}
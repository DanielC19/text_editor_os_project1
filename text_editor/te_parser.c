#include "te_editor.h"

#include <stdio.h>
#include <string.h>

static size_t te_skip_spaces(const char *s, size_t i)
{
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    return i;
}

static size_t te_skip_one_separator(const char *s, size_t i)
{
    if (s[i] == ' ' || s[i] == '\t')
        i++;
    return i;
}

static void te_copy_until_newline(char *dst, size_t dst_size, const char *src)
{
    size_t n = 0;

    if (dst_size == 0)
        return;

    while (src[n] != '\0' && src[n] != '\n' && n + 1 < dst_size)
        n++;

    memcpy(dst, src, n);
    dst[n] = '\0';
}

int te_parse_line_editor(const char *line)
{
    size_t i = 0;
    char cmd[32];
    size_t cmd_len = 0;
    char arg[256];
    size_t arg_len = 0;

    i = te_skip_spaces(line, i);
    if (line[i] == '\0' || line[i] == '\n')
        return 0;

    while (line[i] != '\0' && line[i] != '\n' && line[i] != ' ' && line[i] != '\t' && cmd_len + 1 < sizeof(cmd))
    {
        cmd[cmd_len++] = line[i++];
    }
    cmd[cmd_len] = '\0';

    if (strcmp(cmd, "q") == 0)
    {
        printf("Use 'qw' to save and quit, or 'qd' to quit without saving.\n");
    }
    else if (strcmp(cmd, "qw") == 0)
    {
        te_save();
        te_close();
    }
    else if (strcmp(cmd, "qd") == 0)
    {
        te_close();
    }
    else if (strcmp(cmd, "w") == 0)
    {
        te_save();
    }
    else if (strcmp(cmd, "p") == 0)
    {
        i = te_skip_spaces(line, i);
        te_copy_until_newline(arg, sizeof(arg), line + i);
        arg_len = 0;
        while (arg[arg_len] != '\0' && arg[arg_len] != ' ' && arg[arg_len] != '\t')
            arg_len++;
        arg[arg_len] = '\0';
        te_print(arg[0] ? arg : NULL);
    }
    else if (strcmp(cmd, "a") == 0)
    {
        char text[256];
        i = te_skip_one_separator(line, i);
        te_copy_until_newline(text, sizeof(text), line + i);
        te_append(text[0] ? text : NULL);
    }
    else if (strcmp(cmd, "d") == 0)
    {
        i = te_skip_spaces(line, i);
        te_copy_until_newline(arg, sizeof(arg), line + i);
        arg_len = 0;
        while (arg[arg_len] != '\0' && arg[arg_len] != ' ' && arg[arg_len] != '\t')
            arg_len++;
        arg[arg_len] = '\0';
        te_delete(arg[0] ? arg : NULL);
    }
    else if (strcmp(cmd, "i") == 0)
    {
        char nbuf[32];
        size_t nlen = 0;
        char text[256];

        i = te_skip_spaces(line, i);
        while (line[i] >= '0' && line[i] <= '9' && nlen + 1 < sizeof(nbuf))
            nbuf[nlen++] = line[i++];
        nbuf[nlen] = '\0';

        i = te_skip_one_separator(line, i);
        te_copy_until_newline(text, sizeof(text), line + i);
        te_insert(nlen ? nbuf : NULL, text[0] ? text : NULL);
    }
    else if (strcmp(cmd, "s") == 0)
    {
        i = te_skip_spaces(line, i);
        te_copy_until_newline(arg, sizeof(arg), line + i);
        arg_len = 0;
        while (arg[arg_len] != '\0' && arg[arg_len] != ' ' && arg[arg_len] != '\t')
            arg_len++;
        arg[arg_len] = '\0';
        te_search(arg[0] ? arg : NULL);
    }
    else if (strcmp(cmd, "m") == 0)
    {
        te_metadata();
    }
    else if (strcmp(cmd, "y") == 0)
    {
        i = te_skip_spaces(line, i);
        te_copy_until_newline(arg, sizeof(arg), line + i);
        arg_len = 0;
        while (arg[arg_len] != '\0' && arg[arg_len] != ' ' && arg[arg_len] != '\t')
            arg_len++;
        arg[arg_len] = '\0';
        te_copy(arg[0] ? arg : NULL);
    }
    else if (strcmp(cmd, "x") == 0)
    {
        i = te_skip_spaces(line, i);
        te_copy_until_newline(arg, sizeof(arg), line + i);
        arg_len = 0;
        while (arg[arg_len] != '\0' && arg[arg_len] != ' ' && arg[arg_len] != '\t')
            arg_len++;
        arg[arg_len] = '\0';
        te_paste(arg[0] ? arg : NULL);
    }
    else
    {
        printf("Unknown command: %s\n", cmd);
    }

    return 1;
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
        return -1;

    while (te_global_state.fd != -1)
    {
        printf("\033[1;36m editor> " COLOR_RESET);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            printf("\n");
            break;
        }

        te_parse_line_editor(line);
    }

    return 0;
}

int cmd_help_text_editor(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Este comando solo funciona dentro del editor de texto. Usa 'editor <archivo>' y luego ingresa los comandos.\n");
    return 0;
}

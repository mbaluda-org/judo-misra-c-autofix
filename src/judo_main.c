/*
 *  Judo - Embeddable JSON and JSON5 parser.
 *  Copyright (c) 2025 Railgun Labs, LLC
 *
 *  This software is dual-licensed: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation. For the terms of this
 *  license, see <https://www.gnu.org/licenses/>.
 *
 *  Alternatively, you can license this software under a proprietary
 *  license, as set out in <https://railgunlabs.com/judo/license/>.
 */

// This Judo command-line interface reads JSON from stdin, parses
// it into a tree structure, then walks the tree while printing
// its contents to stdout.

// This program does not attempt to be MISRA C compliant.

#include "judo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

#if defined(_WIN32)
#include <io.h>
#define JUDO_STDOUT_FD 1
#else
#include <unistd.h>
#define JUDO_STDOUT_FD STDOUT_FILENO
#endif

char *judo_readstdin(size_t *size);
void judo_writeall(int32_t fd, const char *buffer, size_t length);

static void judo_write_stdout_literal(const char *text)
{
    judo_writeall(JUDO_STDOUT_FD, text, strlen(text));
}

static void judo_write_stdout_uint32(uint32_t value)
{
    static const char digits[] = "0123456789";
    char buffer[sizeof("4294967295")];
    size_t index = sizeof(buffer);
    uint32_t remaining_value = value;

    do
    {
        index -= 1u;
        buffer[index] = digits[remaining_value % 10u];
        remaining_value /= 10u;
    } while (remaining_value != 0u);

    judo_writeall(JUDO_STDOUT_FD, &buffer[index], sizeof(buffer) - index);
}

struct program_options
{
    bool suppress_output;
    bool pretty_print;
    bool use_tabs;
    bool escape_unicode;
    int indention_width;
};

static int32_t decode_utf8(const char *string, uint32_t *scalar)
{
    static const uint8_t unsafe_utf8_sequence_lengths[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    const uint8_t *bytes = (const uint8_t *)string;
    const int32_t bytes_needed = (int32_t)unsafe_utf8_sequence_lengths[bytes[0]];
    uint32_t cp;
    switch (bytes_needed)
    {
    case 1:
        cp = (uint32_t)bytes[0];
        break;
    case 2:
        cp = ((uint32_t)bytes[0]) & 0x1Fu;
        cp = (cp << 6u) | (((uint32_t)bytes[1]) & 0x3Fu);
        break;
    case 3:
        cp = ((uint32_t)bytes[0]) & 0x0Fu;
        cp = (cp << 6u) | (((uint32_t)bytes[1]) & 0x3Fu);
        cp = (cp << 6u) | (((uint32_t)bytes[2]) & 0x3Fu);
        break;
    case 4:
        cp = ((uint32_t)bytes[0]) & 0x07u;
        cp = (cp << 6u) | (((uint32_t)bytes[1]) & 0x3Fu);
        cp = (cp << 6u) | (((uint32_t)bytes[2]) & 0x3Fu);
        cp = (cp << 6u) | (((uint32_t)bytes[3]) & 0x3Fu);
        break;
    default:
        cp = 0x0;
        break;
    }
    *scalar = cp;
    return bytes_needed;
}

// The column source location refers to the code point index of the error.
// A "proper" column index would refer to the grapheme cluster, but that
// requires implementing the Unicode grapheme cluster break algorithm.
// An implementation of this algorithm is available in the Unicorn library
// available here: <https://railgunlabs.com/unicorn/>.
static void compulate_source_location(const char *input, int32_t input_length, int32_t location, int *line, int *column)
{
    *line = 1;
    *column = 1;

    int32_t at = 0;
    while (at < location)
    {
        if ((at < location + 1) && (at < input_length - 2))
        {
            if (memcmp(&input[at], "\r\n", 2) == 0)
            {
                (*line) += 1;
                (*column) = 1;
                at += 2;
                continue;
            }
        }

        uint32_t cp;
        const int32_t byte_count = decode_utf8(&input[at], &cp);
        switch (cp)
        {
        case 0x000A: // Line feed
        case 0x000D: // Carriage return
        case 0x2028: // Line separator
        case 0x2029: // Paragraph separator
            (*line) += 1;
            (*column) = 1;
            break;
        default:
            (*column) += 1;
            break;
        }

        at += byte_count;
    }
}

static void print_tree(struct judo_value *value, const char *source, const struct program_options *options)
{
    struct judo_span where = {0};
    switch (judo_gettype(value))
    {
    case JUDO_TYPE_NULL:
    case JUDO_TYPE_BOOL:
    case JUDO_TYPE_NUMBER:
    case JUDO_TYPE_STRING:
        where = judo_value2span(value);
        printf("%.*s", where.length, &source[where.offset]);
        break;

    case JUDO_TYPE_ARRAY:
        putchar('[');
        for (struct judo_value *elem = judo_first(value); elem != NULL; elem = judo_next(elem))
        {
            print_tree(elem, source, options);
            if (judo_next(elem) != NULL)
            {
                putchar(',');
            }
        }
        putchar(']');
        break;

    case JUDO_TYPE_OBJECT:
        putchar('{');
        for (struct judo_member *member = judo_membfirst(value); member != NULL; member = judo_membnext(member))
        {
            where = judo_name2span(member);
            printf("%.*s:", where.length, &source[where.offset]);
            print_tree(judo_membvalue(member), source, options);
            if (judo_membnext(member) != NULL)
            {
                putchar(',');
            }
        }
        putchar('}');
        break;

    default:
        break;
    }
}

static void pretty_print_indent(int depth, const struct program_options *options)
{
    // The following printf trick only works if the depth is greater than 0, otherwise
    // if it's zero, then it leaves a single space which we don't want.
    if (depth > 0)
    {
        if (options->use_tabs)
        {
            for (int i = 0; i < depth; i++)
            {
                putchar('\t');
            }
        }
        else
        {
            printf("%*c", depth * options->indention_width, ' ');
        }
    }
}

static void pretty_print_tree(struct judo_value *value, const char *source, int depth, const struct program_options *options)
{
    struct judo_span where = {0};
    switch (judo_gettype(value))
    {
    case JUDO_TYPE_NULL:
    case JUDO_TYPE_BOOL:
    case JUDO_TYPE_NUMBER:
    case JUDO_TYPE_STRING:
        where = judo_value2span(value);
        printf("%.*s", where.length, &source[where.offset]);
        break;

    case JUDO_TYPE_ARRAY:
        if (judo_len(value) == 0)
        {
            printf("[]");
        }
        else
        {
            puts("[");
            for (struct judo_value *elem = judo_first(value); elem != NULL; elem = judo_next(elem))
            {
                pretty_print_indent(depth + 1, options);
                pretty_print_tree(elem, source, depth + 1, options);

                if (judo_next(elem) != NULL)
                {
                    putchar(',');
                }
                putchar('\n');
            }
            pretty_print_indent(depth, options);
            putchar(']');
        }
        break;

    case JUDO_TYPE_OBJECT:
        if (judo_len(value) ==0)
        {
            printf("{}");
        }
        else
        {
            puts("{");
            for (struct judo_member *member = judo_membfirst(value); member != NULL; member = judo_membnext(member))
            {
                pretty_print_indent(depth + 1, options);

                where = judo_name2span(member);
                printf("%.*s: ", where.length, &source[where.offset]);

                pretty_print_tree(judo_membvalue(member), source, depth + 1, options);

                if (judo_membnext(member) != NULL)
                {
                    putchar(',');
                }
                putchar('\n');
            }

            pretty_print_indent(depth, options);
            putchar('}');
        }
        break;

    default:
        break;
    }
}

static void *memfunc(void *user_data, void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return malloc(size);
    }
    else
    {
        free(ptr);
        return NULL;
    }
}

static void judo_main(const struct program_options *options)
{
    size_t dynbuf_length = 0;
    char *dynbuf = judo_readstdin(&dynbuf_length);
    if (dynbuf == NULL)
    {
        fprintf(stderr, "error: failed to read stdin\n");
        exit(2);
    }

    struct judo_error error = {0};
    struct judo_value *root;
    const enum judo_result result = judo_parse(dynbuf, dynbuf_length, &root, &error, NULL, memfunc);
    if (result != JUDO_RESULT_SUCCESS)
    {
        if (result == JUDO_RESULT_OUT_OF_MEMORY)
        {
            fprintf(stderr, "error: memory allocation failed\n");
            free(dynbuf);
            exit(2);
        }

        int line, column;
        compulate_source_location(dynbuf, (int32_t)dynbuf_length, error.where.offset, &line, &column);
        fprintf(stderr, "stdin:%d:%d: error: %s\n", line, column, error.description);
        free(dynbuf);
        exit(1);
    }

    if (!options->suppress_output)
    {
        if (options->pretty_print)
        {
            pretty_print_tree(root, dynbuf, 0, options);
        }
        else
        {
            print_tree(root, dynbuf, options);
        }
    }

    free(dynbuf);
    judo_free(root, NULL, memfunc);
}

int main(int argc, char *argv[])
{
    // Initialize the options structure with its default values.
    // The default values will be used if the user does not specify them.
    struct program_options options = {
        .indention_width = 4,
    };

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 ||
            strcmp(arg, "--help") == 0)
        {
            judo_write_stdout_literal(
                "Usage: judo [options...]\n"
                "\n"
                "Judo is a command-line interface to the C library of the same name.\n"
                "This program reads JSON from stdin and writes it back to stdout.\n"
                "Errors are written to stderr. Column indices are reported relative\n"
                "to the code point (not the code unit or grapheme cluster).\n"
                "\n"
                "Judo is configured at compile-time. This version of Judo was built\n"
                "with the following options:\n");

#if defined(JUDO_RFC4627)
            judo_write_stdout_literal("  JSON standard: RFC 4627\n");
#elif defined(JUDO_RFC8259)
            judo_write_stdout_literal("  JSON standard: RFC 8259\n");
#elif defined(JUDO_JSON5)
            judo_write_stdout_literal("  JSON standard: JSON5\n");
#endif

            judo_write_stdout_literal("  JSON extension(s): \n");
#if defined(JUDO_WITH_COMMENTS)
            judo_write_stdout_literal("    comments\n");
#elif defined(JUDO_WITH_TRAILING_COMMAS)
            judo_write_stdout_literal("    trailing commas\n");
#endif

            judo_write_stdout_literal("  Maximum structure depth: ");
            judo_write_stdout_uint32((uint32_t)JUDO_MAXDEPTH);
            judo_write_stdout_literal(
                "\n"
                "\n"
                "Options:\n"
                "  -q, --quite         Validate the input, but do not print to stdout.\n"
                "                      Check the exit status for success or errors.\n"
                "\n"
                "  -p, --pretty        Print the JSON in a visually appealing way.\n"
                "\n"
                "  -i N, --indent=N    Set the indention width to N spaces when pretty\n"
                "                      printing with spaces (default is 4).\n"
                "  -t, --tabs          Indent with tabs instead of spaces when pretty\n"
                "                      printing.\n"
                "\n"
                "  -v, --version       Prints the Judo library version and exits.\n"
                "  -h, --help          Prints this help message and exits.\n"
                "\n"
                "Exit status:\n"
                "  0  if OK,\n"
                "  1  if the JSON input is malformed,\n"
                "  2  if an error occurred while processing the JSON input,\n"
                "  3  if an invalid command-line option is specified.\n"
                "\n"
                "Judo website and online documentation: <https://railgunlabs.com/judo/>\n"
                "Judo repository: <https://github.com/railgunlabs/judo/>\n"
                "\n"
                "Judo is Free Software distributed under the GNU General Public License\n"
                "version 3 as published by the Free Software Foundation. You may also\n"
                "license Judo under a commercial license, as set out at\n"
                "<https://railgunlabs.com/judo/license/>.\n");
            exit(0);
        }

        if (strcmp(arg, "-v") == 0 ||
            strcmp(arg, "--version") == 0)
        {
            judo_write_stdout_literal("1.1.0\n");
            exit(0);
        }

        if (strcmp(arg, "-q") == 0 ||
            strcmp(arg, "--quite") == 0)
        {
            options.suppress_output = true;
            continue;
        }

        if (strcmp(arg, "-p") == 0 ||
            strcmp(arg, "--pretty") == 0)
        {
            options.pretty_print = true;
            continue;
        }

        if (strcmp(arg, "-t") == 0 ||
            strcmp(arg, "--tabs") == 0)
        {
            options.use_tabs = true;
            continue;
        }


        if (strcmp(arg, "-e") == 0 ||
            strcmp(arg, "--escape") == 0)
        {
            options.escape_unicode = true;
            continue;
        }

        if (strcmp(arg, "-i") == 0 ||
            strncmp(arg, "--indent", 8) == 0)
        {
            if (arg[1] == 'i')
            {
                if (i == argc - 1)
                {
                    fprintf(stderr, "error: expected indention width\n");
                    exit(3);
                }
                i += 1;
                arg = argv[i];
            }
            else
            {
                if (arg[8] != '=')
                {
                    fprintf(stderr, "error: expected indention width\n");
                    exit(3);
                }
                arg += 9;
            }

            char *endptr = NULL;
            errno = 0;
            const unsigned long value = strtoul(arg, &endptr, 10);
            if ((endptr == arg) || (errno != 0))
            {
                fprintf(stderr, "error: invalid or missing indention width\n");
                exit(3);
            }
            else if ((value >= (unsigned long)UINT16_MAX) || (value == 0UL))
            {
                fprintf(stderr, "error: indention width is too large or small\n");
                exit(3);
            }
            else
            {
                options.indention_width = (int)value;
            }
            continue;
        }

        fprintf(stderr, "error: unknown option '%s'\n", arg);
        exit(3);
    }

    judo_main(&options);
    return 0;
}

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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#define JUDO_STDOUT_FD INT32_C(1)
#define JUDO_STDERR_FD INT32_C(2)

typedef int32_t judo_fd_t;
#if defined(_WIN32)
typedef int32_t judo_write_result_t;
#define JUDO_WRITE _write
#else
#if defined(SSIZE_MAX) && defined(INT64_MAX) && (SSIZE_MAX == INT64_MAX)
typedef int64_t judo_write_result_t;
#elif defined(SSIZE_MAX) && defined(INT32_MAX) && (SSIZE_MAX == INT32_MAX)
typedef int32_t judo_write_result_t;
#else
#error Unsupported ssize_t width
#endif
#define JUDO_WRITE write
#endif

char *judo_readstdin(size_t *size);

struct program_options
{
    bool suppress_output;
    bool pretty_print;
    bool use_tabs;
    bool escape_unicode;
    int32_t indention_width;
};

static void judo_write_all(judo_fd_t fd, const char *buffer, size_t length)
{
    size_t next_chunk = 0U;

    for (size_t bytes_written = 0U; bytes_written < length; bytes_written += next_chunk)
    {
        const judo_write_result_t result = JUDO_WRITE(fd, &buffer[bytes_written], length - bytes_written);
        if (result <= 0)
        {
            break;
        }
        next_chunk = (size_t)result;
    }
}

static void judo_write_char(judo_fd_t fd, char ch)
{
    judo_write_all(fd, &ch, (size_t)1U);
}

static void judo_write_cstr(judo_fd_t fd, const char *string)
{
    judo_write_all(fd, string, strlen(string));
}

static void judo_write_line(judo_fd_t fd, const char *string)
{
    judo_write_cstr(fd, string);
    judo_write_char(fd, '\n');
}

static void judo_write_span(judo_fd_t fd, const char *source, struct judo_span where)
{
    judo_write_all(fd, &source[where.offset], (size_t)where.length);
}

static void judo_write_uint32(judo_fd_t fd, uint32_t value)
{
    static const char decimal_digits[] = "0123456789";
    char digits[32];
    size_t length = 0;
    uint32_t remaining = value;

    do
    {
        const uint32_t digit = remaining % 10U;
        digits[length] = decimal_digits[(size_t)digit];
        remaining /= 10U;
        length++;
    } while (remaining != 0U);

    while (length > 0U)
    {
        length -= 1U;
        judo_write_char(fd, digits[length]);
    }
}

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
static void compute_source_location(const char *input, int32_t input_length, int32_t location, int32_t *line, int32_t *column)
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
        judo_write_span(JUDO_STDOUT_FD, source, where);
        break;

    case JUDO_TYPE_ARRAY:
        judo_write_char(JUDO_STDOUT_FD, '[');
        for (struct judo_value *elem = judo_first(value); elem != NULL; elem = judo_next(elem))
        {
            print_tree(elem, source, options);
            if (judo_next(elem) != NULL)
            {
                judo_write_char(JUDO_STDOUT_FD, ',');
            }
        }
        judo_write_char(JUDO_STDOUT_FD, ']');
        break;

    case JUDO_TYPE_OBJECT:
        judo_write_char(JUDO_STDOUT_FD, '{');
        for (struct judo_member *member = judo_membfirst(value); member != NULL; member = judo_membnext(member))
        {
            where = judo_name2span(member);
            judo_write_span(JUDO_STDOUT_FD, source, where);
            judo_write_char(JUDO_STDOUT_FD, ':');
            print_tree(judo_membvalue(member), source, options);
            if (judo_membnext(member) != NULL)
            {
                judo_write_char(JUDO_STDOUT_FD, ',');
            }
        }
        judo_write_char(JUDO_STDOUT_FD, '}');
        break;

    default:
        break;
    }
}

static void pretty_print_indent(int32_t depth, const struct program_options *options)
{
    // The following printf trick only works if the depth is greater than 0, otherwise
    // if it's zero, then it leaves a single space which we don't want.
    if (depth > 0)
    {
        if (options->use_tabs)
        {
            for (int32_t i = 0; i < depth; i++)
            {
                judo_write_char(JUDO_STDOUT_FD, '\t');
            }
        }
        else
        {
            const int32_t width = depth * options->indention_width;
            for (int32_t i = 0; i < width; i++)
            {
                judo_write_char(JUDO_STDOUT_FD, ' ');
            }
        }
    }
}

static void pretty_print_tree(struct judo_value *value, const char *source, int32_t depth, const struct program_options *options)
{
    struct judo_span where = {0};
    switch (judo_gettype(value))
    {
    case JUDO_TYPE_NULL:
    case JUDO_TYPE_BOOL:
    case JUDO_TYPE_NUMBER:
    case JUDO_TYPE_STRING:
        where = judo_value2span(value);
        judo_write_span(JUDO_STDOUT_FD, source, where);
        break;

    case JUDO_TYPE_ARRAY:
        if (judo_len(value) == 0)
        {
            judo_write_cstr(JUDO_STDOUT_FD, "[]");
        }
        else
        {
            judo_write_line(JUDO_STDOUT_FD, "[");
            for (struct judo_value *elem = judo_first(value); elem != NULL; elem = judo_next(elem))
            {
                pretty_print_indent(depth + 1, options);
                pretty_print_tree(elem, source, depth + 1, options);

                if (judo_next(elem) != NULL)
                {
                    judo_write_char(JUDO_STDOUT_FD, ',');
                }
                judo_write_char(JUDO_STDOUT_FD, '\n');
            }
            pretty_print_indent(depth, options);
            judo_write_char(JUDO_STDOUT_FD, ']');
        }
        break;

    case JUDO_TYPE_OBJECT:
        if (judo_len(value) == 0)
        {
            judo_write_cstr(JUDO_STDOUT_FD, "{}");
        }
        else
        {
            judo_write_line(JUDO_STDOUT_FD, "{");
            for (struct judo_member *member = judo_membfirst(value); member != NULL; member = judo_membnext(member))
            {
                pretty_print_indent(depth + 1, options);

                where = judo_name2span(member);
                judo_write_span(JUDO_STDOUT_FD, source, where);
                judo_write_cstr(JUDO_STDOUT_FD, ": ");

                pretty_print_tree(judo_membvalue(member), source, depth + 1, options);

                if (judo_membnext(member) != NULL)
                {
                    judo_write_char(JUDO_STDOUT_FD, ',');
                }
                judo_write_char(JUDO_STDOUT_FD, '\n');
            }

            pretty_print_indent(depth, options);
            judo_write_char(JUDO_STDOUT_FD, '}');
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
        judo_write_cstr(JUDO_STDERR_FD, "error: failed to read stdin\n");
        exit(2);
    }

    struct judo_error error = {0};
    struct judo_value *root;
    const enum judo_result result = judo_parse(dynbuf, dynbuf_length, &root, &error, NULL, memfunc);
    if (result != JUDO_RESULT_SUCCESS)
    {
        if (result == JUDO_RESULT_OUT_OF_MEMORY)
        {
            judo_write_cstr(JUDO_STDERR_FD, "error: memory allocation failed\n");
            free(dynbuf);
            exit(2);
        }

        int32_t line;
        int32_t column;
        compute_source_location(dynbuf, (int32_t)dynbuf_length, error.where.offset, &line, &column);
        judo_write_cstr(JUDO_STDERR_FD, "stdin:");
        judo_write_uint32(JUDO_STDERR_FD, (uint32_t)line);
        judo_write_char(JUDO_STDERR_FD, ':');
        judo_write_uint32(JUDO_STDERR_FD, (uint32_t)column);
        judo_write_cstr(JUDO_STDERR_FD, ": error: ");
        judo_write_cstr(JUDO_STDERR_FD, error.description);
        judo_write_char(JUDO_STDERR_FD, '\n');
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

    for (int32_t i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 ||
            strcmp(arg, "--help") == 0)
        {
            judo_write_line(JUDO_STDOUT_FD, "Usage: judo [options...]");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "Judo is a command-line interface to the C library of the same name.");
            judo_write_line(JUDO_STDOUT_FD, "This program reads JSON from stdin and writes it back to stdout.");
            judo_write_line(JUDO_STDOUT_FD, "Errors are written to stderr. Column indices are reported relative");
            judo_write_line(JUDO_STDOUT_FD, "to the code point (not the code unit or grapheme cluster).");
            judo_write_line(JUDO_STDOUT_FD, "");

            judo_write_line(JUDO_STDOUT_FD, "Judo is configured at compile-time. This version of Judo was built");
            judo_write_line(JUDO_STDOUT_FD, "with the following options:");

#if defined(JUDO_RFC4627)
            judo_write_line(JUDO_STDOUT_FD, "  JSON standard: RFC 4627");
#elif defined(JUDO_RFC8259)
            judo_write_line(JUDO_STDOUT_FD, "  JSON standard: RFC 8259");
#elif defined(JUDO_JSON5)
            judo_write_line(JUDO_STDOUT_FD, "  JSON standard: JSON5");
#endif

            judo_write_line(JUDO_STDOUT_FD, "  JSON extension(s): ");
#if defined(JUDO_WITH_COMMENTS)
            judo_write_line(JUDO_STDOUT_FD, "    comments");
#elif defined(JUDO_WITH_TRAILING_COMMAS)
            judo_write_line(JUDO_STDOUT_FD, "    trailing commas");
#endif

            judo_write_cstr(JUDO_STDOUT_FD, "  Maximum structure depth: ");
            judo_write_uint32(JUDO_STDOUT_FD, (uint32_t)JUDO_MAXDEPTH);
            judo_write_char(JUDO_STDOUT_FD, '\n');

            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "Options:");
            judo_write_line(JUDO_STDOUT_FD, "  -q, --quiet         Validate the input, but do not print to stdout.");
            judo_write_line(JUDO_STDOUT_FD, "                      Check the exit status for success or errors.");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "  -p, --pretty        Print the JSON in a visually appealing way.");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "  -i N, --indent=N    Set the indention width to N spaces when pretty");
            judo_write_line(JUDO_STDOUT_FD, "                      printing with spaces (default is 4).");
            judo_write_line(JUDO_STDOUT_FD, "  -t, --tabs          Indent with tabs instead of spaces when pretty");
            judo_write_line(JUDO_STDOUT_FD, "                      printing.");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "  -v, --version       Prints the Judo library version and exits.");
            judo_write_line(JUDO_STDOUT_FD, "  -h, --help          Prints this help message and exits.");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "Exit status:");
            judo_write_line(JUDO_STDOUT_FD, "  0  if OK,");
            judo_write_line(JUDO_STDOUT_FD, "  1  if the JSON input is malformed,");
            judo_write_line(JUDO_STDOUT_FD, "  2  if an error occurred while processing the JSON input,");
            judo_write_line(JUDO_STDOUT_FD, "  3  if an invalid command-line option is specified.");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "Judo website and online documentation: <https://railgunlabs.com/judo/>");
            judo_write_line(JUDO_STDOUT_FD, "Judo repository: <https://github.com/railgunlabs/judo/>");
            judo_write_line(JUDO_STDOUT_FD, "");
            judo_write_line(JUDO_STDOUT_FD, "Judo is Free Software distributed under the GNU General Public License");
            judo_write_line(JUDO_STDOUT_FD, "version 3 as published by the Free Software Foundation. You may also");
            judo_write_line(JUDO_STDOUT_FD, "license Judo under a commercial license, as set out at");
            judo_write_line(JUDO_STDOUT_FD, "<https://railgunlabs.com/judo/license/>.");
            exit(0);
        }

        if (strcmp(arg, "-v") == 0 ||
            strcmp(arg, "--version") == 0)
        {
            judo_write_line(JUDO_STDOUT_FD, "1.1.0");
            exit(0);
        }

        if ((strcmp(arg, "-q") == 0) ||
            (strcmp(arg, "--quiet") == 0) ||
            (strcmp(arg, "--quite") == 0))
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
                    judo_write_cstr(JUDO_STDERR_FD, "error: expected indention width\n");
                    exit(3);
                }
                i += 1;
                arg = argv[i];
            }
            else
            {
                if (arg[8] != '=')
                {
                    judo_write_cstr(JUDO_STDERR_FD, "error: expected indention width\n");
                    exit(3);
                }
                arg += 9;
            }

            char *endptr = NULL;
            errno = 0;
            const unsigned long value = strtoul(arg, &endptr, 10);
            if ((endptr == arg) || (errno != 0))
            {
                judo_write_cstr(JUDO_STDERR_FD, "error: invalid or missing indention width\n");
                exit(3);
            }
            else if ((value >= (unsigned long)UINT16_MAX) || (value == 0UL))
            {
                judo_write_cstr(JUDO_STDERR_FD, "error: indention width is too large or small\n");
                exit(3);
            }
            else
            {
                options.indention_width = (int32_t)value;
            }
            continue;
        }

        judo_write_cstr(JUDO_STDERR_FD, "error: unknown option '");
        judo_write_cstr(JUDO_STDERR_FD, arg);
        judo_write_cstr(JUDO_STDERR_FD, "'\n");
        exit(3);
    }

    judo_main(&options);
    return 0;
}

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

// This example uses the parser to create an in-memory tree from JSON source
// text. Each node of the tree represents a JSON value with the root being
// the top-level JSON value (often an object or array). The example recurses
// through the tree and prints each value to stdout. The result is a compact
// representation of the original JSON source text.

// This code does not attempt to be MISRA compliant.

#include "judo.h"
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

#if defined(_WIN32)
#include <io.h>
#define JUDO_STDOUT_FD 1
#define JUDO_STDERR_FD 2
#else
#include <unistd.h>
#define JUDO_STDOUT_FD STDOUT_FILENO
#define JUDO_STDERR_FD STDERR_FILENO
#endif

char *judo_readstdin(size_t *size);

static long judo_writefd(int fd, const char *buffer, size_t length)
{
#if defined(_WIN32)
    return (long)_write(fd, buffer, (unsigned int)length);
#else
    return (long)write(fd, buffer, length);
#endif
}

static void write_all(int fd, const char *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        const long written = judo_writefd(fd, &buffer[total], length - total);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (written == 0)
        {
            break;
        }
        total += (size_t)written;
    }
}

static void write_string(int fd, const char *string)
{
    size_t length = 0;
    while (string[length] != '\0')
    {
        length++;
    }
    write_all(fd, string, length);
}

//! [parser_process_memory]
void *memfunc(void *user_data, void *ptr, size_t size)
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
//! [parser_process_memory]

void print_tree(const char *source, struct judo_value *value)
{
    struct judo_span span;
    struct judo_value *elem;
    struct judo_member *member;

//! [parser_process_traverse]
    switch (judo_gettype(value))
    {
    case JUDO_TYPE_NULL:
    case JUDO_TYPE_BOOL:
    case JUDO_TYPE_NUMBER:
    case JUDO_TYPE_STRING:
        span = judo_value2span(value);
        write_all(JUDO_STDOUT_FD, &source[span.offset], span.length);
        break;
    // [cont...]
//! [parser_process_traverse]

//! [parser_process_array]
    case JUDO_TYPE_ARRAY:
        write_all(JUDO_STDOUT_FD, "[", 1);
        elem = judo_first(value);
        while (elem != NULL)
        {
            print_tree(source, elem);
            if (judo_next(elem) != NULL)
            {
                write_all(JUDO_STDOUT_FD, ",", 1);
            }
            elem = judo_next(elem);
        }
        write_all(JUDO_STDOUT_FD, "]", 1);
        break;
    // [cont...]
//! [parser_process_array]

//! [parser_process_object]
    case JUDO_TYPE_OBJECT:
        write_all(JUDO_STDOUT_FD, "{", 1);
        member = judo_membfirst(value);
        while (member != NULL)
        {
            span = judo_name2span(member);
            write_all(JUDO_STDOUT_FD, &source[span.offset], span.length);
            write_all(JUDO_STDOUT_FD, ":", 1);
            print_tree(source, judo_membvalue(member));
            if (judo_membnext(member) != NULL)
            {
                write_all(JUDO_STDOUT_FD, ",", 1);
            }
            member = judo_membnext(member);
        }
        write_all(JUDO_STDOUT_FD, "}", 1);
        break;
//! [parser_process_object]

    default:
        break;
    }
}

int main(int argc, char *argv[])
{
//! [parser_process_stdin]
    size_t json_len = 0;
    const char *json = judo_readstdin(&json_len);
//! [parser_process_stdin]
    if (json == NULL)
    {
        write_string(JUDO_STDERR_FD, "error: failed to read stdin\n");
        return 2;
    }

//! [parser_process_input]
    struct judo_error error = {0};
    struct judo_value *root;
    enum judo_result result = judo_parse(json, json_len, &root, &error, NULL, memfunc);
    if (result == JUDO_RESULT_SUCCESS)
    {
        print_tree(json, root);
        judo_free(root, NULL, memfunc);
    }
    else
    {
        write_string(JUDO_STDERR_FD, "error: ");
        write_string(JUDO_STDERR_FD, error.description);
        write_all(JUDO_STDERR_FD, "\n", 1);
        return 1;
    }
//! [parser_process_input]

    return 0;
}

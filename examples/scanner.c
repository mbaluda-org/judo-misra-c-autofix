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

// This example scans JSON source text and prints, to stdout, each token
// on its own separate line. Numbers, strings, and member names are printed
// by lexeme. You can use 'judo_stringify' and 'judo_numberify' functions
// to obtain the escaped string and floating-point number.

// This code does not attempt to be MISRA compliant.

#include "judo.h"
#include <stddef.h>
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

static void process_token(struct judo_stream stream, const char *json)
{
//! [scanner_process_token]
    switch (stream.token)
    {
    case JUDO_TOKEN_NULL: write_string(JUDO_STDOUT_FD, "null\n"); break;
    case JUDO_TOKEN_TRUE: write_string(JUDO_STDOUT_FD, "true\n"); break;
    case JUDO_TOKEN_FALSE: write_string(JUDO_STDOUT_FD, "false\n"); break;
    case JUDO_TOKEN_ARRAY_BEGIN: write_string(JUDO_STDOUT_FD, "[push]\n"); break;
    case JUDO_TOKEN_ARRAY_END: write_string(JUDO_STDOUT_FD, "[pop]\n"); break;
    case JUDO_TOKEN_OBJECT_BEGIN: write_string(JUDO_STDOUT_FD, "{push}\n"); break;
    case JUDO_TOKEN_OBJECT_END: write_string(JUDO_STDOUT_FD, "{pop}\n"); break;
    case JUDO_TOKEN_NUMBER:
        write_string(JUDO_STDOUT_FD, "number: ");
        write_all(JUDO_STDOUT_FD, &json[stream.where.offset], stream.where.length);
        write_all(JUDO_STDOUT_FD, "\n", 1);
        break;
    case JUDO_TOKEN_STRING:
        write_string(JUDO_STDOUT_FD, "string: ");
        write_all(JUDO_STDOUT_FD, &json[stream.where.offset], stream.where.length);
        write_all(JUDO_STDOUT_FD, "\n", 1);
        break;
    case JUDO_TOKEN_OBJECT_NAME:
        write_string(JUDO_STDOUT_FD, "{name: ");
        write_all(JUDO_STDOUT_FD, &json[stream.where.offset], stream.where.length);
        write_all(JUDO_STDOUT_FD, "}\n", 2);
        break;
    default:
        break;
    }
//! [scanner_process_token]
}

int main(int argc, char *argv[])
{
//! [scanner_process_stdin]
    size_t json_len = 0;
    const char *json = judo_readstdin(&json_len);
//! [scanner_process_stdin]
    if (json == NULL)
    {
        write_string(JUDO_STDERR_FD, "error: failed to read stdin\n");
        return 2;
    }

//! [scanner_process_stream]
    struct judo_stream stream = {0};
    enum judo_result result;
    for (;;)
    {
        result = judo_scan(&stream, json, json_len);
        if (result == JUDO_RESULT_SUCCESS)
        {
            if (stream.token == JUDO_TOKEN_EOF)
            {
                break;
            }
            process_token(stream, json);
        }
        else
        {
            write_string(JUDO_STDERR_FD, "error: ");
            write_string(JUDO_STDERR_FD, stream.error);
            write_all(JUDO_STDERR_FD, "\n", 1);
            return 1;
        }
    }
//! [scanner_process_stream]

    return 0;
}

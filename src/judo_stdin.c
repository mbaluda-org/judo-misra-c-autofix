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

// Reads input from stdin on both Windows and *nix systems. This is used
// by the command-line interface and Judo examples. This code does not
// attempt to be MISRA compliant.

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if defined(_WIN32)
#include <io.h>
#define JUDO_STDIN_FD 0
#define JUDO_STDERR_FD 2
#else
#include <unistd.h>
#define JUDO_STDIN_FD STDIN_FILENO
#define JUDO_STDERR_FD STDERR_FILENO
#endif

static int judo_write_all(int fd, const char *buffer, size_t length)
{
    while (length > 0u)
    {
#if defined(_WIN32)
        const unsigned int chunk_length = (length > (size_t)INT_MAX) ? (unsigned int)INT_MAX : (unsigned int)length;
        const int bytes_written = _write(fd, buffer, chunk_length);
#else
        const ssize_t bytes_written = write(fd, buffer, length);
#endif
        if (bytes_written <= 0)
        {
            return -1;
        }

        buffer += (size_t)bytes_written;
        length -= (size_t)bytes_written;
    }

    return 0;
}

char *judo_readstdin(size_t *size)
{
    char *dynbuf = NULL;
    size_t dynbuf_length = 0;
    size_t dynbuf_capacity = 0;

    for (;;)
    {
        char buffer[4096];
#if defined(_WIN32)
        const int bytes_read = _read(JUDO_STDIN_FD, buffer, sizeof(buffer));
#else
        const ssize_t bytes_read = read(JUDO_STDIN_FD, buffer, sizeof(buffer));
#endif
        if (bytes_read == 0)
        {
            break;
        }
        else if (bytes_read < 0)
        {
            free(dynbuf);
            return NULL;
        }

        const size_t buffer_length = (size_t)bytes_read;
        const size_t new_capacity = dynbuf_length + buffer_length;

        // Limit the input to 10 megabytes to avoid integer overflow elsewhere in the implementation.
        // This also ensures the buffer capacity remains under the maximum signed 32-bit integer.
        if (new_capacity >= 1024 * 1024 * 10)
        {
            (void)judo_write_all(JUDO_STDERR_FD, "error: input too large\n", sizeof("error: input too large\n") - 1u);
            free(dynbuf);
            return NULL;
        }

        if (new_capacity >= dynbuf_capacity)
        {
            char *tmpbuf = realloc(dynbuf, new_capacity);
            if (tmpbuf == NULL)
            {
                free(dynbuf);
                return NULL;
            }
            dynbuf = tmpbuf;
            dynbuf_capacity = new_capacity;
        }

        memcpy(&dynbuf[dynbuf_length], buffer, buffer_length);
        dynbuf_length += buffer_length;
    }

    *size = dynbuf_length;
    return dynbuf;
}

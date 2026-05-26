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
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEFAULT_PAGE_SIZE 4096U
#define MMAP_NO_FD (-1)
#define MMAP_NO_OFFSET 0

_Static_assert(sizeof(uintptr_t) == sizeof(void *), "uintptr_t must match pointer width");

char *judo_readstdin(size_t *size);

static size_t page_size(void)
{
    const long size = sysconf(_SC_PAGESIZE);
    if (size <= 0)
    {
        return DEFAULT_PAGE_SIZE;
    }
    return (size_t)size;
}

static size_t mapping_size(size_t size)
{
    const size_t page_bytes = page_size();
    size_t padding = 0U;
    size_t rounded_size = 0U;

    if (size != 0U)
    {
        padding = size % page_bytes;
        if (padding != 0U)
        {
            padding = page_bytes - padding;
        }

        if (size <= (SIZE_MAX - padding))
        {
            rounded_size = size + padding;
        }
    }

    return rounded_size;
}

/*
 * MAP_FAILED is defined by this platform as (void *)-1, and uintptr_t has the
 * same width as a data pointer on this platform. Copy the pointer object
 * representation to check for the all-bits-one sentinel without a direct cast.
 */
static uint8_t mapping_failed(const void *mapping)
{
    union
    {
        const void *pointer;
        unsigned char bytes[sizeof(void *)];
    } representation = { NULL };
    unsigned char failed_pattern[sizeof(representation.bytes)] = { 0U };
    uint8_t failed = 0U;

    representation.pointer = mapping;
    (void)memset(failed_pattern, UCHAR_MAX, sizeof(failed_pattern));
    if (memcmp(representation.bytes, failed_pattern, sizeof(representation.bytes)) == 0)
    {
        failed = 1U;
    }

    return failed;
}

//! [parser_process_memory]
static void *memfunc(void *user_data, void *ptr, size_t size)
{
    const int prot = (int)((unsigned int)PROT_READ | (unsigned int)PROT_WRITE);
    const int flags = (int)((unsigned int)MAP_PRIVATE | (unsigned int)MAP_ANONYMOUS);
    const void * const context = user_data;
    void *result = NULL;
    size_t rounded_size = 0U;

    (void)context;
    rounded_size = mapping_size(size);

    if (rounded_size != 0U)
    {
        if (ptr == NULL)
        {
            union
            {
                const void *mapping;
                void *result;
            } mapped = { NULL };

            mapped.mapping = mmap(NULL, rounded_size, prot, flags, MMAP_NO_FD, MMAP_NO_OFFSET);
            if (mapping_failed(mapped.mapping) == 0U)
            {
                result = mapped.result;
            }
        }
        else
        {
            (void)munmap(ptr, rounded_size);
        }
    }

    return result;
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
        printf("%.*s", span.length, &source[span.offset]);
        break;
    // [cont...]
//! [parser_process_traverse]

//! [parser_process_array]
    case JUDO_TYPE_ARRAY:
        putchar('[');
        elem = judo_first(value);
        while (elem != NULL)
        {
            print_tree(source, elem);
            if (judo_next(elem) != NULL)
            {
                putchar(',');
            }
            elem = judo_next(elem);
        }
        putchar(']');
        break;
    // [cont...]
//! [parser_process_array]

//! [parser_process_object]
    case JUDO_TYPE_OBJECT:
        putchar('{');
        member = judo_membfirst(value);
        while (member != NULL)
        {
            span = judo_name2span(member);
            printf("%.*s:", span.length, &source[span.offset]);
            print_tree(source, judo_membvalue(member));
            if (judo_membnext(member) != NULL)
            {
                putchar(',');
            }
            member = judo_membnext(member);
        }
        putchar('}');
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
        fprintf(stderr, "error: failed to read stdin\n");
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
        fprintf(stderr, "error: %s\n", error.description);
        return 1;
    }
//! [parser_process_input]

    return 0;
}

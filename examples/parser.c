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
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

char *judo_readstdin(size_t *size);

//! [parser_process_memory]
struct example_allocation_header
{
    size_t size;
};

void *example_mem_allocator(void *user_data, void *ptr, size_t size)
{
    (void)user_data;

    if (ptr == NULL)
    {
        const size_t total_size = sizeof(struct example_allocation_header) + size;
#if defined(_WIN32)
        struct example_allocation_header *header = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (header == NULL)
        {
            return NULL;
        }
#else
        struct example_allocation_header *header = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (header == MAP_FAILED)
        {
            return NULL;
        }
#endif
        header->size = total_size;
        return &header[1];
    }

    {
        struct example_allocation_header *header = &((struct example_allocation_header *)ptr)[-1];
        (void)size;
#if defined(_WIN32)
        (void)VirtualFree(header, 0, MEM_RELEASE);
#else
        (void)munmap(header, header->size);
#endif
        return NULL;
    }
}
//! [parser_process_memory]

void print_tree(const char *source, struct judo_value *value)
{
    struct print_frame
    {
        struct judo_value *value;
        struct judo_value *element;
        struct judo_member *member;
        uint8_t state;
    };

    struct judo_span span;
    int32_t depth = 1;
    struct print_frame stack[JUDO_MAXDEPTH + 1] = {0};

    stack[0].value = value;
    while (depth > 0)
    {
        struct print_frame *top = &stack[depth - 1];

//! [parser_process_traverse]
        if (top->state == 0U)
        {
            switch (judo_gettype(top->value))
            {
            case JUDO_TYPE_NULL:
            case JUDO_TYPE_BOOL:
            case JUDO_TYPE_NUMBER:
            case JUDO_TYPE_STRING:
                span = judo_value2span(top->value);
                printf("%.*s", span.length, &source[span.offset]);
                depth -= 1;
                break;
    // [cont...]
//! [parser_process_traverse]

//! [parser_process_array]
            case JUDO_TYPE_ARRAY:
                putchar('[');
                top->element = judo_first(top->value);
                if (top->element == NULL)
                {
                    putchar(']');
                    depth -= 1;
                }
                else
                {
                    top->state = 1U;
                    stack[depth] = (struct print_frame){
                        .value = top->element,
                    };
                    depth += 1;
                }
                break;
    // [cont...]
//! [parser_process_array]

//! [parser_process_object]
            case JUDO_TYPE_OBJECT:
                putchar('{');
                top->member = judo_membfirst(top->value);
                if (top->member == NULL)
                {
                    putchar('}');
                    depth -= 1;
                }
                else
                {
                    top->state = 2U;
                    span = judo_name2span(top->member);
                    printf("%.*s:", span.length, &source[span.offset]);
                    stack[depth] = (struct print_frame){
                        .value = judo_membvalue(top->member),
                    };
                    depth += 1;
                }
                break;
//! [parser_process_object]

            default:
                depth -= 1;
                break;
            }
        }
        else if (top->state == 1U)
        {
            top->element = judo_next(top->element);
            if (top->element == NULL)
            {
                putchar(']');
                depth -= 1;
            }
            else
            {
                putchar(',');
                stack[depth] = (struct print_frame){
                    .value = top->element,
                };
                depth += 1;
            }
        }
        else
        {
            top->member = judo_membnext(top->member);
            if (top->member == NULL)
            {
                putchar('}');
                depth -= 1;
            }
            else
            {
                putchar(',');
                span = judo_name2span(top->member);
                printf("%.*s:", span.length, &source[span.offset]);
                stack[depth] = (struct print_frame){
                    .value = judo_membvalue(top->member),
                };
                depth += 1;
            }
        }
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
    enum judo_result result = judo_parse(json, json_len, &root, &error, NULL, example_mem_allocator);
    if (result == JUDO_RESULT_SUCCESS)
    {
        print_tree(json, root);
        judo_free(root, NULL, example_mem_allocator);
    }
    else
    {
        fprintf(stderr, "error: %s\n", error.description);
        return 1;
    }
//! [parser_process_input]

    return 0;
}

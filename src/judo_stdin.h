#ifndef JUDO_STDIN_H
#define JUDO_STDIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool judo_writeall(int32_t fd, const char *buffer, size_t length);
char *judo_readstdin(size_t *size);

#endif

#ifndef USING_INTERPRETER_H
#define USING_INTERPRETER_H

#include <sys/types.h>

typedef struct {
	ssize_t lowest_negative_skip;
	ssize_t highest_positive_skip;
} interpreter_meta;

void execute_bf(char *bytecode, interpreter_meta meta);

#endif

#include<stdbool.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<err.h>

#include "parser.h"
#include "flattener.h"
#include "interpreter.h"
#include "debug.h"

/*
 * Brainfuck basics:
 *
 * + (*data)++
 * - (*data)--
 * > data++
 * < data--
 * . out data
 * , in data
 * [ jump to matching ] if !*data
 * ] jump to matching [
 */


int main(int argc, char **argv){
	if (argc != 2) errx(1, "Need exactly 1 argument (file path)");
	char *filename = argv[1];

	int fd = open(filename, O_RDONLY);
	if (fd == -1) err(1, "Can't open file %s", filename);
	size_t size = (size_t) lseek(fd, 0, SEEK_END);
	if (size == (size_t)-1) err(1, "Can't seek file %s", filename);
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) err(1, "Can't mmap file %s", filename);

	bf_op root = build_bf_tree(&(blob_cursor){.data = map, .len = size});

#ifndef NDEBUG
	print_bf_op(&root, 0);
	printf("\n\n");
#endif

	blob_cursor flat = {
		.data = malloc(128),
		.pos = 0,
		.len = 128,
	};
	flatten_bf(&root, &flat);

	// For the tiny savings this will give us...
	free_bf_op_children(&root);

#ifndef NDEBUG
	print_flattened(flat.data);
	printf("\n\n");
#endif

	execute_bf(flat.data);

	free(flat.data);
	munmap(map, size);
	close(fd);
}

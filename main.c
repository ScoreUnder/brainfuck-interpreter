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

void usage(FILE *send_help_to, int exitcode) {
	fputs(
			"Optimizing brainfuck interpreter\n"
			"Options:\n"
			"\t--dump-tree       Dump the optimized representation of the brainfuck program in tree form before execution\n"
			"\t--dump-opcodes    Dump the flat optimized representation of the brainfuck program before execution\n"
			"\t--no-execute      Do not execute the brainfuck program\n"
			"\t--help            Print this help message\n",
			send_help_to
	);
	exit(exitcode);
}

int main(int argc, char **argv){
	bool dump_tree = false, dump_opcodes = false, execute = true;

	int argpos = 1;
	for (; argpos < argc; argpos++) {
		// If options stop, stop parsing options
		if (argv[argpos][0] != '-') break;

		if (!strcmp(argv[argpos], "--dump-opcodes")) {
			dump_opcodes = true;
		} else if (!strcmp(argv[argpos], "--dump-tree")) {
			dump_tree = true;
		} else if (!strcmp(argv[argpos], "--no-execute")) {
			execute = false;
		} else if (!strcmp(argv[argpos], "--")) {
			argpos++;
			break;
		} else if (!strcmp(argv[argpos], "--help")) {
			usage(stdout, 0);
		} else {
			warnx("Invalid argument %s", argv[argpos]);
			usage(stderr, 1);
		}
	}

	if (argpos != argc - 1) {
		warnx("Need exactly 1 filename");
		usage(stderr, 1);
	}

	char *filename = argv[argpos];

	int fd = open(filename, O_RDONLY);
	if (fd == -1) err(1, "Can't open file %s", filename);
	size_t size = (size_t) lseek(fd, 0, SEEK_END);
	if (size == (size_t)-1) err(1, "Can't seek file %s", filename);
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) err(1, "Can't mmap file %s", filename);

	bf_op root = build_bf_tree(&(blob_cursor){.data = map, .len = size});

	munmap(map, size);
	close(fd);

	if (dump_tree)
		print_bf_op(&root, 0);

	blob_cursor flat = {
		.data = malloc(128),
		.pos = 0,
		.len = 128,
	};
	flatten_bf(&root, &flat);

	// For the tiny savings this will give us...
	free_bf_op_children(&root);

	if (dump_opcodes)
		print_flattened(flat.data);

	if (execute)
		execute_bf(flat.data);

	free(flat.data);
}

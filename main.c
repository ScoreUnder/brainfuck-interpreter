#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

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

void usage(char *my_name, FILE *send_help_to, int exitcode) {
	fprintf(send_help_to,
			"Optimizing brainfuck interpreter\n"
			"\n"
			"Usage:\n"
			"\t%s [OPTIONS...] [FILE]\n"
			"\n"
			"If no file is specified, brainfuck code will be read from stdin until a bang (!) is reached.\n"
			"\n"
			"Options:\n"
			"\t--dump-tree       Dump the optimized representation of the brainfuck program in tree form before execution\n"
			"\t--dump-opcodes    Dump the flat optimized representation of the brainfuck program before execution\n"
			"\t--no-execute      Do not execute the brainfuck program\n"
			"\t--help            Print this help message\n"
			"\t--                Stop parsing options. The next argument, if any, will be treated as the filename\n",
			my_name
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
			usage(argv[0], stdout, 0);
		} else {
			warnx("Invalid argument %s", argv[argpos]);
			usage(argv[0], stderr, 1);
		}
	}

	if (argpos < argc - 1) {
		warnx("Cannot handle more than 1 filename.\nUse an external utility like cat if you want to join source code together.");
		usage(argv[0], stderr, 1);
	}

	FILE *file;

	if (argpos < argc) {
		char *filename = argv[argpos];
		file = fopen(filename, "r");

		if (!file) err(1, "Can't open file %s", filename);
	} else {
		file = stdin;
	}

	bf_op root = build_bf_tree(file, file == stdin);

	if (file != stdin)
		fclose(file);

	if (dump_tree)
		print_bf_op(&root, 0);

	blob_cursor flat = {
		.data = malloc(128),
		.pos = 0,
		.len = 128,
	};
	interpreter_meta meta = flatten_bf(&root, &flat);

	// For the tiny savings this will give us...
	free_bf_op_children(&root);

	if (dump_opcodes)
		print_flattened(flat.data);

	if (execute)
		execute_bf(flat.data, meta);

	free(flat.data);
}

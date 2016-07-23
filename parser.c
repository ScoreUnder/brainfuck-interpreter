#include <err.h>

#include "parser.h"
#include "optimizer.h"

static char op_type_for_char[] = {
	['+'] = BF_OP_ALTER,
	['-'] = BF_OP_ALTER,
	['<'] = BF_OP_ALTER,
	['>'] = BF_OP_ALTER,
	[','] = BF_OP_IN,
	['.'] = BF_OP_OUT,
	['['] = BF_OP_LOOP,
};

bf_op* build_bf_tree(char *data, size_t *pos, size_t len, bool expecting_bracket, size_t *out_len) {
	size_t allocated = 16, my_ops_len = 0;
	bf_op *my_ops = malloc(allocated * sizeof *my_ops);

	size_t i = *pos;
	while (i < len) {
		unsigned char c = data[i++];
		switch (c) {
			case '+':
			case '-':
				if (my_ops_len == 0
						|| (my_ops[my_ops_len - 1].op_type != BF_OP_ALTER
							&& my_ops[my_ops_len - 1].op_type != BF_OP_SET)) {
new_alter:
					if (my_ops_len == allocated) {
						allocated *= 2;
						my_ops = realloc(my_ops, allocated * sizeof *my_ops);
					}
					my_ops[my_ops_len++] = (bf_op){
						.op_type = BF_OP_ALTER,
						.offset = 0,
						.amount = 0,
					};
				}
				break;
			case '>':
			case '<':
				if (my_ops_len == 0
						|| my_ops[my_ops_len - 1].op_type != BF_OP_ALTER
						|| my_ops[my_ops_len - 1].amount != 0) {
					goto new_alter;
				}
				break;
			case '.':
			case ',':
			case '[':
				if (my_ops_len == allocated) {
					allocated *= 2;
					my_ops = realloc(my_ops, allocated * sizeof *my_ops);
				}
				my_ops[my_ops_len++] = (bf_op){
					.op_type = op_type_for_char[c],
				};
				break;
		}

		bf_op *op = &my_ops[my_ops_len - 1];
		switch (c) {
			case '+':
				op->amount++;
				break;
			case '-':
				op->amount--;
				break;
			case '>':
				op->offset++;
				break;
			case '<':
				op->offset--;
				break;
			case '[':
				op->child_op = build_bf_tree(data, &i, len, true, &op->child_op_count);
				optimize_loop(op);
				break;
			case ']':
				if (!expecting_bracket) errx(1, "Unexpected end of loop");
				goto end;
		}
	}

end:
	*pos = i;
	*out_len = my_ops_len;
	if (my_ops_len == 0) {
		free(my_ops);
		my_ops = NULL;
	}
	return my_ops;
}

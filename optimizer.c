#include "optimizer.h"

void optimize_loop(bf_op *op) {
	// Shite optimizations
	if (op->child_op_count == 1
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[0].offset == 0
			&& (op->child_op[0].amount == 1
				|| op->child_op[0].amount == (cell_int)-1)) {
		free(op->child_op);
		op->op_type = BF_OP_SET;
		op->amount = 0;
	} else if (op->child_op_count == 2
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[1].op_type == BF_OP_ALTER
			&& op->child_op[0].offset == -op->child_op[1].offset
			&& op->child_op[1].amount == (cell_int)-1) {
		ssize_t offset = op->child_op[0].offset;
		cell_int scalar = op->child_op[0].amount;
		free(op->child_op);
		op->op_type = BF_OP_MULTIPLY;
		op->offset = offset;
		op->amount = scalar;
	} else if (op->child_op_count == 3
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[1].op_type == BF_OP_ALTER
			&& op->child_op[2].op_type == BF_OP_ALTER
			&& op->child_op[0].offset == 0
			&& op->child_op[1].offset == -op->child_op[2].offset
			&& op->child_op[0].amount == (cell_int)-1
			&& op->child_op[2].amount == 0) {
		// TODO this should really be handled by some kind of optimizer which shuffles lonely incs/decs to the right
		ssize_t offset = op->child_op[1].offset;
		cell_int scalar = op->child_op[1].amount;
		free(op->child_op);
		op->op_type = BF_OP_MULTIPLY;
		op->offset = offset;
		op->amount = scalar;
	} else if (op->child_op_count == 1
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[0].amount == 0) {
		ssize_t offset = op->child_op[0].offset;
		free(op->child_op);
		op->op_type = BF_OP_SKIP;
		op->offset = offset;
	}
}

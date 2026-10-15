/*
 * TODO; swap sub around for non direct cases ?
 *	 cleanup x++ and ++x style ops for 6800 in particular
 *		use ldx _foo inx stx _foo for 1-4 byte 16bit
 *		spot the common *x++=   and  blah *x++ cases
 *		and do these for small values directly as
 *		ldx _foo inx stx _foo [dex] stb ,x  etc
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6809.h"

static char workbuf[32];

/*
 *	Helpers for code generation and tracking
 */

/*
 *	Size handling
 */
unsigned get_size(unsigned t)
{
	if (PTR(t))
		return 2;
	if (t == CSHORT || t == USHORT)
		return 2;
	if (t == CCHAR || t == UCHAR)
		return 1;
	if (t == CLONG || t == ULONG || t == FLOAT)
		return 4;
	if (t == CLONGLONG || t == ULONGLONG || t == DOUBLE)
		return 8;
	if (t == VOID)
		return 0;
	error("gs");
	return 0;
}

unsigned get_stack_size(unsigned t)
{
	return get_size(t);
}

void repeated_op(unsigned n, const char *op)
{
	while(n--)
		output("%s", op);
}

/* Chance to rewrite the tree from the top rather than none by node
   upwards. We will use this for 8bit ops at some point and for cconly
   propagation */
struct node *gen_rewrite(struct node *n)
{
	return n;
}

static void squash_node(struct node *n, struct node *o)
{
	n->value = o->value;
	n->snum = o->snum;
	free_node(o);
}

static void squash_left(struct node *n, unsigned op)
{
	struct node *l = n->left;
	n->op = op;
	squash_node(n, l);
	n->left = NULL;
}

static void squash_right(struct node *n, unsigned op)
{
	struct node *r = n->right;
	n->op = op;
	squash_node(n, r);
	n->right = NULL;
}

/*
 *	Stick simple things on the right. Elsewhere the code assumes that
 *	9 or higher score means we can do it without eating X
 */
static unsigned is_simple(struct node *n)
{
	unsigned op = n->op;

	/* We can load these directly */
	if (op == T_CONSTANT)
		return 10;
	/* These are as easy but we also have some helpers that benefit
	   most from right constant */
	if (op == T_NAME && !cpu_pic)
		return 9;
	if (op == T_NREF)
		return 9;
	if (op == T_ARGUMENT || op == T_LOCAL)
		return 9;
	/* Harder as uses X */
	if (op == T_LREF) {
		/* 6809 has ,S ops */
		return 9;
	}
	return 0;
}

static unsigned free_cast(unsigned nt, unsigned rt)
{
	/* Pointers act as ushort */
	if (PTR(nt))
		nt = USHORT;
	if (PTR(rt))
		rt = USHORT;
	/* If the types match except for sign then no work */
	if (nt == rt || (nt ^ rt) == UNSIGNED)
		return 1;
	/* If the types are integer and the new type is "smaller" then
	   no work is done but we need the info from the cast */
	return 0;
}

/* Called on T_CAST for dereference conversion */
static struct node *shrink_load(struct node *n)
{
	register struct node *r = n->right;
	unsigned s = get_size(n->type);
	/* Floats and stuff we don't want to mess with */
	if ((r->type & ~UNSIGNED) != CLONG)
		return n;
	/* Can't shrink stuff where we need all of it */
	if (n->type >= CLONG)
		return n;
	if (r->flags & SIDEEFFECT)	/* Volatile */
		return n;
	/* Big endian sucks */
	/* Turn the reference into the low word directly if we can */
	if(r->op == T_LREF || r->op == T_NREF ||
			r->op == T_DEREF || r->op == T_DEREFPLUS)
		r->value += 4 - s;
	else if (r->op == T_LDEREF)
		r->snum += 4 - s;
	else
		return n;
	/* Now fix type */
	r->type = n->type;
	free_node(n);
	return r;
}

/*
 *	Our chance to do tree rewriting. We don't do much
 *	at this point, but we do rewrite name references and function calls
 *	to make them easier to process.
 */
struct node *gen_rewrite_node(struct node *n)
{
	register struct node *r = n->right;
	register struct node *l = n->left;
	register unsigned op = n->op;
	unsigned nt = n->type;
	unsigned off;

	/* Adjust any argument pushed by the called function to
	   be the other side of the push of pc and maybe of u */
	if (op == T_ARGUMENT && n->value < func_a1size)
		n->value -= argbase;

	/* Eliminate casts for sign, pointer conversion or same */
	if (op == T_CAST) {
		if (cast_fold_safe(r->op)) {
			if (free_cast(nt, r->type)) {
				free_node(n);
				r->type = nt;
				return r;
			}
		}
		return shrink_load(n);
	}
	/* It's easier to deal with the == 0 and != 0 cases via bool */
	/* x == 0 is !x */
	if (op == T_EQEQ && r->op == T_CONSTANT && r->value == 0) {
		squash_node(n, r);
		n->op = T_BANG;
		n->right = l;
		n->left = NULL;
		return n;
	}
	/* x != 0 is bool of x */
	if (op == T_BANGEQ && r->op == T_CONSTANT && r->value == 0) {
		squash_node(n, r);
		n->right = l;
		n->left = NULL;
		n->op = T_BOOL;
		return n;
	}

	/* Rewrite function call of a name into a new node so we can
	   turn it easily into call xyz */
	if (op == T_FUNCCALL && r->op == T_NAME && PTR(r->type) == 1) {
		n->op = T_CALLNAME;
		/* Save old vararg flag, func offsets are always 0 */
		n->value = n->snum;
		n->snum = r->snum;
		free_node(r);
		n->right = NULL;
	}
	/* Merge offset to object into a single direct reference */
	if (op == T_PLUS && r->op == T_CONSTANT &&
		(l->op == T_LOCAL || l->op == T_NAME || l->op == T_ARGUMENT)) {
		/* We don't care if the right offset is 16bit or 32 as we've
		   got 16bit pointers */
		/* printf(";merge right %x %lu+%lu\n",
			op, l->value, r->value); */
		l->value += r->value;
		free_node(r);
		free_node(n);
		return l;
	}
	/* Turn a deref of an offset to an object into a single op so we can
	   generate a single lda offset,x in the code generator. This happens
	   in some array dereferencing and especially struct pointer access */
	if (op == T_DEREF || op == T_DEREFPLUS) {
		if (op == T_DEREF)
			n->value = 0;	/* So we can treat deref/derefplus together */
		if (r->op == T_PLUS) {
			off = n->value + r->right->value;
			if (r->right->op == T_CONSTANT && off < 253) {
				n->op = T_DEREFPLUS;
				free_node(r->right);
				n->right = r->left;
				n->value = off;
				free_node(r);
				/* We might then rewrite this again */
				return gen_rewrite_node(n);
			}
		}
	}
	if (op == T_EQ || op == T_EQPLUS) {
		if (op == T_EQ)
			n->value = 0;	/* So we can treat deref/derefplus together */
		if (l->op == T_PLUS) {
			off = n->value + l->right->value;
			if (l->right->op == T_CONSTANT && off < 253) {
				n->op = T_EQPLUS;
				free_node(l->right);
				n->left = l->left;
				n->value = off;
				free_node(l);
				/* We might then rewrite this again */
				return gen_rewrite_node(n);
			}
		}
	}
	/* regptr++  The size will always be the true size for ++ and const */
	if (op == T_DEREF && r->op == T_PLUSPLUS && r->left->op == T_REG &&
		r->right->value == get_size(n->type))
	{
		n->op = T_RDEREFPLUS;
		n->value = r->right->value;
		free_node(r->left);
		free_node(r->right);
		free_node(r);
		n->right = NULL;
		return n;
	}
	/* *regptr++ =  again the size will be const and right */
	/* Must only do this when the size matches the add (could in theory
	   also do stb ,u++ ?? */
	if (op == T_EQ && l->op == T_PLUSPLUS && l->left->op == T_REG &&
		l->left->value == get_size(n->type))
	{
		n->op = T_REQPLUS;
		n->value = l->left->value;
		free_node(l->left);
		free_node(l->right);
		free_node(l);
		n->left = NULL;
		return n;
	}
	/* *(reg + offset). Optimize this specially as it occurs a lot */
	if (op == T_DEREF && r->op == T_PLUS && r->left->op == T_RREF &&
		r->right->op == T_CONSTANT) {
		n->op = T_RDEREF;
		n->right = NULL;
		n->snum = r->right->value;	/* Offset to add */
		n->value = r->left->value;	/* Register number */
		free_node(r->right);		/* Discard constant */
		free_node(r->left);		/* Discard T_REG */
		free_node(r);			/* Discsrd plus */
		return n;
	}
	/* *regptr */
	if (op == T_DEREF && r->op == T_RREF) {
		n->op = T_RDEREF;
		n->right = NULL;
		n->snum = 0;
		n->value = r->value;
		free_node(r);
		return n;
	}
	/* *(reg + offset) =  Optimize this specially as it occurs a lot */
	if ((op == T_EQ || op == T_EQPLUS) && l->op == T_PLUS && l->left->op == T_RREF &&
		l->right->op == T_CONSTANT) {
		n->op = T_REQ;
		/* Offset of EQPLUS in n->value, plus right side */
		n->snum = n->value + l->right->value;	/* Offset to add */
		n->value = l->left->value;	/* Register number */
		free_node(l->right);		/* Discard constant */
		free_node(l->left);		/* Discard T_REG */
		free_node(l);			/* Discsrd plus */
		n->left = NULL;
		return n;
	}
	/* *regptr = */
	if ((op == T_EQ || op == T_EQPLUS) && l->op == T_RREF) {
		n->op = T_REQ;
		n->snum = n->value;	/* Copy over the addition */
		n->value = l->value;	/* Set the register */
		n->left = NULL;
		free_node(l);
		return n;
	}
	if ((op == T_DEREF || op == T_DEREFPLUS) && r->op == T_LREF) {
		/* At this point r->value is the offset for the local */
		/* n->value is the offset for the ptr load */
		r->snum = n->value;		/* Save the offset so it is squashed in */
		squash_right(n, T_LDEREF);	/* n->value becomes the local ref */
		return n;
	}
	if ((op == T_EQ || op == T_EQPLUS) && l->op == T_LREF) {
		/* At this point r->value is the offset for the local */
		/* n->value is the offset for the ptr load */
		l->snum = n->value;		/* Save the offset so it is squashed in */
		squash_left(n, T_LEQ);		/* n->value becomes the local ref */
		return n;
	}
	/* Optimizations for 6809 indirect addressing */
	if (n->value == 0 && (op == T_DEREF || op == T_DEREFPLUS) && r->op == T_NREF) {
		/* At this point r->value is the offset for the local */
		/* n->value is the offset for the ptr load */
		squash_right(n, T_NDEREF);	/* n->value becomes the local ref */
		return n;
	}
	/* This is trickier with 32bit values so defer that for now */
	if (nt < CLONG && n->value == 0 && (op == T_EQ || op == T_EQPLUS) && l->op == T_NREF) {
		squash_left(n, T_NEQ);		/* n->value becomes the local ref */
		return n;
	}

	/* Rewrite references into a load operation */
	if (op == T_DEREF) {
		if (r->op == T_LOCAL || r->op == T_ARGUMENT) {
			if (r->op == T_ARGUMENT)
				r->value += argbase + frame_len;
			squash_right(n, T_LREF);
			return n;
		}
		if (r->op == T_REG) {
			/* Save the register number in the squash */
			r->snum = r->value;
			squash_right(n, T_RREF);
			n->value = 0;
			return n;
		}
		if (r->op == T_NAME) {
			squash_right(n, T_NREF);
			return n;
		}
		if (r->op == T_RREF) {
			squash_right(n, T_RDEREF);
			n->snum = 0;
			return n;
		}
	}
	if (op == T_EQ) {
		if (l->op == T_NAME) {
			squash_left(n, T_NSTORE);
			return n;
		}
		if (l->op == T_LOCAL || l->op == T_ARGUMENT) {
			if (l->op == T_ARGUMENT)
				l->value += argbase + frame_len;
			squash_left(n, T_LSTORE);
			return n;
		}
		if (l->op == T_REG) {
			l->snum = l->value;
			n->value = 0;
			squash_left(n, T_RSTORE);
			return n;
		}
	}
	/* Commutive operations. We can swap the sides over on these */
	/* We want to put things we can easily use on the right so that
	   we have the best chance of being able to do operations without
	   having to stack values */
	if (op == T_AND || op == T_OR || op == T_HAT || op == T_STAR || op == T_PLUS) {
		if (is_simple(n->left) > is_simple(n->right)) {
			n->right = l;
			n->left = r;
		}
	}
	return n;
}

/* Negate the 16bit working value */
static void negate_d(void)
{
	unsigned v;
	output("nega");
	output("negb");
	output("sbca #0");
	/* TODO: make this a modify_d */
	if (a_valid && b_valid) {
		v = (a_val << 8) + b_val;
		v = WORD(-v);
		modify_a(v >> 8);
		modify_b(v);
	} else
		invalidate_work();
}

/*
 *	If possible turn this node into a direct access. We've already checked
 *	that the right hand side is suitable. If this returns 0 it will instead
 *	fall back to doing it stack based.
 */
unsigned gen_direct(struct node *n)
{
	unsigned s = get_size(n->type);
	struct node *r = n->right;
	unsigned off;
	uint16_t v;
	uint16_t hv;

	switch(n->op) {
	/* Clean up is special and must be handled directly. It also has the
	   type of the function return so don't use that for the cleanup value
	   in n->right */
	case T_CLEANUP:
		gen_cleanup(n);
		return 1;
	case T_EQ:
	case T_EQPLUS:
		off = n->value;
		v = r->value;
		/* If the left side was not simple then try and shortcut the
		   right side */
		if (r->op == T_CONSTANT) {
			swap_d_x();
			if (s == 1)
				load_b_const(v);
			else if (s == 2)
				load_d_const(v);
			 else if (s == 4) {
				load_d_const(v);
				load_y_const(r->value >> 16);
			}
			store_via_x(n, off);
			return 1;
		}
		break;
	case T_PLUS:
		if (r->op == T_CONSTANT && r->type != FLOAT) {
			if (s == 4)
				return gen_add32(r->value);
			if (s == 2) {
				add_d_const(r->value);
				return 1;
			}
			if (s == 1) {
				add_b_const(r->value);
				return 1;
			}
		}
		/* FIXME: ARGUMENT also */
		if (s == 2 && r->op == T_LOCAL) {
			v = r->value + sp;
			/* D holds a constant, want to add sp + v */
			output("Xleax %u,s", v);
			output("pshs x");
			output("DZaddd ,s++");
			return 1;
		}
		return write_opd(r, "add", 0);
	case T_MINUS:
		/* FIXME: can just flip top bit and add for float but
		   not useful ? */
		if (r->op == T_CONSTANT && r->type != FLOAT) {
			if (s == 4)
				return gen_add32(-r->value);
			if (s == 2) {
				add_d_const(-r->value);
				set_z_flag(0);
				return 1;
			}
			if (s == 1) {
				add_b_const(-r->value);
				b_val -= r->value;
				set_z_flag(0);
				return 1;
			}
		}
		return write_opd(r, "sub", 0);
	case T_AND:
		if (r->op == T_CONSTANT) {	/* No need to type check - canno tbe float */
			v = WORD(r->value);
			hv = r->value >> 16;

			/* Check if it makes sense to do long inline. Only
			   do so if we have Y or the upper half is trivial */
			if (BYTE(v) != 0xFF) {
				if (BYTE(v))
					output("andb #%u", BYTE(v));
				else
					load_b_const(0);
				modify_b(b_val & v);
			}
			if (s >= 2) {
				v >>= 8;
				if (v != 0xFF) {
					if (v)
						output("anda #%u", v);
					else
						load_a_const(0);
				}
				modify_a(a_val & v);
			} else
				set_z_flag(0);
			if (s == 4) {
				if (hv == 0x0000)
					load_y_const(0);
				else if (hv != 0xFFFF) {
					swap_d_y();
					if (hv & 0xFF)
						output("Bandb #%u", BYTE(hv));
					else
						load_b_const(0);
					hv >>= 8;
					if (hv & 0xFF)
						output("Aanda #%u", hv & 0xFF);
					else
						load_a_const(0);
					swap_d_y();
				}
			}
			return 1;
		}
		return write_op(r, "and", "and", 0);
	case T_OR:
		if (r->op == T_CONSTANT) {
			v = WORD(r->value);
			hv = r->value >> 16;
			/* Check if it makes sense to do long inline. Only
			   do so if we have Y or the upper half is trivial */
			if (BYTE(v)) {
				output("orb #%u", BYTE(v));
				modify_b(b_val | v);
			}
			if (s >= 2) {
				v >>= 8;
				if (v)
					output("ora #%u", v);
				modify_a(a_val | v);
			} else
				set_z_flag(0);

			if (s == 4) {
				if (hv == 0xFFFF)
					load_y_const(0xFFFF);
				else if (hv) {
					swap_d_y();
					if (hv & 0xFF)
						output("Borb #%u", BYTE(hv));
					hv >>= 8;
					if (hv & 0xFF)
						output("Aora #%u", BYTE(hv));
					swap_d_y();
				}
			}
			return 1;
		}
		return write_op(r, "or", "or", 0);
	case T_HAT:
		if (r->op == T_CONSTANT) {
			v = WORD(r->value);
			hv = r->value >> 16;
			if (BYTE(v) == 0xFF)
				output("comb");
			else if (BYTE(v))
				output("eorb #%u", v & 0xFF);
			modify_b(b_val ^ v);
			if (s >= 2) {
				v >>= 8;
				if (BYTE(v) == 0xFF)
					output("coma");
				else if (BYTE(v))
					output("eora #%u", v);
				modify_a(a_val ^ v);
			} else
				set_z_flag(0);
			if (s == 4) {
				if (hv) {
					swap_d_y();
					if (BYTE(hv) == 0xFF)
						output("Bcomb");
					else if (hv & 0xFF)
						output("Beorb #%u", hv & 0xFF);
					hv >>= 8;
					if (BYTE(hv) == 0xFF)
						output("Acoma");
					else if (BYTE(hv))
						output("Aeora #%u", hv);
					swap_d_y();
				}
			}
			return 1;
		}
		return write_op(r, "eor", "eor", 0);
	case T_LTLT:
		return left_shift(n);
	case T_GTGT:
		return right_shift(n);
	case T_EQEQ:
		return cmp_direct(n, "booleq", "booleq", 1);
	case T_BANGEQ:
		return cmp_direct(n, "boolne", "boolne", 2);
	case T_LT:
		return cmp_direct(n, "boolult", "boollt", 0);
	case T_GT:
		return cmp_direct(n, "boolugt", "boolgt", 0);
	case T_LTEQ:
		return cmp_direct(n, "boolule", "boolle", 0);
	case T_GTEQ:
		return cmp_direct(n, "booluge", "boolge", 0);
	case T_SLASH:
		if (r->op == T_CONSTANT) {
			if (s <= 2 && gen_fast_div(s, r->value, n->type))
				return 1;
		}
		break;
	case T_STAR:
		if (r->op == T_CONSTANT) {
			v = r->value;
			if (s <= 2)
				return gen_fast_mul(s, v);
		}
		break;
	}
	return 0;
}

/*
 *	Allow the code generator to shortcut the generation of the argument
 *	of a single argument operator (for example to shortcut constant cases
 *	or simple name loads that can be done better directly)
 */
unsigned gen_uni_direct(struct node *n)
{
	unsigned s = get_size(n->type);
	struct node *r = n->right;
	unsigned nr = n->flags & NORETURN;

	switch(n->op) {
	case T_LEQ:
		/* We have a specific optimization case that occurs a lot
		   *auto = 0, that we can optimize nicely */
		/* printf(";LEQ rv %lu nr %u nv %lu snum %u s %u\n",
			r->value, nr, n->value, n->snum, s); */
		if (r->op == T_CONSTANT && r->value == 0 && nr) {
			if (n->snum == 0 && s == 1) {
				/* n->value is the frame offset of the pointer
				   local; correct for anything pushed since */
				output("Mclr [%u,s]", WORD(n->value + sp));
				return 1;
			}
		}
		return 0;
	case T_NEQ:
		if (r->op == T_CONSTANT && r->value == 0 && nr) {
			if (s == 1 && nr) {
				output("Mclr [%s+%u]", namestr(n->snum), (unsigned)n->value);
				return 1;
			}
		}
		return 0;
	/* Writes of 0 to an object we can use clr for providing the
	   result is not then re-used */
	case T_LSTORE:
	case T_NSTORE:
		if (r->op == T_CONSTANT && d_type_constant() &&
					d_const_value() == r->value)
			if (write_opd(n, "Mst", 0)) {
				set_ds_node(n);
				return 1;
			}
		/* Optimizations for constants */
		if (nr && r->op == T_CONSTANT && r->value == 0) {
			if (write_uni_op(n, "Mclr", 0))
				return 1;
		}
		return 0;
	}
	return 0;
}

/*
 *	Try and build an op where we load X with the pointer,
 *	AB with the data and call a helper. Some of these may also
 *	benefit from inline forms later. 32bit also works as the
 *	value ends up in Y/AB which is all safe from the load of
 *	the X pointer.
 */

unsigned do_xptrop(struct node *n, const char *op, unsigned off)
{
	unsigned size = get_size(n->type);
	if (size == 4 && optsize) {
		add_x_const(off);
		helper_s(n, op);
		return 1;
	}
	switch(n->op) {
	case T_ANDEQ:
		op_on_ptr(n, "and", off);
		break;
	case T_OREQ:
		op_on_ptr(n, "or", off);
		break;
	case T_HATEQ:
		op_on_ptr(n, "eor", off);
		break;
	case T_PLUSEQ:
		opd_on_ptr(n, "add", "adc", off);
		break;
	case T_MINUSEQ:
		/* We want to subtract D *from* .X. Negate D and add
		   as this seems the cheapest approach */
		if (size == 1) {
			output("Bnegb");
			opd_on_ptr(n, "Zadd", "adc", off);
			break;
		}
		if (size == 2) {
			negate_d();
			opd_on_ptr(n, "Zadd", "adc", off);
			break;
		} else
			helper(n, "negate");
		opd_on_ptr(n, "DZadd", "adc", off);
		break;
	case T_STAREQ:
		/* If we have D we have mul */
		size = get_size(n->type);
		if (size == 1) {
			output("Alda %u,x", off);
			output("ABmul");
			output("MZstb %u,x", off);
			set_ds_node_ptr(n->left);
			return 1;
		}
		/* Fall through */
	/* TODO:
	   MINUSEQ
	   PLUSPLUS
	   MINUSMINUS
	   shift prob not */
	default:
		add_x_const(off);
		helper_s(n, op);
		return 1;
	}
	/* Stuff D back into ,X */
	store_via_x(n, off);
	if (size <= 2)
		set_z_flag(0);
	set_ds_node_ptr(n->left);
	return 1;
}

static unsigned deref_op(unsigned op)
{
	switch(op) {
	case T_NAME:
		return T_NREF;
	case T_LOCAL:
		return T_LREF;
	}
	return 0;
}

unsigned write_xsimple(struct node *n, unsigned via_ptr)
{
	const char *op, *op2;
	unsigned off = 0;
	unsigned op16 = 0;
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned top = n->op;

	switch(n->op) {
	case T_ANDEQ:
		op = op2 = "and";
		break;
	case T_OREQ:
		op = op2 = "or";
		break;
	case T_HATEQ:
		op = op2 = "eor";
		break;
	case T_PLUSEQ:
		op = "add";
		op2 = "adc";
		op16 = 1;
		break;
	case T_MINUSEQ:
		op = "sub";
		op2 = "sbc";
		op16 = 1;
		break;
	default:
		return 0;
	}
	top = deref_op(l->op);
	if (via_ptr || top == 0) {
		off = load_x_with(l);
		load_via_x(n, off);
		via_ptr = 1;
	} else {
		/* Not sure we should encourage this kind of behaviour ;) */
		/* TODO: turn this simple/simple stuff into a tree rewrite */
		l->op = top;
		l->type = r->type;
		write_opd(l, "ld", off);
	}
	if (op16)
		write_opd(r, op, 0);
	else
		write_op(r, op, op2, 0);

	if (via_ptr)
		store_via_x(n, off);
	else
		write_opd(l, "st", off);
	set_d_node_ptr(l);
	return 1;
}

/* TODO: 6809 could index locals via S so if n->left is LOCAL or
   ARGUMENT we can special case it */
unsigned do_xeqop(struct node *n, const char *op)
{
	unsigned off;
	struct node *l = n->left;
	struct node *r = n->right;

	/* Float always goes via the helper */
	if (n->type == FLOAT)
		return 0;

	/* Handle simpler cases of -= the other way around */
	/* Avoid cases that also need X */
	if (is_simple(r) > 8 && get_size(n->type) <= 2) {
		if (is_simple(l)) {
			if (write_xsimple(n, 0))
				return 1;
		} else if (can_load_r_with(l)) {
			if (write_xsimple(n, 1))
				return 1;
		}
	}
	if (!can_load_r_with(l)) {
		/* Compute the left side and stack it */
		codegen_lr(l);
		gen_push(l);
		/* Get D right, then pull the pointer into X */
		codegen_lr(r);
		pop_x();
		sp -= 2;	/* Pulling the pointer back off */
		off = 0;
		/* and drop into he helper */
	} else {
		/* Get the value part into AB */
		codegen_lr(n->right);
		/* Load X (lval of the eq op) up (doesn't disturb AB) */
		off = load_x_with(l);
	}
	/* Things we can then inline */
	if (do_xptrop(n, op, off) == 0)
		error("xptrop");
	set_d_node_ptr(l);
	return 1;
}

/* TODO: move to per CPU file */
unsigned do_stkeqop(struct node *n, const char *op)
{
	if (n->type == FLOAT)
		return 0;
	output_nf("Xpuls x");
	return do_xptrop(n, op, 0);
}

/*
 *	Things we can try and do as a direct memory op for bytes
 */

static void memop_reg(char reg, const char *op, unsigned off, struct node *l, unsigned pre, unsigned ct)
{
	if (pre == 1)
		output("Bldb %u,%c", off, reg);
	sprintf(workbuf, "BM%s %u,%c", op, off, reg);
	repeated_op(ct, workbuf);
	if (pre == 2) {
		output("BZldb %u,%c", off, reg);
		set_ds_node_ptr(l);
	}
}

unsigned memop_const(struct node *n, const char *op, unsigned nr, unsigned pre)
{
	register struct node *l = n->left;
	unsigned v = l->value;
	struct node *r = n->right;
	unsigned ct = r->value;

	if (r->op != T_CONSTANT)
		return 0;

	if (nr)
		pre = 0;

	if (ct > 2 + nr)
		return 0;

	switch(l->op) {
	case T_NAME:
		if (pre == 1)
			output("Bldb %s+%u%s\n", namestr(l->snum), v, pic_op);
		sprintf(workbuf, "BM%s %s+%u%s", op, namestr(l->snum), v, pic_op);
		repeated_op(ct, workbuf);
		if (pre == 2) {
			output("BZldb %s+%u%s", namestr(l->snum), v, pic_op);
			set_ds_node_ptr(l);
		}
		if (pre)
			invalidate_b();
		return 1;
	case T_ARGUMENT:
		v += argbase + frame_len;
	case T_LOCAL:
		v += sp;
		memop_reg('s', op, v, l, pre, ct);
		return 1;
	case T_RREF:
		memop_reg('u', op, v, l, pre, ct);
		return 1;
	}
	if (can_load_r_with(l)) {
		/* OK so via X */
		v += load_x_with(l);
		memop_reg('x', op, v, l, pre, ct);
		return 1;
	}
	return 0;
}

unsigned memop_shift(struct node *n, const char *op, const char *opu)
{
	unsigned v;
	unsigned rv;
	struct node *l = n->left;
	struct node *r = n->right;
	v = l->value;
	rv = r->value;
	if (r->op != T_CONSTANT)
		return 0;

	/* Right shifts are sign specific */
	if (n->type & UNSIGNED) {
		op = opu;
		if (rv > 7) {
			rv = 1;
			op = "clr";
		}
	}
	if (rv > 2 && opt < 2)
		return 0;
	switch(l->op) {
	case T_NAME:
		sprintf(workbuf, "M%s %s+%u%s", op, namestr(l->snum), v, pic_op);
		repeated_op(rv, workbuf);
		return 1;
	case T_ARGUMENT:
		v += argbase + frame_len;
	case T_LOCAL:
		sprintf(workbuf, "M%s %u,s", op, v + sp);
		repeated_op(rv, workbuf);
		return 1;
	case T_RREF:
		sprintf(workbuf, "M%s %u,u", op, v + sp);
		repeated_op(rv, workbuf);
		return 1;
	}
	return 0;
}

/* Generate inline code for ++ and -- operators when it makes sense */
unsigned add_to_node(struct node *n, int sign, int retres)
{
	struct node *r = n->right;
	struct node *l = n->left;
	unsigned v = l->value;
	unsigned s = get_size(n->type);
	unsigned off;

	if (s > 2 || r->op != T_CONSTANT)
		return 0;
	if (l->op == T_ARGUMENT || l->op == T_LOCAL) {
		if (l->op == T_ARGUMENT)
			v += argbase + frame_len;
		v += sp;
		/* FIXME: should check if d contains the wanted pointed value */
		load_d_const(sign * r->value);
		output("Daddd %u,s", v);
		output("Mstd %u,s", v);
		if (retres)
			set_ds_node_ptr(r);
		else
			add_d_const(-sign * r->value);
		return 1;
	}
	/* It's marginal whether we should go via X or not */
	if (!can_load_r_with(l))
		return 0;
	off = load_x_with(l);
	if (s == 1) {
		op8_on_ptr("Bld", off);
		/* Need to clean up the B stuff */
		if (!retres)
			output_nf("pshs b");
		add_b_const(sign * r->value);
		op8_on_ptr("MZst", off);
		if (!retres)
			output("puls b");
		else
			set_ds_node_ptr(l);
		return 1;
	}
	op16d_on_ptr("Dld", off);
	if (!retres) {
		push_d();
		sp += 2;
	}
	add_d_const(sign * r->value);
	op16d_on_ptr("DMst", off);
	if (!retres) {
		sp -= 2;
		pop_d();
	} else
		set_ds_node_ptr(l);
	return 1;
}

static unsigned is_ccdown(struct node *n)
{
	register unsigned op = n->op;
	if (op == T_ANDAND || op == T_OROR)
		return 1;
	if (op == T_BOOL)
		return 1;
	if (op == T_BANG && !(n->flags & CCFIXED))
		return 1;
	return 0;
}

/* Operators that we known to handle as CCONLY if possible */
static unsigned is_cconly(struct node *n)
{
	register unsigned op = n->op;
	if (op == T_ANDAND || op == T_OROR || op == T_BOOL)
		return 1;
	/* integer and byte sized compares via flags */
	if ((op == T_EQEQ || op == T_BANGEQ) && n->right->type < CLONG)
		return 1;
	/* Inverting flags has limits */
	if (op == T_BANG && !(n->flags & CCFIXED))
		return 1;
	return 0;
}

/*
 *	Try and push CCONLY down through the tree
 */
static void propogate_cconly(register struct node *n)
{
	register struct node *l, *r;

	/* Only do this for nodes that are CCONLY. For example if we hit
	   an EQ (assign) then whilst the result of the assign may be
	   CC only, the subtree of the assignment is most definitely not */
	if (!is_cconly(n) && !(n->flags & CCONLY))
		return;

	l = n->left;
	r = n->right;

	n->flags |= CCONLY;
	/* Deal with the CCFIXED limitations for now */
	if (n->flags & CCFIXED) {
		if (l)
			l->flags |= CCFIXED;
		if (r)
			r->flags |= CCFIXED;
	}
	// printf(";made cconly %x %04X\n", n->op, n->flags);
	/* Are we a node that can CCONLY downwards */
	if (is_ccdown(n)) {
		// printf(";ccdown of %x L\n", n->op);
		if (l)
			propogate_cconly(l);
		// printf(";ccdown cont %x R\n", n->op);
		if (r)
			propogate_cconly(r);
		// printf(";ccdown done %x\n", n->op);
	}
}

/* Turn something boolean for flags */

static void boolify_cc(struct node *n, unsigned s)
{
	if (s > 2) {
		helper(n, "bool");
		set_a(0);
		n->flags |= ISBOOL;
	} else if (s == 2)
		output("DZsubd #0");
	else
		output("Ztstb");
	n->flags |= USECC;
	if (n->right->flags & ISBOOL) {
		n->flags |= ISBOOL;
		set_a(0);
	}
	set_z_flag(0);
}

/*
 *	Allow the code generator to shortcut trees it knows
 */
unsigned gen_shortcut(struct node *n)
{
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned s = get_size(n->type);
	unsigned nr = n->flags & NORETURN;
	unsigned v;

	/* Don't generate unreachable code */
	if (unreachable)
		return 1;

	/* Handle function call argument subtrees specially, mostly for
	   6809 although we have some opportunities to use ldx/pshx on 6303/803 */

	/* We should never meet an ARGCOMMA as they are handled by
	   gen_fcall */
	if (n->op == T_ARGCOMMA)
		error("a,");

	/* Walk the subtree if we are condition code dependent and try and
	 * propogate that as far into the tree as we can */
	if (n->flags & CCONLY)
		propogate_cconly(n);

	/* Function calls we want to parse the argument subtree ourselves
	   to optimise push handling */
	if (n->op == T_FUNCCALL) {
		/* Generate and stack all the arguments */
		gen_fcall(l, n->snum);
		/* Generate the address of the function */
		if (can_load_r_with(r))
			v = load_x_with(r);
		else {
			sp += 2;
			if (!(cpufeat & 4))
				push_d();
			codegen_lr(r);
			v = 0;
			make_x_d();
			if (!(cpufeat & 4))
				pop_d();
			sp -= 2;
		}
		if (cpufeat & 4)	/* banked */
			push_x();
		output("jsr %d,x", v);
		if (cpufeat & 4)	/* banked */
			pop_x();
		invalidate_all();
		trackf = NULL;
		return 1;
	}

	if (n->op == T_CALLNAME) {
		gen_fcall(l, n->value);
		if (cpufeat & 4)	/* banked */
			push_x();
		output("%s %s", jsr_op, namestr(n->snum));
		if (cpufeat & 4)
			pop_x();
		invalidate_all();
		trackf = NULL;
		return 1;
	}

	/* Handle operations that are of the form (OP (REG) (thing)) as we can't really
	   talk about 'address' of a register variable for 6809 */
	if (l && l->op == T_REG) {
		v = r->value;
		switch(n->op) {
		case T_PLUSPLUS:
			if (!nr)
				tfr_u_d();
			/* add_u_const ?? TODO */
			output_nf("Uleau %u,u", v);
			return 1;
		case T_PLUSEQ:
			if (r->op == T_CONSTANT)
				output_nf("leau %u,u", v);
			else {
				codegen_lr(r);
				output_nf("leau d,u");
			}
			if (!nr)
				tfr_u_d();
			return 1;
		case T_MINUSMINUS:
			if (!nr)
				tfr_u_d();
			output_nf("leau -%u,u", v);
			return 1;
		case T_MINUSEQ:
			if (r->op == T_CONSTANT)
				output_nf("leau -%u,u", v);
			else {
				codegen_lr(r);
				negate_d();
				output_nf("leau d,u");
			}
			if (!nr)
				tfr_u_d();
			return 1;
		case T_STAREQ:
			/* TODO: fast mul forms */
			codegen_lr(r);
			helper(n, "regmul");
			return 1;
		case T_SLASHEQ:
			/* TODO: short forms */
			codegen_lr(r);
			helper_s(n, "regdiv");
			return 1;
		case T_PERCENTEQ:
			codegen_lr(r);
			helper_s(n, "regmod");
			return 1;
		case T_SHLEQ:
			codegen_lr(r);
			helper(n, "reglsl");
			return 1;
		case T_SHREQ:
			codegen_lr(r);
			helper_s(n, "regshr");
			return 1;
		/* These don't trash other regs about */
		case T_ANDEQ:
			codegen_lr(r);
			output("D%s __regand", jsr_op);
			return 1;
		case T_OREQ:
			codegen_lr(r);
			output("D%s __regor", jsr_op);
			return 1;
		case T_HATEQ:
			codegen_lr(r);
			output("D%s __regxor", jsr_op);
			return 1;
		}
	}

	switch(n->op) {
	case T_COMMA:
		/* The comma operator discards the result of the left side,
		   then evaluates the right. Avoid pushing/popping and
		   generating stuff that is surplus */
		l->flags |= NORETURN;
		codegen_lr(l);
		/* Parent determines child node requirements */
		r->flags |= nr;
		codegen_lr(r);
		return 1;
	case T_BOOL:
		/* Logic via jumping always uses CC in our case */
		if (r->op == T_ANDAND || r->op == T_OROR)
			r->flags |= USECC;
		/* We cannot tell in advance what we can use flags on to
		   shortcut a boolean operator because it's down to what
		   happens to end up in the flags registers. Instead we
		   generate the subtree and take a look */
		codegen_lr(r);
//		output_nf(";bool");
		/* Constant result ?  BOOL wants "r != 0".  Only test the
		   working register if the child did not already produce
		   condition codes: in that case the register may not hold the
		   child's value any more. */
		if (!(r->flags & USECC) && cmp_known(n, get_size(r->type), 2, 0))
			return 1;
		/* The thing to bool is USECC but we are not. This should not happen */
		if (!(n->flags & CCONLY) && (r->flags & USECC) && !(r->flags & ISBOOL)) {
			output_nf(";BOTCH %04X:%04X", r->op, r->flags);
			output("%s __cctobool", jsr_op);
			set_a(0);
			set_z_flag(0);
			n->flags |= ISBOOL | USECC;
			return 1;
		}
		/* If the previous operator was boolean but did not use
		   CC flags then it's required to have set them for BEQ/BNE
		   as usual. If it was USECC then don't touch as they
		   may have been inverted. Should never happen. */
		if ((r->flags & ISBOOL) && !(r->flags & USECC)) {
			set_z_flag(0);
			r->flags |= USECC;
		}
		/* Opportunistic flag case */
		/* In the odd cases that the Z flag is in passing set the way
		   we need then treat it as if we had planned the event */
		if (!(r->flags & USECC) && trackf == stdflags) {
			/* TODO: review || cc_invert and ditto on tidying BANGEQ */
			if (!(n->flags & CCFIXED) || cc_invert)
				r->flags |= USECC;
		}
		/* We want condition codes, we currently have the flag
		   correct so do nothing but mark us as USECC in turn */
		if ((n->flags & CCONLY) && (r->flags & USECC)) {
			n->flags |= USECC;
			/* Inherit the bool flag as well as USECC */
			if (r->flags & ISBOOL)
				n->flags |= ISBOOL;
			return 1;
		}
		/* Do the actual conversion ourselves */
		s = get_size(r->type);
		/* If we can do a quick conversion from the bool test to
		   Z flag do so, but remember this is reversed so we can't
		   play games if CCFIXED is set */
		if ((n->flags & CCONLY) && !(n->flags & CCFIXED))
			boolify_cc(n, s);
		else {
			/* Either we need the value or it's harder */
			/* Complex cases - long and float go to helpers */
			if (!(r->flags & ISBOOL)) {
				helper(n, "bool");
				/* Helper made the output bool and set
				   the flags */
				n->flags |= USECC;
				set_a(0);
			}
			/* Otherwise just inherit the ISBOOLness and thus flags */
			n->flags |= ISBOOL | USECC;
			set_z_flag(0);
		}
		return 1;
	case T_BANG:
		/* This is actually a bit easier than BOOL because the
		   Z flag reflects the truth as expected for other cases.
		   So n == 0, sets Z and is thus true */
		/* Logic via jumping always uses CC in our case */
		if (r->op == T_ANDAND || r->op == T_OROR)
			r->flags |= USECC;
		/* Same as with BOOL */
		codegen_lr(r);
//		output_nf(";bang");
		/* Constant result ?  BANG wants "r == 0".  As with BOOL only
		   look at the working register if the child has not already
		   set the condition codes. */
		if (!(r->flags & USECC) && cmp_known(n, get_size(r->type), 1, 0)) {
			output_nf(";known");
			return 1;
		}
		/* Flags hold the state already and we can use condition
		   codes. This means we can probably invert the flag logic
		   but there are fringe cases that would need us to rework &&
		   and || in backend.c */
		if ((n->flags & CCONLY) && (r->flags & USECC)) {
			/* Can't just flip flag */
			if (n->flags & CCFIXED) {
				if (r->flags & ISBOOL)
					output("Beorb #1");
				else {
					output("%s __cctonot", jsr_op);
					set_a(0);
				}
				n->flags |= ISBOOL;
				return 1;
			}
			/* Easy usual case */
			cc_invert ^= 1;
			n->flags |= USECC;
			return 1;
		}
		/* Opportunistic flag case */
		if (trackf == stdflags && !cc_invert)
			r->flags |= USECC;
		/* Code that follows requires the actual value of the bool */
		if (!(n->flags & CCONLY) || (n->flags & CCFIXED)) {
//			output_nf(";ccnot2 nf:%04X ro:%04X rf:%04X\n",
//				n->flags, r->op, r->flags);
			/* Parent requires a bool value */
			if ((r->flags & USECC) && (r->flags & CCONLY) && !(r->flags & ISBOOL)) {
				output("%s __cctonot", jsr_op);
				n->flags |= USECC | ISBOOL;
			} else if (r->flags & ISBOOL)
				output("BZeorb #1");
			else {
				helper(n, "not");
				n->flags |= USECC | ISBOOL;
				set_z_flag(0);
			}
		} else {
			if (r->flags & USECC) {
				/* Do we need to check anything here */
				cc_invert ^= 1;
			} else {
				boolify_cc(n, s);
				/* Z is now the inverted test we need */
				cc_invert = 1;
			}
			n->flags |= USECC;
		}
		return 1;

	case T_DEREF:
	case T_DEREFPLUS:
		/* Our right hand side is the thing to deref. See if we can
		   get it into X instead */
		v = n->value;
		if (r->op == T_RREF) {
			/* Want to use U here */
			switch(s) {
			case 1:
				output("ZBldb %u,u", v);
				break;
			case 2:
				output("ZDldd %u,u", v);
				break;
			case 4:
				output("Yldy %u,u", v);
				output("Dldd %u,u", v + 2);
				break;
			default:
				error("d+");
			}
			return 1;
		}
		if (can_load_r_simple(r)) {
			v += load_x_with(r);
			invalidate_work();
			load_via_x(n, v);
			/* FIXME: set_d_node_ptr(r); */
			return 1;
		}
		return 0;
	case T_EQ:	/* Our left is the address */
	case T_EQPLUS:
		/* FIXME: optimise RREF on left as stores via ,u */
		v = n->value;
		if (can_load_r_simple(l)) {
			/* FIXME: !optsize ? */
			if (r->op == T_CONSTANT && nr && s == 4) {
				if (r->value == 0) {
					/* We can optimize thing = 0 for the case
					   we don't also need the value */
					v += load_x_with(l);
					uniop_on_ptr("Mclr", v, s);
					return 1;
				}
			}
			codegen_lr(r);
			v += load_x_with(l);
			store_via_x(n, v);
			/* FIXME: can we do this .. investigate set_ds_node_ptr(l); */
			return 1;
		}
		if (can_load_d_nox(r) && s < 4) {
			codegen_lr(l);
			make_x_d();
			/* X is now our pointer */

			/* Load D without touching X */
			if (r->op == T_CONSTANT) {
				if (r->value == 0 && nr) {	/* Special case */
					uniop_on_ptr("clr", 0, s);
					return 1;
				} else {
					codegen_lr(r);
				}
			} else
				write_opd(r, "ld", 0);
			invalidate_work();

			/* Store */
			store_via_x(n, v);
			return 1;
		}
		return 0;
	case T_PLUSEQ:
		if (s == 1 && memop_const(n, "inc", nr, 2))
			return 1;
		if (s == 2 && add_to_node(n, 1, 1))
			return 1;
		return do_xeqop(n, "xpluseq");
	case T_MINUSEQ:
		if (s == 1 && memop_const(n, "dec", nr, 2))
			return 1;
		if (s == 2 && add_to_node(n, -1, 1))
			return 1;
		return do_xeqop(n, "xminuseq");
	case T_PLUSPLUS:
		if (s == 1 && memop_const(n, "inc", nr, 1))
			return 1;
		if (s == 2 && add_to_node(n, 1, nr))
			return 1;
		return do_xeqop(n, "xplusplus");
	case T_MINUSMINUS:
		if (s == 1 && memop_const(n, "dec", nr, 1))
			return 1;
		if (s == 2 && add_to_node(n, -1, nr))
			return 1;
		return do_xeqop(n, "xmminus");
	case T_STAREQ:
		return do_xeqop(n, "xmuleq");
	case T_SLASHEQ:
		return do_xeqop(n, "xdiveq");
	case T_PERCENTEQ:
		return do_xeqop(n, "xremeq");
	case T_SHLEQ:
		if (s == 1 && nr && memop_shift(n, "lsl", "lsl"))
			return 1;
		return do_xeqop(n, "xshleq");
	case T_SHREQ:
		if (s == 1 && nr && memop_shift(n, "asr", "lsr"))
			return 1;
		return do_xeqop(n, "xshreq");
	case T_ANDEQ:
		return do_xeqop(n, "xandeq");
	case T_OREQ:
		return do_xeqop(n, "xoreq");
	case T_HATEQ:
		return do_xeqop(n, "xhateq");
#if 0
	/* This won't work as is and requires a rethink	*/
	/* Thankfully it's only the expression/expression case */
	case T_MINUS:
		/* Do it backwards if not a const and on a 6809 */
		if (r->op == T_CONSTANT || s > 2 || cpu_is_09 == 0)
			return 0;
		codegen_lr(r);
		/* Try and write it as
			ldd foo  subd 1,s or similar */
		if (write_opd(l, "sub", 0))
			return 1;
		/* Ok then we have to write it the long way */
		gen_push(r);
		codegen_lr(l);
		if (s == 1)
			op8_on_spi("sub");
		else
			op16d_on_spi("sub");
		/* our op takes it back off stack */
		sp -= get_stack_size(r->type);
		return 1;
#endif
	case T_RSTORE:
		if (nr && can_load_r_with(r)) {
			v = load_u_with(r);
			if (v)
				output_nf("Uleau %u,u", v);
			return 1;
		}
		codegen_lr(r);
		tfr_d_u();
		return 1;
	case T_REQ:
		codegen_lr(r);
		switch(s) {
		case 4:
			output("Msty %u,u", n->snum);
			output("Mstd %u,u", n->snum + 2);
			return 1;
		case 2:
			output("Mstd %u,u", n->snum);
			return 1;
		case 1:
			output("Mstb %u,u", n->snum);
			return 1;
		}
		break;
	case T_REQPLUS:
		codegen_lr(r);
		switch(s) {
		case 4:
			output("Msty ,u++");
			/* Fall through */
		case 2:
			output("Mstd ,u++");
			break;
		case 1:
			output("Mstb ,u+");
			break;
		}
		return 1;
	/* Try and optimise 6809 direct stores of U to local and name */
	case T_LSTORE:
	case T_NSTORE:
		if (r->op == T_RREF && s == 2 && nr) {
			if (op16u_on_node(n, "Mst", 0)) {
				set_u_node(n);
				return 1;
			}
		}
		break;
	}
	return 0;
}

static unsigned gen_cast(struct node *n)
{
	unsigned lt = n->type;
	unsigned rt = n->right->type;
	unsigned ls;
	unsigned rs;

	if (PTR(rt))
		rt = USHORT;
	if (PTR(lt))
		lt = USHORT;

	/* Floats and stuff handled by helper */
	if (!IS_INTARITH(lt) || !IS_INTARITH(rt))
		return 0;

	ls = get_size(lt);
	rs = get_size(rt);

	/* Size shrink is free */
	if (ls <= rs)
		return 1;
	/* Don't do the harder ones */
	if (!(rt & UNSIGNED)) {
		if (rs == 1 && ls == 2) {
			output("Dsex");
			return 1;
		}
		if (opt > 2 && ls == 4) {
			/* Sign extend the 16bit value in D into the high
			   word held in Y.  D must be preserved, so save it
			   and derive the sign (0x0000 or 0xffff) from the
			   high byte, duplicating it into both halves of Y.
			   (The 6800 version can simply store B twice into
			   @hireg; on the 6809 Y has to be built up.) */
			if (rs == 1)
				output("Dsex");
			output("pshs d");
			output("Btfr a,b");
			output("Asex");
			output("Btfr a,b");
			tfr_d_y();
			output("Dpuls d");
			return 1;
		}
		return 0;
	}
	if (rs == 1)
		load_a_const(0);
	if (ls == 4)
		load_y_const(0);
	return 1;
}

unsigned gen_node(register struct node *n)
{
	unsigned s = get_size(n->type);
	unsigned nr = n->flags & NORETURN;
	unsigned se = n->flags & SIDEEFFECT;
	unsigned v;

	/* Function call arguments are special - they are removed by the
	   act of call/return and reported via T_CLEANUP */
	if (n->left && n->op != T_ARGCOMMA && n->op != T_CALLNAME && n->op != T_FUNCCALL)
		sp -= get_stack_size(n->left->type);
	v = n->value;
	switch (n->op) {
	case T_DEREF:
	case T_DEREFPLUS:
		make_x_d();
		load_via_x(n, v);
		return 1;
	case T_EQ:	/* Assign - ToS is address, working value is value */
	case T_EQPLUS:
		invalidate_mem();
		pop_x();
		store_via_x(n, v);
		return 1;
	case T_CONSTANT:
		if (s == 1) {
			load_b_const(v);
			return 1;
		}
		if (s == 2) {
			load_d_const(v);
			return 1;
		}
		/* size 4 varies */
		load_d_const(v);
		if ((n->value >> 16) == WORD(v))
			tfr_d_y();
//			output_nf("Ytfr d,y");	/* FIXME: add this as op for tracking ? */
		else
			load_y_const(n->value >> 16);
		return 1;
	case T_NAME:
		/* TODO: long tracking */
		if (s == 4) {
			load_y_const(0);
			/* Now do the op on the word */
			set_ds_node(n);	/* FIXME - need to sort long and to be after */
			return op16d_on_node(n, "ld", 0);
		}
		/* Fall through */
	case T_LREF:
	case T_NREF:
		/* Don't remove volatile loads */
		if (d_holds_node(n) && !se)
			return 1;
		if (write_opd(n, "Zld", 0)) {
			set_ds_node(n);
			return 1;
		}
		if (s == 4) {
			op16y_on_node(n, "ld", 0);
			op16d_on_node(n, "ld", 2);
			set_d_node(n);
			return 1;
		}
		return 0;
	case T_LSTORE:
	case T_NSTORE:
		if (write_opd(n, "ZMst", 0)) {
			set_ds_node(n);
			/* ST on 6809 sets flags too */
			return 1;
		}
		if (s == 4) {
			op16y_on_node(n, "Mst", 0);
			op16d_on_node(n, "Mst", 2);
			set_ds_node(n);
			return 1;
		}
		break;
	/* TODO: clean up so that the node hold and tracking are checked in
	   called command not here */
	case T_RREF:
		if (d_holds_node(n))
			return 1;
		tfr_u_d();
		set_d_node(n);
		/* tfr does not set flags */
		return 1;
	case T_RSTORE:
		tfr_d_u();
		set_ds_node(n);
		return 1;
	case T_ARGUMENT:
		if (d_holds_node(n))
			return 1;
		v += argbase + frame_len;
	case T_LOCAL:
		if (d_holds_node(n))
			return 1;
		/* Should look if its already in X FIXME: */
		output("leax %u,s", v + sp);
		set_x_node(n);
		/* X is now the value we need */
		tfr_x_d();
		/* The leax will set the flags correctly if our working size
		   is word */
		if (s == 2)
			set_z_flag(0);
		return 1;
	case T_LDEREF:
		if (n->snum == 0 && s <= 2) {
			if (s == 1)
				output("ZBldb [%u,s]", v + sp);
			else
				output("ZDldd [%u,s]", v + sp);
			return 1;
		} else {
			output("Xldx %u,s", v + sp);
		}
		load_via_x(n, n->snum);
		return 1;
	case T_NDEREF:
		/* Deref through a name. Only generated for the 6809
		   and only when the object offset is 0 and size is 1 or 2 */
		if (s == 1)
			output("ZBldb [%s + %u]", namestr(n->snum), v);
		else
			output("ZDldd [%s + %u]", namestr(n->snum), v);
		return 1;
	case T_RDEREF:
		if (s == 4) {
			output("Yldy %u,u", n->snum);
			output("Dldd %u,u", n->snum + 2);
		} else if (s == 2)
			output("ZDldd %u,u", n->snum);
		else
			output("ZBldb %u,u", n->snum);
		return 1;
	case T_RDEREFPLUS:
		if (s == 4) {
			output("UYldy ,u++");
			output("UDldd ,u++");
		} else if (s == 2)
			output("ZUDldd ,u++");
		else
			output("ZUBldb ,u+");
		return 1;
	case T_LEQ:
		/* We probably want some indirecting helpers later */
		if (n->snum == 0 && s <= 2) {
			if (s == 1)
				output("ZMstb [%u,s]", v + sp);
			else
				output("ZMstd [%u,s]", v + sp);
			return 1;
		}
		output("Xldx %u,s", v + sp);
		if (s == 1)
			op8_on_ptr("ZMst", n->snum);
		else if (s == 2)
			op16d_on_ptr("ZMst", n->snum);
		else
			store_via_x(n, n->snum);
		return 1;
	case T_NEQ:
		if (s == 1)
			output("ZMstb [%s + %u]", namestr(n->snum), v);
		else
			output("ZMstd [%s + %u]", namestr(n->snum), v);
		return 1;
	/* Type casting */
	case T_CAST:
		if (nr)
			return 1;
		return gen_cast(n);
	/* Single argument ops we can do directly */
	case T_TILDE:
		if (s == 4)
			return 0;
		if (s == 2) {
			output("coma");
			modify_a(~a_val);
		}
		output("comb");
		modify_b(~b_val);
		if (s == 1)
			set_z_flag(0);
		return 1;
	case T_NEGATE:
		if (s == 2) {
			negate_d();
			return 1;
		}
		if (s == 1) {
			output("Znegb");
			modify_b(-b_val);
			return 1;
		}
		if (n->type == FLOAT) {
			/* Invert the top bit of Y */
			output("Yleay 0x8000,y");
			return 1;
		}
		return 0;
	/* Double argument ops we can handle easily */
	case T_PLUS:
		if (n->type == FLOAT)
			return 0;
		/* This is a special case as we need to order the
		   operations */
		return write_tos_opd(n, "add", "adc");
	case T_AND:
		return write_tos_op(n, "and");
	case T_OR:
		return write_tos_op(n, "or");
	case T_HAT:
		return write_tos_op(n, "eor");
	/* These do the maths backwards in effect so use the other equvialent
	   compare for the ordered part */
	case T_EQEQ:
		return cmp_op(n, "booleq", "booleq", 1);
	case T_BANGEQ:
		return cmp_op(n, "boolne", "boolne", 2);
	case T_LT:
		return cmp_op(n, "boolugt", "boolgt", 0);
	case T_GT:
		return cmp_op(n, "boolult", "boollt", 0);
	case T_LTEQ:
		return cmp_op(n, "booluge", "boolge", 0);
	case T_GTEQ:
		return cmp_op(n, "boolule", "boolle", 0);
	/* EQ ops via top of stack. Simpler ones alreadu got handled
	   by direct or shortcut. For a complex one we try and inline it
	   by pulling X, but if we can't then we will use a helper between
	   X and D (or Y:D for long) */
	case T_STAREQ:
		return do_xeqop(n, "xmuleq");
	case T_SLASHEQ:
		return do_xeqop(n, "xdiveq");
	case T_PERCENTEQ:
		return do_xeqop(n, "xremeq");
	case T_SHLEQ:
		if (s == 1 && nr && memop_shift(n, "lsl", "lsl"))
			return 1;
		return do_xeqop(n, "xshleq");
	case T_SHREQ:
		if (s == 1 && nr && memop_shift(n, "asr", "lsr"))
			return 1;
		return do_xeqop(n, "xshreq");
	case T_ANDEQ:
		return do_stkeqop(n, "xandeq");
	case T_OREQ:
		return do_stkeqop(n, "xoreq");
	case T_HATEQ:
		return do_stkeqop(n, "xhateq");
	case T_PLUSEQ:
		return do_stkeqop(n, "xpluseq");
	case T_MINUSEQ:
		return do_stkeqop(n, "xminuseq");
	/* Function calls that were not to a constant name */
	case T_BOOL:
	case T_BANG:
		/* Should always be handled as a shortvut */
		error("bn");
	}
	return 0;
}

/* TODO
	WIP Track X v S offset
	Conditions (EQEQ BANGEQ LT GT LTEQ GTEQ) - optimize < 0 and
	other easy ones ?
	BANG
	BOOL
	STAR SLASH PERCENT (const optimizations)
	WIP Track register values
	Track condition codes
	Think about how to improve long handling
	Arg helpers like Z8 etc so we can optimize post 6800 a bit
	and also optimize push const cases and long consts especially
	(and probably push arg)
	Optimize arg pushes if have pshx by using LDX PSHX for consts except
		6809)
	Add all the general operations we can do 16bit with a 6803
*/

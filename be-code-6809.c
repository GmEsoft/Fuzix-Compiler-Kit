#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6800.h"

static unsigned op8r_on_node(struct node *r, char reg, const char *op, unsigned off);
static unsigned can_load_r8_simple(struct node *r, unsigned off);
static unsigned load_r_with(char reg, struct node *r, unsigned off);

unsigned label;		/* Used to hand out local labels in the form X%u */

void push_d(void)
{
	output_nf("pshs d");
}

void pop_d(void)
{
	output_nf("puls d");
}

void tfr_u_d(void)
{
	output_nf("tfr u,d");
	/* TODO: when tracking sorted D is now a copy of the regvar */
	invalidate_work();
}

void tfr_d_u(void)
{
	output_nf("tfr d,u");
	/* Don't track U yet but this will make D a copy of the regvar */
}


/*
 *	Perform an optimized 32bit constant add
 *
 *	For stuff using @hireg we try and optimize the common cases of adding
 *	smaller constants. For stuff where we add high bits only shortcut
 *	some of the work.
 *
 *	Care is needed as add_d_const() will not leave carry valid if asked
 *	to add 0, hence the use of op.
 */

unsigned gen_add32(unsigned long v)
{
	unsigned l = WORD(v);
	unsigned h = WORD(v >> 16);

	if (l) {
		add_d_const(l);
		output_nf("bcc X%u", ++label);
		output("leay 1,y");
		outlabel("X%u", label);
	}
	if (h)
		output("leay %u,y", h);
	return 1;
}

/* String variants to push 2-8 bytes. add cc, to get odd. Also stacks
   with u in the right place for register frames. We could abuse dp
   to get one byte more with a slight complication */
static char regframe[] = "d,x,y,u";

static void pshframe(unsigned size)
{
	output_nf("pshs %s%s",
		size & 1 ? "cc,":"",
		regframe + 8 - (size & ~1));
}

void gen_prologue(const char *name)
{
	unreachable = 0;
	invalidate_all();
	outlabel("%s", name);
}

/* Generate the stack frame */
void gen_frame(unsigned size, unsigned aframe)
{
	argbase = ARGBASE;
	frame_len = size;

	if (func_flags & F_REG(1)) {
		if (size <= 6)
			pshframe(size + 2);
		else {
			output_nf("pshs u");
			adjust_s(-size, 0);
		}
		argbase += 2;
		return;
	}
	if (size > 2 && size <= 8)
		pshframe(size);
	else
		adjust_s(-size, 0);
	return;
}


void gen_epilogue(unsigned size, unsigned argsize)
{
	unsigned vr = func_flags & F_VOIDRET;
	unsigned r = func_flags & F_REG(1);
	char *str;
	unsigned max = 2;
	static char rw[] = "d,y,x";
	static char rb[] = "y,x,a";
	static char rd[] = "d,y,x,dp";
	unsigned byteok = vr;

	/* FIXME: it would be better if the front end passed backends
	   the type of the first arg and return not this flag. Then we
	   could also use Y reliably for int returns and A for char */
	if (vr)
		max = 6;

	if (cpufeat & 2) {
		/* use-dp */
		byteok = 1;
		max++;
	}
	if (sp)
		error("sp");


	printf(";size %u byteok %u vr %u\n", size, byteok, vr);
	if (size && size <= max && (byteok | !(size & 1))) {
		if (size & 1) {
			if (byteok)
				str = rd + 7 - size;
			else
				str = rb + 5 - size;
		} else
			str = rw + 6 - size;
		output_nf("puls %s%s,pc", str, r ? ",u" : "");
	} else {
		adjust_s(size, vr ? 0 : 1);
		if (r)
			output_nf("puls u,pc");
		else
			output_nf("rts");
	}
	unreachable = 1;
}

/* We handle function arguments specially as we both push them to a different
   stack and optimize them. The tree looks like this

                 FUNCCALL|CALLNAME
                  /
	       ARGCOMMA
	        /    \
	      ...    EXPR
	      /
	    ARG
 */

#define R_A		1
#define R_B		2
#define R_X		4
#define R_Y		8
#define R_U		16

#define USE_A		1
#define USE_B		2
#define USE_D		3
#define USE_X		4
#define USE_Y		5
#define USE_U		6
#define USE_L		7

#define NCALL		16		/* For now TODO */

static uint8_t rstk_s[NCALL];		/* Register mask for each push */
static uint32_t fstk_s[NCALL];		/* Operation list for each push */
static uint8_t *rstk = rstk_s;
static uint32_t *fstk = fstk_s;
static uint8_t rused;
static uint32_t stackop;

static void pushregs(void)
{
	char *rptr = "abxyu";

	if (rused == 0)
		return;

	/* FIXME: needs to run via output_nf() eventually */

	/* We have the registers loaded, now push and clean up the
	   tracking stack */
	printf("\tpshs ");
	if (rused & R_A)
		sp++;
	if (rused & R_B)
		sp++;
	if (rused & R_X)
		sp += 2;
	if (rused & R_Y)
		sp += 2;
	if (rused & R_U)
		sp += 2;
	while(rused) {
		if (rused & 1) {
			putchar(*rptr);
			if (rused > 1)
				putchar(',');
		}
		rptr++;
		rused >>= 1;
	}
	putchar('\n');
}

static void argstack(struct node *n)
{
	unsigned op;
	unsigned v;
	stackop >>= 3;
	op = stackop & 7;
	if (op == 0) {
		pushregs();
		stackop = *--fstk;
		rused = *--rstk;
		stackop >>= 3;
		op = stackop & 7;
	}
	switch(op) {
	case 0:
		error("astk");
	case USE_A:
		op8r_on_node(n, 'a', "ld", 0);
		break;
	case USE_B:
		op8_on_node(n, "ld", 0);
		break;
	case USE_D:
		codegen_lr(n);
		break;
	case USE_X:
		v = load_x_with(n, 0);
		if (v) {
			output("leax %u,x",  WORD(v));
			invalidate_x();	/* for now */
		}
		break;
	case USE_Y:
		v= load_r_with('y', n, 0);
		if (v)
			output("leay %u,y",  WORD(v));
		break;
	case USE_U:
		/* Already usable */
		break;
	/* Longs not yet converted */
	case USE_L:
		/* Should try and load swapping the halves into
		   the next two registers */
		codegen_lr(n);
		gen_push(n);
		break;
	}
}

static void prep_push(struct node *n)
{
	unsigned s = get_size(n->type);

	stackop <<= 3;
	/* Downward we try and assign things to the ideal register */
	if (s == 1) {
		if (rused == 0 && can_load_r8_simple(n, 0)) {
			stackop |= USE_A;
			rused |= R_A;
		} else if (rused == R_A && can_load_r8_simple(n, 0)) {
			stackop |= USE_B;
			rused |= R_B;
		} else {
			if(stackop) {
				*fstk++ = stackop;
				*rstk++ = rused;
			}
			/* Push ourselves an op to codegen_lr() then push B */
			*fstk++ = USE_D << 3;
			*rstk++ = R_B;
			/* Prepare again */
			stackop = 0;
			rused = 0;
		}
	} else if (s == 2) {
		/* A or B or both pushed already ? */
		/* We might have X used but not D because of pushing
		   a local address */
		if (n->op == T_RREF) {
			stackop |= USE_U;
			*fstk++ = stackop << 3;
			*rstk++ = rused | R_U;
			stackop = 0;
			rused = 0;
		} else if (rused == 0 && can_load_r_simple(n, 0) && n->op != T_LOCAL && n->op != T_ARGUMENT) {
			stackop |= USE_D;
			rused |= R_A|R_B;
		} else if (can_load_r_with(n, 0) && (rused & (R_X|R_Y)) != (R_X|R_Y)) {
			/* X and Y not yet used so can use X */
			if (rused & R_X) {
				stackop |= USE_Y;
				rused |= R_Y;
			} else {
				stackop |= USE_X;
				rused |= R_X;
			}
		} else {
			/* Push any outstanding as the current op */
			if (stackop) {
				*fstk++ = stackop;
				*rstk++ = rused;
			}
			/* Push ourselves an op */
			*fstk++ = USE_D << 3;
			*rstk++ = R_A | R_B;
			/* Prepare for next op */
			stackop = 0;
			rused = 0;
		}
	} else {
		/* Will tackle long stuff later */
		if (stackop) {
			*fstk++ = stackop;
			*rstk++ = rused;
		}
		*fstk++ = USE_L << 3;
		*rstk++ = 0;	/* handled by gen_push instead */
		stackop = 0;
		rused = 0;
		/* Handled via the gen_push so it will fix sp itself */
	}
}

static void do_fcall(struct node *n)
{
	if (n == NULL)
		return;
	if (n->op == T_ARGCOMMA) {
		/* Recurse down arguments and stack then on the way up */
		prep_push(n->right);
		do_fcall(n->left);
		argstack(n->right);
	} else {
		prep_push(n);
		stackop <<= 3;
		argstack(n);
		/* May be a finall push queued here */
	}
	/* Final node done */
}

void gen_fcall(struct node *n)
{
	*fstk++ = stackop;
	*rstk++ = rused;
	stackop = 0;
	rused = 0;
	do_fcall(n);
	pushregs();
	rused = *--rstk;
	stackop = *--fstk;
}

/*
 *	6809 implementation of the code generation section
 *	We have Y as the upper bits of the working value, and we have
 *	U as a register. There are no complications about index ranges,
 *	and we can index off stack. 6809 is easy mode.
 */

static unsigned make_with_op(char r, register uint8_t ev, register uint8_t tv)
{
	/* INC and DEC */
	if (((ev + 1) & 0xFF) == tv) {
		output("inc%c", r);
		return 1;
	}
	if (((ev - 1) & 0xFF) == tv) {
		output("dec%c", r);
		return 1;
	}
	/* COM */
	if ((ev ^ 0xFF) == tv) {
		output("com%c", r);
		return 1;
	}
	/* LSR */
	if ((ev >> 1) == tv) {
		output("lsr%c", r);
		return 1;
	}
	/* ASL/LSL */
	if (((ev << 1) & 0xFF) == tv) {
		output("asl%c", r);
		return 1;
	}
	/* ASR ? */
	if ((ev & tv & 0x80) && (ev >> 1) == (tv & 0x7F)) {
		output("asr%c", r);
		return 1;
	}
	/* NEG */
	if (((256 - ev) & 0xFF) == tv) {
		output("neg%c", r);
		return 1;
	}
	return 0;
}

/* 16bit constant load */

/* TODO:
	spot case where one half is right and other can be optimized
	spot case where right half can be SEXed into left
	spot case we can use tba
 */
void load_d_const(uint16_t n)
{
	unsigned hi,lo;

	lo = n & 0xFF;
	hi = n >> 8;

/*	printf(";want %04X have %02X:%02X val %d %d\n",
		n, a_val, b_val, a_valid, b_valid); */

	if (n == 0) {
		if (!a_valid || a_val)
			output("clra");
		if (!b_valid || b_val)
			output("clrb");
	} else if (!a_valid || !b_valid || a_val != hi || b_val != lo)
		output("ldd #%u", n);

	a_valid = 1;	/* We know the byte values */
	b_valid = 1;
	d_valid = 0;	/* No longer an object reference */
	a_val = hi;
	b_val = lo;

}

/* Unlike 6800 style processors TFR is slower and as long as a constant
   load so don't use it */
void load_a_const(uint8_t n)
{
	if (a_valid && n == a_val)
		return;
	if (n == 0)
		output("clra");
	else if (n == 0xFF && b_valid && (b_val & 0x80))
		output("sex");
	else if (!a_valid || !make_with_op('a', a_val, n))
		output("lda #%u", n & 0xFF);
	a_valid = 1;
	a_val = n;
	d_valid = 0;
}

void load_b_const(uint8_t n)
{
	if (b_valid && n == b_val)
		return;
	if (n == 0)
		output("clrb");
	else if (!b_valid || !make_with_op('b', b_val, n))
		output("ldb #%u", n & 0xFF);
	b_valid = 1;
	b_val = n;
	d_valid = 0;
}

void add_d_const(uint16_t n)
{
	if (n == 0)
		return;

	/* TODO: can do better in terms of obj/offset but not clear it is
	   that useful */

	d_valid = 0;

	if (n & 0xFF)
		output("addd #%u", n);
	else
		output("adda #%u", n >> 8);

	if (b_val + (n & 0xFF) < b_val)
		a_val += (n >> 8) + 1;
	else
		a_val += (n >> 8);
	b_val += (n & 0xFF);
}

void add_b_const(uint8_t n)
{
	if (n == 0)
		return;
	output("addb #%u", n & 0xFF);
	b_val += n;
	d_valid = 0;

}

void load_a_b(void)
{
	output_nf("tfr b,a");
	a_val = b_val;
	a_valid = b_valid;
	d_valid = 0;
}

void load_b_a(void)
{
	output_nf("tfr a,b");
	b_val = a_val;
	b_valid = a_valid;
	d_valid = 0;
}

void move_s_d(void)
{
	output_nf("tfr s,d");
	invalidate_work();
}

void move_d_s(void)
{
	output_nf("tfr d,s");
}

void swap_d_y(void)
{
	output_nf("exg d,y");
}

void swap_d_x(void)
{
	output_nf("exg d,x");
	invalidate_work();
	invalidate_x();
}

/* Get D into X (may trash D) */
void make_x_d(void)
{
	output_nf("tfr d,x");
	invalidate_x();
}

/* Get X into D (may trash X) */
void make_d_x(void)
{
	output_nf("tfr x,d");
	invalidate_x();
}

void pop_x(void)
{
	output_nf("puls x");
	invalidate_x();
}

/*
 *	There are multiple strategies depnding on chip features
 *	available.
 */
void adjust_s(int n, unsigned save_d)
{
	if (n)
		output_nf("leas %d,s", n);
	return;
}

void op8_on_ptr(const char *op, unsigned off)
{
	output("%sb %u,x", op, off);
}

/* Do the low byte first in case it's add adc etc */
void op16_on_ptr(const char *op, const char *op2, unsigned off)
{
	/* Big endian */
	output("%sb %u,x", op, off + 1);
	output("%sa %u,x", op2, off);
}

/* Operations where D can be used on later processors */
void op16d_on_ptr(const char *op, const char *op2, unsigned off)
{
	/* Big endian */
	output("%sd %u,x", op, off);
}

static void op8_on_tos(const char *op)
{
	output("%sb ,s+", op);
}

static void op32_on_ptr(const char *op, const char *op2, unsigned off)
{
	output("%sb %u,x", op, off + 3);
	output("%sa %u,x", op2, off + 2);
	swap_d_y();
	output("%sb %u,x", op2, off + 1);
	output("%sa %u,x", op2, off);
	swap_d_y();
}

void op32d_on_ptr(const char *op, const char *op2, unsigned off)
{
	output("%sd %u,x", op, off + 2);
	swap_d_y();
	output("%sb %u,x", op2, off + 1);
	output("%sa %u,x", op2, off);
	swap_d_y();
}

void load32(unsigned off)
{
	output("ldy %u,x", off);
	output("ldd %u,x", off + 2);
}

void store32(unsigned off, unsigned nr)
{
	output("sty %u,x", off);
	output("std %u,x", off + 2);
}

void uniop_on_ptr(register const char *op, register unsigned off,
						register unsigned size)
{
	off += size;
	while(size--)
		output("%s %u,x", op, --off);
}

unsigned make_local_ptr(unsigned off, unsigned rlim)
{
	output("leax %u,s", off + sp);
	return 0;
}

/* Do we need this on 6809 ? */
unsigned make_tos_ptr(void)
{
	output_nf("tfr s,x");
	x_fpoff = sp;
	x_fprel = 1;
	return 0;
}

static char *addr_form(register struct node *r, unsigned off, unsigned s)
{
	static char addr[32];
	const char *mod = "";
	unsigned v = r->value;

	if (s == 1)
		mod = "<";

	switch(r->op) {
	case T_CONSTANT:
		if (s == 1)
			v &= 0xFF;
		sprintf(addr, "#%s%u", mod, v + off);
		return addr;
	case T_NAME:
		sprintf(addr, "#%s%s+%u", mod, namestr(r->snum), v + off);
		return addr;
	case T_NSTORE:
	case T_NREF:
		sprintf(addr, "%s+%u%s", namestr(r->snum), v + off, pic_op);
		return addr;
	/* Only occurs on 6809 */
	case T_RDEREF:
		sprintf(addr, "%u,u", r->snum + off);
		return addr;
	/* TODO: Can we do locals safely via ,s ?? */
	default:
		error("aform");
	}
	return NULL;
}

/* Those with address forms we can directly load. Need to look at locals via ,s */
unsigned can_load_d_nox(struct node *n, unsigned off)
{
	register unsigned op = n->op;
	if (op == T_CONSTANT || op == T_NAME || op == T_NREF)
		return 1;
	return 0;
}

/* These functions must not touch X on the 6809, they can on others */
static unsigned op8r_on_node(struct node *r, char reg, const char *op, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	invalidate_work();	/* FIXME */

	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		output("%s%c %u,s", op, reg, off + v + sp);
		break;
	case T_LDEREF:
		if (r->snum)
			return 0;
		output("%s%c [%u,s]", op, reg, off + v + sp);
		break;
	case T_NSTORE:
		store = 1;
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	case T_RDEREF:
		output("%s%c %s", op, reg, addr_form(r, off, 1));
		break;
	default:
		return 0;
	}
	/* FIXME for constant load */
	if (store)
		invalidate_mem();
	else
		invalidate_work();
	return 1;
}

unsigned op8_on_node(struct node *r, const char *op, unsigned off)
{
	return op8r_on_node(r, 'b', op, off);
}

/* Do the low byte first in case it's add adc etc */
unsigned op16_on_node(struct node *r, const char *op, const char *op2, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		/* Big endian */
		off += v + sp;
		output("%sb %u,s", op, off + 1);
		output("%sa %u,s", op2, off);
		break;
	case T_CONSTANT:
		output("%sa #>%u", op, (v + off) & 0xFFFF);
		output("%sb #<%u", op2, (v + off) & 0xFFFF);
		break;
	case T_NSTORE:
		store = 1;
	case T_NREF:
	case T_NAME:
		output("%sa %s", op, addr_form(r, off, 1));
		output("%sb %s", op2, addr_form(r, off + 1, 1));
		break;
	default:
		return 0;
	}
	if (store)
		invalidate_mem();
	else
		invalidate_work();
	return 1;
}

unsigned op16d_on_node(struct node *r, const char *op, const char *op2, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		/* Big endian */
		output("%sd %u,s", op, off + v + sp);
		break;
	case T_LDEREF:
		/* Big endian */
		if (r->snum)
			return 0;
		output("%sd [%u,s]", op, off + v + sp);
		break;
	case T_NSTORE:
		store = 1;
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		output("%sd %s", op, addr_form(r, off, 2));
		break;
	default:
		return 0;
	}
	if (store)
		invalidate_mem();
	else
		invalidate_work();
	return 1;
}

static unsigned op16r_on_node(struct node *r, char reg, const char *op, unsigned off)
{
	unsigned v = r->value;
	switch(r->op) {
	case T_LSTORE:
		invalidate_mem();
	case T_LREF:
		v = r->value;
		output("%s%c %u,s", op, reg, v + off + sp);
		break;
	case T_NSTORE:
		invalidate_mem();
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		output("%s%c %s", op, reg, addr_form(r, off, 2));
		break;
	/* These two cases will be doable on the left with right const */
	case T_MINUS:
	case T_PLUS:
		v = r->right->value;
		if (r->op == T_MINUS)
			v = -v;
		op16r_on_node(r->left, reg, op, off);
		output("lea%c %u,%c", reg, WORD(v), reg);
		break;
	default:
		return 0;
	}
	return 1;
}

unsigned op16u_on_node(struct node *r, const char *op, unsigned off)
{
	return op16r_on_node(r, 'u', op, off);
}

unsigned op16y_on_node(struct node *r, const char *op, unsigned off)
{
	return op16r_on_node(r, 'y', op, off);
}

unsigned write_op(struct node *r, const char *op, const char *op2, unsigned off)
{
	unsigned s = get_size(r->type);
	if (s == 2)
		return op16_on_node(r, op, op2, off);
	if (s == 1)
		return op8_on_node(r, op, off);
	return 0;
}

unsigned write_opd(struct node *r, const char *op, const char *op2, unsigned off)
{
	unsigned s = get_size(r->type);
	if (s == 2)
		return op16d_on_node(r, op, op2, off);
	if (s == 1)
		return op8_on_node(r, op, off);
	return 0;
}

unsigned write_uni_op(register struct node *r, const char *op, unsigned off)
{
	unsigned v = r->value;
	unsigned s = get_size(r->type);

	if (s == 4)
		return 0;

	switch(r->op) {
	case T_LSTORE:
	case T_LREF:
		if (s == 2)
			output("%s %u,s", op, v + off + 1);
		output("%s %u,s", op, v + off);
		break;
	case T_NSTORE:
	case T_NREF:
		output("%s %s", op, addr_form(r, off, 1));
		if (s == 2)
			output("%s %s", op, addr_form(r, off + 1, 1));
		break;
	default:
		return 0;
	}
	return 1;
}

static void op16_on_tos(const char *op)
{
	invalidate_work();		/* ?? needed on 09 ? */
	output("%sa ,s+", op);
	output("%sb ,s+", op);
}

void op16d_on_tos(const char *op)
{
	invalidate_work();
	output("%sd ,s++", op);
}

unsigned write_tos_op(struct node *n, const char *op)
{
	unsigned s = get_size(n->type);
	if (s == 4) {
		swap_d_y();
		op16_on_tos(op);
		swap_d_y();
		op16_on_tos(op);
	} else if (s == 2)
		op16_on_tos(op);
	else
		op8_on_tos(op);
	invalidate_work();
	return 1;
}

unsigned write_tos_opd(struct node *n, const char *op, const char *op2)
{
	unsigned s = get_size(n->type);
	if (s == 4) {
		output("%sd 2,s", op);
		swap_d_y();
		output("%sb 1,s", op2);
		output("%sa ,s", op2);
		swap_d_y();
		output_nf("leas 4,s");
		return 1;
	} else if (s == 2)
		op16d_on_tos(op);
	else
		op8_on_tos(op);
	invalidate_work();
	return 1;
}

unsigned write_tos_uniop(struct node *n, const char *op)
{
	unsigned s = get_size(n->type);
	if (s > 2)
		return 0;
	if (s == 2)
		output("%s ,s++", op);
	output("%s ,s+", op);
	return 1;
}

/* TODO: decide how much we inline for -Os */
static void left16(unsigned v)
{
	if (v >= 16)
		load_d_const(0);
	else if (v >= 8) {
		load_a_b();
		load_b_const(0);
		v -= 8;
		if (v)
			repeated_op(v, "lsla");
	} else {
		if (optsize && v > 2)
			output("jsr __lsl%u", v);
		else while(v--) {
			output("lslb");
			output("rola");
		}
	}
}

unsigned left_shift(struct node *n)
{
	unsigned s = get_size(n->type);
	unsigned v;

	if (s > 2 || n->right->op != T_CONSTANT)
		return 0;
	v = n->right->value;
	if (s == 1) {
		if (v >= 8) {
			load_b_const(0);
			return 1;
		}
		invalidate_work();
		repeated_op(v, "lslb");
		return 1;
	}
	if (s == 2) {
		left16(v);
		invalidate_work();
		return 1;
	}
	return 0;
}

unsigned right_shift(struct node *n)
{
	unsigned s = get_size(n->type);
	unsigned v;
	const char *op = "asr";

	if (n->type & UNSIGNED)
		op = "lsr";

	if (s > 2 || n->right->op != T_CONSTANT)
		return 0;
	v = n->right->value;
	if (s == 1) {
		if (v >= 8) {
			load_b_const(0);
			return 1;
		}
		invalidate_work();
		while(v--)
			output("%sb", op);
		return 1;
	}
	if (s == 2) {
		if (v >= 16) {
			load_d_const(0);
			return 1;
		}
		if (v >= 8 && (n->type & UNSIGNED)) {
			load_b_a();
			load_a_const(0);
			v -= 8;
			if (v) {
				while(v--)
					output("%sb", op);
				invalidate_work();
			}
			return 1;
		}
		if (optsize && v > 2)
			output("jsr __%s%u", op, v);
		else while(v--) {
			output("%sa", op);
			output("rorb");
		}
		invalidate_work();
		return 1;
	}
	return 0;
}

/* See if we can easily get the value we want into X/Y/U rather than D. Must
   not harm D in the process. We can make this smarter over time if needed.
   Might be worth passing if we can trash D as it will help make_local_ptr
   later, and will be true for some load cases */
unsigned can_load_r_simple(struct node *r, unsigned off)
{
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	case T_RREF:
	case T_RDEREF:
		return 1;
	}
	return 0;
}

static unsigned can_load_r8_simple(struct node *r, unsigned off)
{
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	/* Can't RREF as no valid tfr u,a or u,b */
	case T_RDEREF:
		return 1;
	}
	return 0;
}

/* Also allow offset in the result and some level of complexity via
   lea for offsets on things like struct */
unsigned can_load_r_with(struct node *r, unsigned off)
{
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	case T_RREF:
	case T_RDEREF:
		return 1;
	case T_PLUS:
	case T_MINUS:
		/* For now - we ought to be able to recurse these */
		if (!can_load_r_simple(r->left, off))
			return 0;
		if (r->right->op != T_CONSTANT)
			return 0;
		return 1;
	}
	return 0;
}

/* For 6800 at least it is usually cheaper to reload even if the value
   we want is in D */
static unsigned load_r_with(char reg, struct node *r, unsigned off)
{
	unsigned v = r->value;
	switch(r->op) {
	case T_ARGUMENT:
		v += argbase + frame_len;
	case T_LOCAL:
		output("lea%c %u,s", reg, v + sp);
		break;
	case T_LREF:
		output("ld%c %u,s", reg, v + sp);
		break;
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	case T_RDEREF:
		output("ld%c %s", reg, addr_form(r, off, 2));
		break;
	case T_RREF:
		if (reg != 'u')
			output_nf("tfr u,%c", reg);
		break;
	case T_PLUS:
		/* Special case array/struct */
		if (cpu_is_09 && can_load_r_simple(r->left, off) &&
			r->right->op == T_CONSTANT) {
			load_r_with(reg, r->left, off);
			return r->right->value;
		}
		break;
	case T_MINUS:
		if (cpu_is_09 && can_load_r_simple(r->left, off) &&
			r->right->op == T_CONSTANT) {
			load_r_with(reg, r->left, off);
			return -r->right->value;
		}
		break;
	default:
		error("lxw");
	}
	return 0;
}

unsigned load_x_with(struct node *r, unsigned off)
{
	unsigned rv = load_r_with('x', r, off);
	invalidate_x();
	return rv;
}

/* 6809 specific register loading */
unsigned load_u_with(struct node *r, unsigned off)
{
	return load_r_with('u', r, off);
}

/* 6809 we have SUBD and access to lots of things */
unsigned cmp_direct(struct node *n, const char *uop, const char *sop)
{
	struct node *r = n->right;
	unsigned s = get_size(r->type);
	unsigned v = r->value;
	unsigned zop = 0;
	const char *op = sop;

	if (s == 4)
		return 0;

	if (r->type & UNSIGNED)
		op = uop;
	/* Use == 0 helper */
	if (v == 0 && s == 2 && r->op == T_CONSTANT)
		zop = 1;
	/* Do byte compare as we know the high one matches */
	else if (s == 2 && a_valid && (v & 0xFF00) == a_val && r->op == T_CONSTANT) {
		output("subb #%u", BYTE(v));
		/* We implicitly did the signed first byte comparison */
		op = uop;
	} else if (s == 1 && op8_on_node(r, "sub", 0) == 0)
		return 0;
	else if (s == 2 && op16d_on_node(r, "sub", "sbc", 0) == 0)
		return 0;
	if (!zop)
		output("%s %s", jsr_op, op);
	else
		output("%s %s0", jsr_op, op);
	n->flags |= ISBOOL;
	set_a(0);
	invalidate_b();
	trackf = stdflags;
	return 1;
}

/*
 *	Do fast multiplies were we can
 */
int count_mul_cost(unsigned n)
{
	int cost = 0;
	if ((n & 0xFF) == 0) {
		n >>= 8;
		cost += 3;		/* tfr a,b clrb */
	}
	while(n > 1) {
		if (n & 1)
			cost += 4;	/* std s++, addd ,--s */
		n >>= 1;
		cost += 2;		/* lslb rola */
	}
	return cost;
}

/* We can probably do better on 6809 TODO */
/* Write the multiply for any value > 0 */
void write_mul(unsigned n)
{
	unsigned pops = 0;
	if ((n & 0xFF) == 0) {
		load_a_b();
		load_b_const(0);
		n >>= 8;
	}
	while(n > 1) {
		if (n & 1) {
			pops++;
			push_d();
		}
		output("lslb");
		output("rola");
		n >>= 1;
	}
	while(pops--)
		output("addd ,s++");
}

unsigned gen_fast_mul(unsigned s, unsigned n)
{
	/* Pulled out of my hat 8) */
	unsigned cost = 15 + 3 * opt;
	if (s > 2)
		return 0;

	if (n == 0) {
		load_d_const(0);
		return 1;
	}
	/* Isn't a simple shift */
	if (n & (n - 1)) {
		/* The base cost of a helper is 8 */
		if (optsize)
			cost = 8;
		if (count_mul_cost(n) <= cost) {
			write_mul(n);
			invalidate_work();
			return 1;
		}
		output("ldx #%u", n);
		invalidate_x();
		output("%s __mulx", jsr_op);
		invalidate_work();
		return 1;
	} else {
		unsigned c = 0;
		/* Power of 2 */
		while(n >>= 1)
			c++;
		left16(c);
		return 1;
	}
}

unsigned gen_fast_div(unsigned n, unsigned s, unsigned u)
{
	u &= UNSIGNED;
	if (s != 2)
		return 0;
	if (n == 1)
		return 1;
	if (n == 256 && u) {
		load_b_a();
		load_a_const(0);
		return 1;
	}
	if (n & (n - 1))
		return 0;
	if (u) {
		while(n > 1) {
			output("lsra");
			output("rorb");
			n >>= 1;
		}
	} else {
		output("tsta");
		output_nf("bpl X%u", ++label);
		output("addd #%u", WORD(n - 1));
		outlabel("X%u", label);
		while(n > 1) {
			output("asra");
			output("rorb");
			n >>= 1;
		}
	}
	invalidate_work();
	return 1;
}

void op_on_ptr(struct node *n, const char *op, unsigned off)
{
	unsigned s = get_size(n->type);
	if (s == 1)
		op8_on_ptr(op, off);
	else if (s == 2)
		op16_on_ptr(op, op, off);
	else
		op32_on_ptr(op,op, off);
}

void opd_on_ptr(struct node *n, const char *op, const char *op2, unsigned off)
{
	unsigned s = get_size(n->type);
	if (s == 1)
		op8_on_ptr(op, off);
	else if (s == 2)
		op16d_on_ptr(op, op2, off);
	else
		op32d_on_ptr(op, op2, off);
}

/* TODO; compare and flip the boolify test rather than go via stack
   when we can */
unsigned cmp_op(struct node *n, const char *uop, const char *op)
{
	unsigned s = get_size(n->right->type);
	if (n->right->type & UNSIGNED)
		op = uop;
	if (s > 2)	/* For now anyway */
		return 0;
	/* We can do this versus s+ or s++ */
	if (s == 1)
		op8_on_tos("sub");
	else if (s == 2)
		op16d_on_tos("sub");
	output("%s %s", jsr_op, op);
	n->flags |= ISBOOL;
	trackf = stdflags;
	invalidate_work();
	set_a(0);
	return 1;
}

unsigned gen_push(struct node *n)
{
	unsigned size = get_size(n->type);
	/* Our push will put the object on the stack, so account for it */
	sp += get_stack_size(n->type);
	switch(size) {
	case 1:
		output_nf("\tpshs b");
		return 1;
	case 2:
		push_d();
		return 1;
	case 4:	/* Have to split them to get the order right */
		/* Or we could go PDP11 style mixed endian long ? */
		push_d();
		output_nf("\tpshs y");
		return 1;
	}
	return 0;
}

void gen_label(const char *tail, unsigned n)
{
	invalidate_all();
	unreachable = 0;
	outlabel("L%d%s", n, tail);
}

unsigned gen_exit(const char *tail, unsigned n)
{
	/* TODO: if unreachable here return 1 ? */
	output_nf("%s L%d%s", jmp_op, n, tail);
	unreachable = 1;
	return 0;
}

void gen_jump(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	output_nf("%s L%d%s", jmp_op, n, tail);
	unreachable = 1;
}

void gen_jfalse(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	if (trackf == NULL)
		error("jf");
	output_nf("b%c%c L%d%s", trackf[2], trackf[3], n, tail);
}

void gen_jtrue(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	if (trackf == NULL)
		error("jt");
	output_nf("b%c%c L%d%s", trackf[0], trackf[1], n, tail);
}

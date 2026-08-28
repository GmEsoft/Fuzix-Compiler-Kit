/* TODO: 6809 changes for stuff like tracking clra and A on bool etc */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6800.h"

unsigned label;		/* Used to hand out local labels of the form X%u */

/*
 *	Start with some simple X versus S tracking
 *	to clean up the local accesses
 */

static uint16_t x_fpoff;
static unsigned x_fprel;
uint8_t a_val;
uint8_t b_val;
unsigned a_valid;
unsigned b_valid;
static unsigned d_valid;
static struct node d_node;
const char *trackf;
const char stdflags[] = "neeq";	/* bne true, beq false */

void invalidate_all(void)
{
	x_fprel = 0;
	a_valid = 0;
	b_valid = 0;
	d_valid = 0;
}

void invalidate_x(void)
{
	x_fprel = 0;
}

/* If we lose tracking on A or B then defacto we lost tracking on D */
void invalidate_a(void)
{
	a_valid = 0;
	d_valid = 0;
}

void invalidate_b(void)
{
	b_valid = 0;
	d_valid = 0;
}

void invalidate_d(void)
{
	d_valid = 0;
}

void invalidate_work(void)
{
	a_valid = 0;
	b_valid = 0;
	d_valid = 0;
}

void invalidate_mem(void)
{
	/* If memory changes it might be an alias to the value cached in AB */
	switch(d_node.op) {
	case T_LREF:
	case T_NREF:
		d_valid = 0;
	}
}

void set_d_node(struct node *n)
{
	memcpy(&d_node, n, sizeof(struct node));
	switch(d_node.op) {
	case T_LSTORE:
		d_node.op = T_LREF;
		break;
	case T_NSTORE:
		d_node.op = T_NREF;
		break;
	case T_RSTORE:
		d_node.op = T_RREF;
		break;
	case T_NREF:
	case T_LREF:
	case T_NAME:
	case T_LOCAL:
	case T_ARGUMENT:
		break;
	default:
		d_valid = 0;
		return;
	}
	d_valid = 1;
}

/* D holds the content pointed to by n */
void set_d_node_ptr(struct node *n)
{
	memcpy(&d_node, n, sizeof(struct node));
	switch(d_node.op) {
	case T_NAME:
		d_node.op = T_NREF;
		break;
	case T_ARGUMENT:
		d_node.value += argbase + frame_len;
		/* Fall through */
	case T_LOCAL:
		d_node.op = T_LREF;
		break;
	default:
		d_valid = 0;
		return;
	}
	d_valid = 1;
}

/* Do we need to check fields by type or will the default filling
   be sufficient ? */
unsigned d_holds_node(struct node *n)
{
	if (d_valid == 0)
		return 0;
	if (d_node.op != n->op)
		return 0;
	if (d_node.snum != n->snum)
		return 0;
	if (d_node.value != n->value)
		return 0;
	return 1;
}

void modify_a(uint8_t val)
{
	a_val = val;
	d_valid = 0;
}

void modify_b(uint8_t val)
{
	b_val = val;
	d_valid = 0;
}

void set_a(uint8_t val)
{
	a_val = val;
	d_valid = 0;
	a_valid = 1;
}

void set_b(uint8_t val)
{
	b_val = val;
	b_valid = 1;
	d_valid = 0;
}

void set_x_node(struct node *n)
{
	invalidate_x();	/* For the moment */
}

void set_ds_node(struct node *n)
{
}

static const char *outflags(register const char *p)
{
	while(isupper(*p)) {
		switch(*p) {
		case 'A':
			invalidate_a();
			break;
		case 'B':
			invalidate_b();
			break;
		case 'D':
			invalidate_work();
			break;
		case 'M':
			invalidate_mem();
			break;
		case 'X':
			invalidate_x();
			break;
		case 'Y':
			/* Y isn't tracked yet */
			break;
		case 'Z':
			trackf = stdflags;
		case 'U':
			/* Not present on 6800 */
		case ' ':
			/* For clarity */
			break;
		default:
			error("ot");
		}
		p++;
	}
	return p;
}

void output_nf(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	p = outflags(p);
	putchar('\t');
	vprintf(p, v);
	putchar('\n');
	va_end(v);
}

void output(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	trackf = NULL;		/* Flags no longer known */
	p = outflags(p);
	putchar('\t');
	vprintf(p, v);
	putchar('\n');
	va_end(v);
}

void outlabel(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	p = outflags(p);
	vprintf(p, v);
	putchar(':');
	putchar('\n');
	va_end(v);
}


void push_d(void)
{
	output_nf("pshb");
	output_nf("psha");
}

void pop_d(void)
{
	output_nf("Apula");
	output_nf("Bpulb");
}

/* FIXME: Generic versions of these made visible and also clear ? */
static void load_d_hireg(void)
{
	if (cpu_has_d)
		output("Dldd @hireg");
	else {
		output("Aldaa @hireg");
		output("Bldab @hireg+1");
	}
}

static void store_d_hireg(void)
{
	if (cpu_has_d)
		output("std @hireg");
	else {
		output("staa @hireg");
		output("stab @hireg+1");
	}
}

static void store_x_hireg(void)
{
	output("stx @hireg");
}

void tfr_u_d(void)
{
	error("ud");
}

void tfr_d_u(void)
{
	error("du");
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
	char *op = "add";
	unsigned l = WORD(v);
	unsigned h = WORD(v >> 16);

	if (v == 0)
		return 1;

	if (cpu_has_y) {
		if (l) {
			add_d_const(l);
			swap_d_y();
			output("Badcb #%u\n", BYTE(h));
			output("Aadca #%u\n", BYTE(h >> 8));
			swap_d_y();
		} else {
			swap_d_y();
			if (BYTE(h))
				add_d_const(h);
			else {
				output("adda #%u", BYTE(h >> 8));
				modify_a(a_val + BYTE(h >> 8));
			}
			swap_d_y();
		}
		return 1;
	}
	/* TODO: still directly fiddles with hireg */
	if (l) {
		add_d_const(l);
		op = "adc";
		if (h == 0) {
			l = ++label;
			output_nf("bcc X%u", l);
			output("inc @hireg+1");
			output("bne X%u", l);
			output("inc @hireg");
			outlabel("X%u", l);
			return 1;
		}
	}
	output("psha");
	if (h & 0xFF) {
		output("ldaa @hireg+1");
		output("%sa #%u", op , BYTE(h));
		output("staa @hireg+1");
		op = "adc";
		if (!(h & 0xFF00)) {
			l = ++label;
			output_nf("bcc X%u", l);
			output("inc @hireg");
			outlabel("X%u", l);
			return 1;
		}
	}
	output("ldaa @hireg");
	output("%sa #%u", op, BYTE(h >> 8));
	output("staa @hireg");
	output("pula");
	return 1;
}

void gen_prologue(const char *name)
{
	unreachable = 0;
	invalidate_all();
	outlabel("%s", name);
}

/* Generate the stack frame */
void gen_frame(unsigned size, unsigned aframe, unsigned t1)
{
	argbase = ARGBASE;
	frame_len = size;
	adjust_s(-size, 0);
}

void gen_epilogue(unsigned size, unsigned argsize)
{
	if (sp)
		error("sp");
	adjust_s(size, (func_flags & F_VOIDRET) ? 0 : 1);
	if (argsize == 0 || cpu_has_d || (func_flags & F_VARARG))
		output_nf("rts");
	else if (argsize <= 8)
		output("jmp __cleanup%u\n", argsize);
	else {
		/* Icky - can we do better remembering AB is live for
		   non void funcs */
		output("jsr __cleanupb");
		output_nf(".word %u\n", argsize);
	}
	unreachable = 1;
}

void gen_cleanup(struct node *n)
{
	uint16_t v = n->right->value;
	sp -= v;
	if (cpu_has_d || n->snum) /* Varargs */
		adjust_s(v, (func_flags & F_VOIDRET) ? 0 : 1);
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

static void argstack(struct node *n)
{
	unsigned s = get_size(n->type);
	unsigned v = n->value;

	/* TODO: review size 4 and long for HC11 - might want to load upper
	   via Y */

	/* If D already holds our object then push it */
	if (d_holds_node(n) && s == 2) {
		gen_push(n);
		return;
	}
	/* TODO: same for X when we get better tracking */

	/* If we are loading a constant 0 then it's a better guess
	   to clra/clrb as it's likely useful to get zeros into A */
	if (s == 2 && n->op == T_CONSTANT && n->value == 0) {
		load_d_const(0);
		gen_push(n);
		return;
	}
	/* If D already holds our value */
	if (s == 2 && n->op == T_CONSTANT && a_valid && b_valid &&
		a_val == (v >> 8) && b_val == BYTE(v)) {
		/* Already in D - need to clean this and tracking up */
		gen_push(n);
		return;
	}
	/* This relies upon off 0 and 2 giving the same answer for can_ */
	if (cpu_has_d && s > 1 && s <= 2 /* FIXME */ && n->op != T_LOCAL && can_load_r_with(n)) {
		/* T_LOCAL can end up with an offset and we don't want
		   an offset are we are not doing a deref */
		load_x_with(n);
		output_nf("pshx");
#if 0
	/* TODO: FIXME */
		if (s == 4) {
			load_x_with(n,2);
			output_nf("pshx");
		}
#endif
		sp += s;
		return;
	}
	codegen_lr(n);
	gen_push(n);
}

void do_fcall(struct node *n)
{
	if (n == NULL)
		return;
	if (n->op == T_ARGCOMMA) {
		/* Recurse down arguments and stack then on the way up */
		do_fcall(n->left);
		argstack(n->right);
	} else {
		argstack(n);
	}
	/* Final node done */
}

void gen_fcall(struct node *n, unsigned vararg)
{
	do_fcall(n);
}

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
void load_d_const(uint16_t n)
{
	register unsigned hi,lo;

	lo = BYTE(n);
	hi = BYTE(n >> 8);

/*	printf(";want %04X have %02X:%02X val %d %d\n",
		n, a_val, b_val, a_valid, b_valid); */

	if (cpu_has_d) {
		if (n == 0) {
			if (!a_valid || a_val)
				output("clra");
			if (!b_valid || b_val)
				output("clrb");
		} else if (!a_valid || !b_valid || a_val != hi || b_val != lo) {
			output("ldd #%u", n);
		}
	} else {
		/* TODO: track AB here and see if we can use existing values */
		lo = n & 0xFF;
		hi = n >> 8;
		if (a_valid == 0 || hi != a_val) {
			if (hi == 0)
				output("clra");
			else if (b_valid && hi == b_val)
				output("tba");
			else if (a_valid == 0 || make_with_op('a', a_val, hi) == 0)
				output("ldaa #%d\n", hi);
		}
		if (b_valid == 0 || lo != b_val) {
			if (lo == 0)
				output("clrb");
			else if (lo == hi)
				output("tab");
			else if (b_valid == 0 || make_with_op('b', b_val, lo) == 0)
				output("ldab #%d\n", lo);
		}
	}
	a_valid = 1;	/* We know the byte values */
	b_valid = 1;
	d_valid = 0;	/* No longer an object reference */
	a_val = hi;
	b_val = lo;

}

void load_a_const(register uint8_t n)
{
	if (a_valid && n == a_val)
		return;
	if (n == 0)
		output("clra");
	else if (b_valid && n == b_val)
		output("tba");
	else if (!a_valid || !make_with_op('a', a_val, n))
		output("ldaa #%u\n", n);
	a_valid = 1;
	a_val = n;
	d_valid = 0;
}

void load_b_const(register uint8_t n)
{
	if (b_valid && n == b_val)
		return;
	if (n == 0)
		output("clrb");
	else if (a_valid && n == a_val)
		output("tab");
	else if (!b_valid || !make_with_op('b', b_val, n))
		output("ldab #%u\n", n);
	b_valid = 1;
	b_val = n;
	d_valid = 0;
}

void add_d_const(register uint16_t n)
{
	if (n == 0)
		return;

	/* TODO: can do better in terms of obj/offset but not clear it is
	   that useful */

	d_valid = 0;

	if (cpu_has_d && (n & 0xFF))
		output("addd #%u\n", n);
	else {
		if (n & 0xFF) {
			output("addb #%u\n", n & 0xFF);
			output("adca #%u\n", n >> 8);
		} else
			output("adda #%u\n", n >> 8);
	}
	if (b_val + (n & 0xFF) < b_val)
		a_val += (n >> 8) + 1;
	else
		a_val += (n >> 8);
	b_val += (n & 0xFF);
}

void add_b_const(register uint8_t n)
{
	if (n == 0)
		return;
	output("addb #%u", n);
	b_val += n;
	d_valid = 0;

}

void load_a_b(void)
{
	output("tba");
	a_val = b_val;
	a_valid = b_valid;
	d_valid = 0;
}

void load_b_a(void)
{
	output("tab");
	b_val = a_val;
	b_valid = a_valid;
	d_valid = 0;
}

void move_s_d(void)
{
	output_nf("sts @tmp");
	if (cpu_has_d)
		output("Dldd @tmp");
	else {
		output("Aldaa @tmp");
		output("Bldab @tmp+1");
	}
	invalidate_work();
}

void move_d_s(void)
{
	if (cpu_has_d)
		output("std @tmp");
	else {
		output("staa @tmp");
		output("stab @tmp+1");
	}
	output_nf("lds @tmp");
}

void load_y_const(uint16_t v)
{
	/* Until we track properly */
	output("Yldy #%u", v);
}

/* Shouldn't get called except on 6809 for now but we will need to move
   the whole 6800 X modifying mess into here later */
void add_x_const(uint16_t)
{
	error("axc");
}

void swap_d_y(void)
{
	/* FIXME: until tracking improves */
	output_nf("DYxgdy");
}

void swap_d_x(void)
{
	/* FIXME: Should really track on the exchange later */
	output_nf("DXxgdx");
}

/* Get D into X (may trash D) */
void make_x_d(void)
{
	if (cpu_has_xgdx)
		swap_d_x();
	else {
		/* FIXME: This is duped a lot */
		if (cpu_has_d)
			output("std @tmp");
		else {
			output("staa @tmp");
			output("stab @tmp+1");
		}
		output("Xldx @tmp");
	}
}

/* Get X into D (may trash X) */
void make_d_x(void)
{
	if (cpu_has_xgdx)
		swap_d_x();
	else {
		output("stx @tmp");
		if (cpu_has_d)
			output("Dldd @tmp");
		else {
			output("Aldaa @tmp");
			output("Bldab @tmp+1");
		}
	}
}

void pop_x(void)
{
	/* Must remember this trashes X, or could make it smart
	   when we track and use offsets of current X then ins ins */
	/* Easier said than done on a 6800 */
	if (cpu_has_pshx)
		output_nf("Xpulx");
	else {
		output_nf("tsx");
		output("Xldx ,x");
		output_nf("ins");
		output_nf("ins");
	}
}

/*
 *	There are multiple strategies depnding on chip features
 *	available.
 */
void adjust_s(register int n, unsigned save_d)
{
	register unsigned abxcost = 3 + 2 * save_d +  n / 255;
	register unsigned hardcost;
	unsigned cost;

	if (cpu_has_d)
		hardcost = 15 + 4 * save_d;
	else
		hardcost = 18 + 2 * save_d;

	cost = hardcost;

	/* Processors with XGDX always have PULX so we use whichever is
	   the shorter of the two approaches */
	if (cpu_has_xgdx) {
		if (n > 14 || n < -14) {
			output_nf("tsx");
			swap_d_x();
			add_d_const(WORD(n));
			swap_d_x();
			output_nf("txs");
			x_fprel = 1;
			x_fpoff = sp;
			return;
		}
		/* As we don't fprel track this */
		invalidate_x();
		if (n > 0) {
			/* Otherwise we know pulx is cheapest */
			repeated_op(n / 2, "Xpulx");
			if (n & 1)
				output_nf("ins");
		}
		if (n < 0) {
			/* pshx likewise is an option */
			repeated_op(-n / 2, "pshx");
			if (n & 1)
				output_nf("des");
		}
		return;
	}

	if (cpu_has_abx && abxcost < hardcost)
		cost = abxcost;
	/* PULX might be fastest */
	if (n > 0 && cpu_has_pshx && (n / 2) + (n & 1) <= cost) {
		repeated_op(n / 2, "Xpulx");
		if (n & 1)
			output_nf("ins");
		return;
	}
	/* If not check if ins is */
	if (n >= 0 && n <= cost) {
		repeated_op(n, "ins");
		return;
	}
	/* ABX is the cheapest option if we have it */
	if (n > 0 && cpu_has_abx && cost == abxcost) {
		/* TODO track b properly when save save_d */
		/* FIXME: need top put S into X and back.. */
		output_nf("\ttsx");
		if (save_d)
			output_nf("pshb");
		if(n > 255) {
			load_b_const(255);
			while(n >= 255) {
				output("abx");
				n -= 255;
				/* TODO: Track here as we add */
			}
		}
		if (n) {
			load_b_const(n);
			output("abx");
		}
		if (save_d)
			output_nf("pulb");
		output_nf("txs");
		x_fprel = 1;
		x_fpoff = 0;
		return;

	}
	/* Forms where ins/des are always best */
	if (n >=0 && n <= 4) {
		repeated_op(n, "ins");
		return;
	}
	if (cpu_has_pshx && n < 0 && -n/2 + (n & 1) <= hardcost) {
		repeated_op(-n/2, "pshx");
		if (n & 1)
			output_nf("des");
		return;
	}
	if (n < 0 && n >= -4) {
		repeated_op(-n, "des");
		return;
	}
	if (optsize) {
		if (n > 0 && n <= 255) {
			output("jsr __addsp8");
			output_nf(".byte %u\n", BYTE(n));
			return;
		}
		if (n <0 && n >= -255) {
			output("jsr __subsp8");
			output_nf(".byte %u", -BYTE(n));
			return;
		}
		output("jsr __modsp16");
		output_nf(".word %u", WORD(n));
		return;
	}
	if (n >=0 && n <= hardcost) {
		repeated_op(n, "ins");
		return;
	}
	if (n < 0 && -n <= hardcost) {
		repeated_op(-n, "des");
		return;
	}
	/* TODO: if we save_d we need to keep and valid */
	/* Inline */

	if (save_d) {
		output("staa @tmp2");
		output("stab @tmp2+1");
	}
	move_s_d();
	add_d_const(n);
	move_d_s();
	if (save_d) {
		output("ldaa @tmp2");
		output("ldab @tmp2+1");
	}
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
	if (cpu_has_d)
		output("%sd %u,x", op, off);
	else {
		output("%sb %u,x", op, off + 1);
		output("%sa %u,x", op2, off);
	}
}

static void op32hi(const char *op2, unsigned off)
{
	if (cpu_has_y) {
		swap_d_y();
		output("%sb %u,x", op2 , off + 1);
		output("%sa %u,x", op2, off);
		swap_d_y();
	} else {
		push_d();
		load_d_hireg();
		output("%sb %u,x", op2 , off + 1);
		output("%sa %u,x", op2, off);
		store_d_hireg();
		pop_d();
	}
}

static void op32_on_ptr(const char *op, const char *op2, unsigned off)
{
	output("%sb %u,x", op, off + 3);
	output("%sa %u,x\n",op2, off + 2);
	op32hi(op2, off);
}

void op32d_on_ptr(const char *op, const char *op2, unsigned off)
{
	if (!cpu_has_d) {
		op32_on_ptr(op, op2, off);
		return;
	}
	output("%sd %u,x", op, off + 2);
	op32hi(op2, off);
}

void load32(register unsigned off)
{
	if (cpu_has_y) {
		output("Yldy %u,x", off);
		output("Dldd %u,x", off + 2);
	} else if (cpu_has_d) {
		output("Dldd %u,x", off);
		store_d_hireg();
		output("Dldd %u,x", off + 2);
	} else {
		output("Aldaa %u,x", off + 2);
		output("Bldab %u,x", off + 3);
		output("Xldx %u,x", off);
		store_x_hireg();
	}
}

void store32(register unsigned off, unsigned nr)
{
	if (cpu_has_y) {
		output("Msty %u,x", off);
		output("Mstd %u,x", off, off + 2);
	} else if (cpu_has_d) {
		output("Mstd %u,x", off + 2);
		load_d_hireg();
		output("Mstd %u,x", off);
		if (nr)
			output("Dldd %u,x", off + 2);
	} else {
		output("Mstab %u,x", off + 3);
		output("Mstaa %u,x", off + 2);
		output_nf("psha");
		/* FIXME: special case hireg still remains */
		output("ldaa @hireg+1");
		output("Mstaa %u,x", off + 1);
		output("ldaa @hireg");
		output("Mstaa %u,x", off);
		output_nf("pula");
	}
}

void uniop_on_ptr(register const char *op, register unsigned off,
						register unsigned size)
{
	off += size;
	while(size--)
		output("%s %u,x", op, --off);
}

/*
 *	Generate a reference via X to a local
 *
 *	TODO: sort out tracking and who owns the fpoff bits so that
 *	our invalidation is consistent.
 */
unsigned make_local_ptr(unsigned off, unsigned rlim)
{
	/* Both relative to frame base */
	int noff = off - x_fpoff;

	printf(";make local ptr off %u, rlim %u noff %u\n", off, rlim, noff);

	/* TODO: if we can d a small < 7 or so shift by decrement then
	   it may beat going via tsx */
	if (x_fprel == 0 ||  noff < 0) {
		output_nf("tsx");
		x_fprel = 1;
		x_fpoff = sp;
	}
	off += x_fpoff;
	if (off <= rlim)
		return off;

	if (cpu_has_xgdx && off - rlim > 5) {
		swap_d_x();
		add_d_const(off);
		swap_d_x();
		x_fpoff -= off;
		return 0;
	}
	/* It is cheaper to inx than mess around with calls for smaller
	   values - 7 or 5 if no save needed */
	if (off - rlim < 7) {
		repeated_op(off - rlim, "inx");
		x_fpoff -= off - rlim;
		return rlim;
	}
	/* These cases push and pop the old D but we don't have a tracking
	   mechanism to optimise it. FIXME one day. The first half of things
	   is fine as we push and load so can optimize, but the resulting
	   case we can currently only invalidate for */
	if (off - rlim < 256) {
		output_nf("pshb");
		load_b_const(off - rlim);
		if (cpu_has_d)	/* And thus ABX */
			output("abx");
		else
			output("jsr __abx");
		output_nf("pulb");
		x_fpoff -= off - rlim;
		return rlim;
	} else {
		/* This case is (thankfully) fairly rare */
		output("jsr __addxconst");
		output_nf(".word %u", off);
		x_fpoff -= off;
		return 0;
	}
}

/* Get pointer to the top of stack. We can optimize this in some cases
   when we track but it will be limited. The 6800 is quite weak on ops
   between register so we sometimes need to build ops against top of stack.

   This needs some care because the stack pointer has already been adjusted
   in codegen-6800 to allow for the removal of the data, thus we actually
   need to offset by the passed size */
static unsigned make_tos_ptr(unsigned s)
{
	printf(";make_tos_ptr %u %u, %u\n", x_fpoff, sp + s, x_fprel);
	/* TODO: we might have X pointing below the stack but in range and that would be just fine */
	if (x_fpoff == sp + s && x_fprel == 1)
		return 0;
	output_nf("Xtsx");
	x_fpoff = sp + s;
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
	else if (s == 3)
		mod = ">";

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
	default:
		error("aform");
	}
	return NULL;
}

/* Those with address forms we can directly load */
unsigned can_load_d_nox(struct node *n)
{
	register unsigned op = n->op;
	if (op == T_CONSTANT || op == T_NAME || op == T_NREF)
		return 1;
	return 0;
}

/* These functions must not touch X on the 6809, they can on others */
unsigned op8_on_node(struct node *r, const char *op, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		off = make_local_ptr(v + off, 255);
		op8_on_ptr(op, off);
		break;
	case T_NSTORE:
		store = 1;
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		output("%sb %s\n", op, addr_form(r, off, 1));
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

/* Do the low byte first in case it's add adc etc */
unsigned op16_on_node(register struct node *r, const char *op, const char *op2, unsigned off)
{
	register unsigned v = r->value;
	unsigned store = 0;

	/* stores are special - they invalidate memory not memory */
	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		off = make_local_ptr(v + off, 254);
		op16_on_ptr(op, op2, off);
		break;
	case T_CONSTANT:
		output("%sb #<%u\n", op, WORD(v + off));
		output("%sa #>%u\n", op2, WORD(v + off));
		break;
	case T_NSTORE:
		store = 1;
	case T_NREF:
		output("%sb %s", op, addr_form(r, off + 1, 1));
		output("%sa %s", op2, addr_form(r, off, 1));
		break;
	case T_NAME:
		output("%sb %s", op, addr_form(r, off, 1));
		output("%sa %s", op2, addr_form(r, off, 3));
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

unsigned op16d_on_node(register struct node *r, const char *op, const char *op2, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	switch(r->op) {
	case T_LSTORE:
		store = 1;
	case T_LREF:
		off = make_local_ptr(v + off, 254);
		op16d_on_ptr(op, op2, off);
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

unsigned op16r_on_node(register struct node *r, const char reg, const char *op, unsigned off)
{
	unsigned v = r->value;
	switch(r->op) {
	case T_LSTORE:
		invalidate_mem();
	case T_LREF:
		off = make_local_ptr(v + off, 254);
		output("%s%c %u,x", op, reg, off);
		break;
	case T_NSTORE:
		invalidate_mem();
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		output("%s%c %s", op, reg, addr_form(r, off, 2));
	default:
		return 0;
	}
	return 1;
}

unsigned op16u_on_node(register struct node *r, const char *op, unsigned off)
{
	error("ur");
	return 0;
}

unsigned op16y_on_node(register struct node *r, const char *op, unsigned off)
{
	return op16r_on_node(r, 'y', op, off);
}

unsigned write_op(register struct node *r, const char *op, const char *op2, unsigned off)
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
	if (s == 2) {
		if (!cpu_has_d)
			return op16_on_node(r, op, op2, off);
		return op16d_on_node(r, op, op2, off);
	}
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
		off = make_local_ptr(v + off, 254);
		uniop_on_ptr(op, off, s);
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

static void op8_on_tos(const char *op)
{
	unsigned off = make_tos_ptr(1);
	output("B%sb %u,x", op, off);
	output_nf("ins");
}

static void op16_on_tos(const char *op)
{
	register unsigned off;
	off = make_tos_ptr(2);
	output("B%sb %u,x", op, off + 1);
	output("A%sa %u,x", op, off);
	output_nf("ins");
	output_nf("ins");
}

static void op16d_on_tos(const char *op)
{
	unsigned off;
	off = make_tos_ptr(2);
	output("D%sd %u,x", op, off);
	output("ins");
	output("ins");
}

/* Only used for operations where there is no ordering requirement */
unsigned write_tos_op(struct node *n, register const char *op)
{
	register unsigned s = get_size(n->type);
	if (s > 2 && !cpu_has_y)
		return 0;
	if (s == 4) {
		swap_d_y();
		/* So that the second 16bit op is correctly offset */
		sp += 2;
		op16_on_tos(op);
		sp -= 2;
		swap_d_y();
		op16_on_tos(op);
	} else if (s == 2)
		op16_on_tos(op);
	else
		op8_on_tos(op);
	/* TODO: we can do better here on tracking ??*/
	/* Right now callers will all invalidate */
	return 1;
}

unsigned write_tos_opd(struct node *n, const char *op, const char *unused)
{
	unsigned s = get_size(n->type);
	if (s > 2)
		return 0;
	else if (s == 2)
		op16d_on_tos(op);
	else
		op8_on_tos(op);
	/* Ditto - tracking */
	return 1;
}

static void uniop8_on_tos(const char *op)
{
	unsigned off = make_tos_ptr(1);
	/* TODO: We can do better here */
	output("B%s %u,x", op, off);
	output_nf("ins");
}

static void uniop16_on_tos(register const char *op)
{
	register unsigned off = make_tos_ptr(2);
	/* TODO: We can do better here */
	output("B%s %u,x", op, off + 1);
	output("A%s %u,x", op, off);
	output_nf("ins");
	output_nf("ins");
}

unsigned write_tos_uniop(struct node *n, register const char *op)
{
	unsigned s = get_size(n->type);
	if (s > 2)
		return 0;
	if (s == 2)
		uniop16_on_tos(op);
	else
		uniop8_on_tos(op);
	return 1;
}

/* TODO: decide how much we inline for -Os */

unsigned left_shift(register struct node *n)
{
	register unsigned s = get_size(n->type);
	register unsigned v;

	if (s > 2 || n->right->op != T_CONSTANT)
		return 0;
	v = n->right->value;
	if (s == 1) {
		if (v >= 8)
			load_b_const(0);
		else
			repeated_op(v, "Blslb");
		return 1;
	}
	if (s == 2) {
		if (v >= 16) {
			load_d_const(0);
			return 1;
		}
		if (v >= 8) {
			load_a_b();
			load_b_const(0);
			v -= 8;
			if (v)
				repeated_op(v, "Alsla");
			return 1;
		}
		while(v--) {
			output("Blslb");
			output("Arola");
		}
		return 1;
	}
	return 0;
}

unsigned right_shift(register struct node *n)
{
	register unsigned s = get_size(n->type);
	register unsigned v;
	register const char *op = "asr";

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
		while(v--)
			output("B%sb", op);
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
					output("B%sb", op);
			}
			return 1;
		}
		while(v--) {
			output("A%sa", op);
			output("Brorb");
		}
		return 1;
	}
	return 0;
}

/* See if we can easily get the value we want into X/Y/U rather than D. Must
   not harm D in the process. We can make this smarter over time if needed.
   Might be worth passing if we can trash D as it will help make_local_ptr
   later, and will be true for some load cases */
unsigned can_load_r_simple(struct node *r)
{
	unsigned s = get_size(r->type);
	if (s == 4)
		return 0;
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		return 1;
	}
	return 0;
}

/* Also allow offset in the result and some level of complexity via
   lea for offsets on things like struct */
unsigned can_load_r_with(struct node *r)
{
	unsigned s = get_size(r->type);
	if (s == 4)
		return 0;
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		return 1;
	}
	return 0;
}

/* For 6800 at least it is usually cheaper to reload even if the value
   we want is in D */
static unsigned load_r_with(char reg, register struct node *r)
{
	register unsigned v = r->value;
	switch(r->op) {
	case T_ARGUMENT:
		v += argbase + frame_len;
	case T_LOCAL:
		/* TODO: will need a Y specific rule for HC11 */
		/* Worst case for size is 252 */
		return make_local_ptr(v, 252);
	case T_LREF:
		v = make_local_ptr(v, 252);
		output("ld%c %u,x", reg, v);
		break;
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
		output("%cld%c %s", reg, addr_form(r, 0, 2));
		break;
	default:
		error("lxw");
	}
	return 0;
}

unsigned load_x_with(struct node *r)
{
	unsigned n = load_r_with('x', r);
	/* Caller owns tracking */
	invalidate_x();
	return n;
}

/* 6809 specific register loading */
unsigned load_u_with(struct node *r)
{
	error("ureg");
	return 0;
}

unsigned cmp_direct(struct node *n, const char *uop, const char *op)
{
	register struct node *r = n->right;
	register unsigned v = r->value;
	register unsigned s = get_size(r->type);

	if (r->op != T_CONSTANT)
		return 0;
	if (r->type & UNSIGNED)
		op = uop;

	/* FIXME: copy extra bits over from 6809 */
	/* If we knoww the upper half matches - eg a byte var that has been expanded then
	   shortcut it. Need to think about this for signed math. I think we can do signed
	   math if we use uop in the size 2 upper match case : TODO*/
	if (s == 1 || (op == uop && a_valid && (v >> 8) == a_val))
		output("BZcmpb #%u", BYTE(v));
	else if (s == 2 && cpu_has_d)
		output("DZsubd #%u", WORD(v));
	else
		return 0;
	output("DZjsr %s", op);
	n->flags |= ISBOOL;
	invalidate_work();
	set_a(0);
	trackf = stdflags;
	return 1;
}

/*
 *	Do fast multiplies were we can
 */

static unsigned do_fast_mul(unsigned s, unsigned n)
{
	if (s > 2)
		return 0;

	switch(n) {
		case 0:
			load_d_const(0);
		case 1:
			return 1;
		case 2:
			output("Blslb");
			output("Arola");
			return 1;
		case 3:
			if (cpu_has_d) {
				output("std @tmp");
				output("Blslb");
				output("Arola");
				output("Daddd @tmp");
				return 1;
			}
			break;
		case 4:
			output("Blslb");
			output("Arola");
			output("Blslb");
			output("Arola");
			return 1;
		case 256:
		case 512:
		case 1024:
		case 2048:
		case 4096:
		case 8192:
			load_a_b();;
			while(n > 256) {
				output("asla");
				n >>= 1;
			}
			load_b_const(0);
			return 1;
	}
	if (optsize)
		return 0;
	switch(n) {
	case 3:		/* 18cyc, 10bytes */
		/* FIXME D form */
		output("staa @tmp");
		output("stab @tmp+1");
		output("Blslb");
		output("Arola");
		output("Baddb @tmp+1");
		output("Aadca @tmp");
		return 1;
	case 5:		/* 22cyc, 12bytes */
		if (cpu_has_d)
			output("std @tmp");
		else {
			output("staa @tmp");
			output("stab @tmp+1");
		}
		output("lslb");
		output("rola");
		output("lslb");
		output("rola");
		if (cpu_has_d)
			output("addd @tmp");
		else {
			output("addb @tmp+1");
			output("adca @tmp");
		}
		return 1;
	case 8:
	case 16:
	case 32:
		while(n >= 2) {
			output("Blslb");
			output("Arola");
			n >>= 1;
		}
		return 1;
	case 64:
		/* Can it be smaller? 20 cycles, 10 bytes */
		/* FIXME: are these worth it */
		output("psha");
		load_b_a();
		output("Bpulb");
		output("Blsrb");
		output("Arora");
		output("Brorb");
		output("Arora");
		output("Brora");
		output("Bandb #$C0");
		return 1;
	case 128:
		output("Alsra");
		load_a_b();
		output("Arora");
		output("Brorb");
		output("Bandb #0x80");
		return 1;
	}
	return 0;
}

unsigned gen_fast_mul(unsigned s, unsigned n)
{
	if (do_fast_mul(s, n)) {
		invalidate_work();
		return 1;
	}
	return 0;
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
			output("Alsra");
			output("Brorb");
			n >>= 1;
		}
	} else {
		/* Round towards zero */
		unsigned m = (n - 1) & 0xFFFF;
		output("tsta");
		output("bpl X%u", ++label);
		add_d_const(m);
		outlabel("X%u", label);
		while(n > 1) {
			output("Aasra");
			output("Brorb");
			n >>= 1;
		}
	}
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
unsigned cmp_op(register struct node *n, const char *uop, const char *op)
{
	unsigned s = get_size(n->right->type);
	if (n->right->type & UNSIGNED)
		op = uop;
	if (s == 1)
		op8_on_tos("Bcmp");
	else if (s == 2 && cpu_has_d)
		op16d_on_tos("Dsub");
	else
		return 0;
	output("jsr %s", op);
	n->flags |= ISBOOL;
	invalidate_work();
	set_a(0);
	trackf = stdflags;
	return 1;
}

unsigned gen_push(struct node *n)
{
	unsigned size = get_size(n->type);
	/* Our push will put the object on the stack, so account for it */
	sp += get_stack_size(n->type);
	switch(size) {
	case 1:
		output_nf("pshb");
		return 1;
	case 2:
		push_d();
		return 1;
	case 4:
		/* TODO: if we have no valid X we should ldx/pshx on 6803 */
		push_d();
		if (cpu_has_y)
			puts("\tpshy");
		else {
			/* Would invalidating and using X be a win on 6800 */
			load_d_hireg();
			push_d();
			invalidate_work();
		}
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
	output("jmp L%d%s", n, tail);
	unreachable = 1;
	return 0;
}

void gen_jump(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	output("jmp L%d%s", n, tail);
	unreachable = 1;
}

void gen_jfalse(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	if (trackf == NULL)
		error("jf");
	output_nf("j%c%c L%d%s", trackf[2], trackf[3], n, tail);
}

void gen_jtrue(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	if (trackf == NULL)
		error("jt");
	output_nf("j%c%c L%d%s", trackf[0], trackf[1], n, tail);
}

/* True if the helper is to be called C style */
static unsigned c_style(struct node *np)
{
	register struct node *n = np;
	/* Assignment is done asm style. No other float used asm helpers
	  should show up as helpcalls but if they do they need to be
	  listed here */
	if (n->op == T_EQ || n->op == T_DEREF)
		return 0;
	/* Float ops otherwise are C style */
	if (n->type == FLOAT)
		return 1;
	n = n->right;
	if (n && n->type == FLOAT)
		return 1;
	return 0;
}

/* FIXME: needs adapting to the changed calling scheme, or better yet
   rewriting the helpers in asm anyway */
/* This works for all the wrong reasons for the moment as the arg is always
   32bit */
void gen_helpcall(struct node *n)
{
	/* Check both N and right because we handle casts to/from float in
	   C call format */
	if (c_style(n))
		gen_push(n->right);
	invalidate_all();
	printf("\t%s __", jsr_op);
}

void gen_helptail(struct node *n)
{
}

void gen_helpclean(struct node *n)
{
	if (c_style(n)) {
		unsigned s = 0;
		if (n->left) {
			s += get_size(n->left->type);
			/* gen_node already accounted for removing this thinking
			   the helper did the work, adjust it back as we didn't */
			sp += s;
		}
		s += get_size(n->right->type);
		/* No helper uses varargs */
		sp -= s;
		/* 6800 expects called code to clean up */
		if (cpu != 6800)
			adjust_s(s, 1);
		/* C style ops that are ISBOOL didn't set the bool flags */
		if (n->flags & ISBOOL)
			output("Ztstb");
	}
}

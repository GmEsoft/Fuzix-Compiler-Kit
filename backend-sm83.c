/*
 *	The gameboy is an 8080 missing a few bits (some useful) and with
 *	Z80isms and some random other stuff thrown in.
 *
 *	TODO: Track HL pointing versus SP to reduce LDHL usage and use inc/dec
 *	TODO: Track if DE and HL match
 *	TODO: Track working register live object
 *	TODO: review volatile handling
 *	TODO: look at what would be involved for "do subtree in HL"
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "compiler.h"
#include "backend.h"
#include "backend-byte.h"

#define BYTE(x)		(((unsigned)(x)) & 0xFF)
#define WORD(x)		(((unsigned)(x)) & 0xFFFF)

/*
 *	So the generic backend knows how to re-type pointers
 */

unsigned target_ptr = UINT;

#define ARGBASE	2	/* Bytes between arguments and locals if no reg saves */

/* Check if a single bit is set or clear */
int bitcheckb1(uint8_t n)
{
	unsigned m = 1;
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (n == m)
			return i;
		m <<= 1;
	}
	return -1;
}

int bitcheck1(unsigned n, unsigned s)
{
	register unsigned i;
	unsigned m = 1;

	if (s == 1)
		return bitcheckb1(n);
	for (i = 0; i < 16; i++) {
		if (n == m)
			return i;
		m <<= 1;
	}
	return -1;
}

int bitcheck0(unsigned n, unsigned s)
{
	if (s == 1)
		return bitcheckb1((~n) & 0xFF);
	return bitcheck1((~n) & 0xFFFF, 2);
}

/*
 *	State for the current function
 */
static unsigned frame_len;	/* Number of bytes of stack frame */
static unsigned sp;		/* Stack pointer offset tracking */
static unsigned argbase;	/* Argument offset in current function */
static unsigned unreachable;	/* Code following an unconditional jump */
static unsigned func_cleanup;	/* Zero if we can just ret out */
static unsigned label;		/* Used to hand out local labels in the form X%u */
static unsigned ccvalid;	/* State of condition codes */
#define CC_UNDEF	0	/* Who knows */
#define CC_VALID	1	/* Matches (Z/NZ) */
#define CC_INVERSE	2	/* Matches the inverse (Z NZ) */

/* Set CC correctly */
static void outputcc(const char *p, ...)
{
	va_list v;
	if (strchr(p, ':') == NULL)
		putchar('\t');
	va_start(v, p);
	vprintf(p, v);
	putchar('\n');
	va_end(v);
	ccvalid = CC_VALID;
}

/* CC other */
static void output(const char *p, ...)
{
	va_list v;
	if (strchr(p, ':') == NULL)
		putchar('\t');
	va_start(v, p);
	vprintf(p, v);
	putchar('\n');
	va_end(v);
	ccvalid = CC_UNDEF;
}

/* CC no effect */
static void outputne(const char *p, ...)
{
	va_list v;
	if (strchr(p, ':') == NULL)
		putchar('\t');
	va_start(v, p);
	vprintf(p, v);
	putchar('\n');
	va_end(v);
}

/* CC inverted but valid */
static void outputinv(const char *p, ...)
{
	va_list v;
	if (strchr(p, ':') == NULL)
		putchar('\t');
	va_start(v, p);
	vprintf(p, v);
	putchar('\n');
	va_end(v);
	ccvalid = CC_INVERSE;
}


/* Start to break stuff out so we can begin to track things later on */

/* Make HL = SP + offset */
static void hl_from_sp(unsigned off)
{
	if (off < 128)
		output("ld hl,sp+%u", off);
	else {
		outputne("ld hl,%u", off);
		output("add hl,sp");
	}
}

static void load_hl(unsigned n)
{
	outputne("ld hl,%u", n);
}

static void load_hl_de(void)
{
	outputne("ld h,d");
	outputne("ld l,e");
}

static void load_de_hl(void)
{
	outputne("ld d,h");
	outputne("ld e,l");
}

static void load_sp_hl(void)
{
	output("ld sp,hl");
}

static void adjust_sp(int size)
{
	if (size < 0) {
		/* 4 bytes */
		if (size < -256) {
			outputne("ld hl,%d", size);
			output("add hl,sp");
			load_sp_hl();
			return;
		}
		/* 2 bytes */
		if (size <= -128) {
			output("add sp,-128");
			size += 128;
		}
		if (size == -1)
			outputne("dec sp");
		else
			output("add sp,%d", size);
	} else if (size > 0) {
		if (size > 254) {
			outputne("ld hl,%u", size);
			output("add hl,sp");
			outputne("ld sp,hl");
			return;
		}
		if(size >= 127) {
			output("add sp,#127");
			size -= 127;
		}
		if (size == 1)
			outputne("inc sp");
		else if (size)
			output("add sp,%u", size);
	}
}

/*
 *	Object sizes
 */

static unsigned get_size(unsigned t)
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
	fprintf(stderr, "type %x\n", t);
	error("gs");
	return 0;
}

#define T_NREF		(T_USER)		/* Load of C global/static */
#define T_CALLNAME	(T_USER+1)		/* Function call by name */
#define T_NSTORE	(T_USER+2)		/* Store to a C global/static */
#define T_LREF		(T_USER+3)		/* Ditto for local */
#define T_LSTORE	(T_USER+4)
#define T_BTST		(T_USER+7)		/* single bit testing */

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

static void swap_op(struct node *n, unsigned op)
{
	struct node *l = n->left;
	n->left = n->right;
	n->right = l;
	n->op = op;
}

/*
 *	Heuristic for guessing what to put on the right. This is very
 *	processor dependent.
 */

static unsigned is_simple(struct node *n)
{
	unsigned op = n->op;

	/* Multi-word objects are never simple */
	if (!PTR(n->type) && (n->type & ~UNSIGNED) > CSHORT)
		return 0;

	/* We can load these directly into a register */
	if (op == T_CONSTANT || op == T_NAME)
		return 10;
	/* We can load this directly into a register but may need xchg pairs */
	if (op == T_NREF)
		return 1;
	return 0;
}

/*
 *	Turn it 8bit - need to enable it everywhere else first
 */
struct node *gen_rewrite(struct node *n)
{
//	byte_label_tree(n, BTF_RELABEL);
	return n;
}

/*
 *	Our chance to do tree rewriting. We don't do much for the 8080
 *	at this point, but we do rewrite name references and function calls
 *	to make them easier to process.
 */
struct node *gen_rewrite_node(struct node *n)
{
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned op = n->op;
	unsigned nt = n->type;

	/* TODO
		- rewrite some reg ops
	*/

	/* Rewrite references into a load operation */
	if (op == T_DEREF) {
		if (r->op == T_LOCAL || r->op == T_ARGUMENT) {
			if (r->op == T_ARGUMENT)
				r->value += argbase + frame_len;
			squash_right(n, T_LREF);
			return n;
		}
		if (r->op == T_NAME) {
			squash_right(n, T_NREF);
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
	}
	/* Eliminate casts for sign, pointer conversion or same */
	if (op == T_CAST && cast_fold_safe(r->op)) {
		if (nt == r->type || (nt ^ r->type) == UNSIGNED ||
		 (PTR(nt) && PTR(r->type))) {
			free_node(n);
			r->type = nt;
			return r;
		}
	}
	/* Rewrite function call of a name into a new node so we can
	   turn it easily into call xyz */
	if (op == T_FUNCCALL && r->op == T_NAME && PTR(r->type) == 1) {
		n->op = T_CALLNAME;
		n->snum = r->snum;
		n->value = r->value;
		free_node(r);
		n->right = NULL;
	}
	/* Commutive operations. We can swap the sides over on these */
	if (op == T_AND || op == T_OR || op == T_HAT || op == T_STAR || op == T_PLUS) {
/*		printf(";left %d right %d\n", is_simple(n->left), is_simple(n->right)); */
		if (is_simple(n->left) > is_simple(n->right)) {
			n->right = l;
			n->left = r;
		}
	}
	/* Turn ++ and -- into easier forms when possible */
	if (op == T_PLUSPLUS && (n->flags & NORETURN))
		n->op = T_PLUSEQ;
	if (op == T_MINUSMINUS && (n->flags & NORETURN))
		n->op = T_MINUSEQ;
	/* Sequence points rock - flip the more akward compares */
	if (op == T_GT)
		swap_op(n, T_LT);
	if (op == T_LTEQ)
		swap_op(n, T_GTEQ);
	return n;
}

/* Export the C symbol */
void gen_export(const char *name)
{
	printf("	.export %s\n", name);
}

void gen_segment(unsigned segment)
{
	switch(segment) {
	case A_CODE:
		printf("\t.%s\n", codeseg);
		break;
	case A_DATA:
		printf("\t.data\n");
		break;
	case A_BSS:
		printf("\t.bss\n");
		break;
	case A_LITERAL:
		printf("\t.literal\n");
		break;
	default:
		error("gseg");
	}
}

/* Generate the function prologue - may want to defer this until
   gen_frame for the most part */
void gen_prologue(const char *name)
{
	output("%s:", name);
	unreachable = 0;
}

/* Generate the stack frame */
void gen_frame(unsigned size, unsigned aframe)
{
	frame_len = size;
	sp = 0;

	if (size)
		func_cleanup = 1;
	else
		func_cleanup = 0;

	argbase = ARGBASE;
	adjust_sp(-size);
}

void gen_epilogue(unsigned size, unsigned argsize)
{
	if (sp != 0)
		error("sp");

	if (unreachable)
		return;

	adjust_sp(size);
	outputne("ret");
}

void gen_label(const char *tail, unsigned n)
{
	unreachable = 0;
	outputne("L%u%s:", n, tail);
}

/* A return statement. We can sometimes shortcut this if we have
   no cleanup to do */
unsigned gen_exit(const char *tail, unsigned n)
{
	if (unreachable)
		return 1;
	if (func_cleanup) {
		gen_jump(tail, n);
		unreachable = 1;
		return 0;
	} else {
		outputne("ret");
		unreachable = 1;
		return 1;
	}
}

void gen_jump(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	/* Force anything deferred to complete before the jump */
	outputne("jr L%u%s", n, tail);
	unreachable = 1;
}

void gen_jfalse(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	switch(ccvalid) {
	case CC_VALID:
		outputne("jr z, L%u%s", n, tail);
		break;
	case CC_INVERSE:
		outputne("jr nz, L%u%s", n, tail);
		break;
	default:
		error("jfu");
	}
}

void gen_jtrue(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	switch(ccvalid) {
	case CC_VALID:
		outputne("jr nz, L%u%s", n, tail);
		break;
	case CC_INVERSE:
		outputne("jr z, L%u%s", n, tail);
		break;
	default:
		error("jtu");
	}
}

/*
 *	Helper handlers. We use a tight format for integers but C
 *	style for float as we'll have C coded float support if any
 */

/* True if the helper is to be called C style */
static unsigned c_style(struct node *np)
{
	register struct node *n = np;
	/* Assignment is done asm style */
	if (n->op == T_EQ)
		return 0;
	/* Float ops otherwise are C style */
	if (n->type == FLOAT)
		return 1;
	n = n->right;
	if (n && n->type == FLOAT)
		return 1;
	return 0;
}

void gen_helpcall(struct node *n)
{
	/* Check both N and right because we handle casts to/from float in
	   C call format */
	if (c_style(n))
		gen_push(n->right);
	printf("\tcall __");
}

void gen_helptail(struct node *n)
{
}

void gen_helpclean(struct node *n)
{
	unsigned s;

	if (c_style(n)) {
		s = 0;
		if (n->left) {
			s += get_size(n->left->type);
			/* gen_node already accounted for removing this thinking
			   the helper did the work, adjust it back as we didn't */
			sp += s;
		}
		s += get_size(n->right->type);
		adjust_sp(s);
		sp -= s;
		/* C style ops that are ISBOOL didn't set the bool flags */
		/* Need to think about keeping bool stuff 8bit here */
		printf(";help clean isbool %04x C\n", n->flags);
		if (n->flags & ISBOOL) {
			output("xor a");
			outputcc("cp e");
		}
	}
	if (n->flags & ISBOOL)
		ccvalid = CC_VALID;
}

void gen_switch(unsigned n, unsigned type)
{
	outputne("ld hl,Sw%u", n);
	printf("\tjp __switch");
	helper_type(type, 0);
	putchar('\n');
}

void gen_switchdata(unsigned n, unsigned size)
{
	outputne("Sw%u:", n);
	outputne(".word %u", size);
}

void gen_case_label(unsigned tag, unsigned entry)
{
	unreachable = 0;
	output("Sw%u_%u:", tag, entry);
}

void gen_case_data(unsigned tag, unsigned entry)
{
	outputne(".word Sw%u_%u", tag, entry);
}

void gen_data_label(const char *name, unsigned align)
{
	outputne("%s:", name);
}

void gen_space(unsigned value)
{
	outputne(".ds %u", value);
}

void gen_text_data(struct node *n)
{
	outputne(".word T%u", n->snum);
}

/* The label for a literal (currently only strings) */
void gen_literal(unsigned n)
{
	if (n)
		outputne("T%u:", n);
}

void gen_name(struct node *n)
{
	outputne(".word %s+%u", namestr(n->snum), WORD(n->value));
}

void gen_value(unsigned type, unsigned long value)
{
	unsigned w = WORD(value);
	if (PTR(type)) {
		outputne(".word %u", w);
		return;
	}
	switch (type) {
	case CCHAR:
	case UCHAR:
		outputne(".byte %u", BYTE(w));
		break;
	case CSHORT:
	case USHORT:
		outputne(".word %u", w);
		break;
	case CLONG:
	case ULONG:
	case FLOAT:
		/* We are little endian */
		outputne(".word %u", w);
		outputne(".word %u", (unsigned) ((value >> 16) & 0xFFFF));
		break;
	default:
		error("unsuported type");
	}
}

void gen_start(void)
{
}

void gen_end(void)
{
}

void gen_tree(struct node *n)
{
	codegen_lr(n);
	outputne(";:");
/*	printf(";SP=%d\n", sp); */
}

/*
 *	Get a local variable
 *
 *	Usually we want to put the variable into DE (or A) but at times it's
 *	useful
 *	to load one into HL (or L). The rule for HL is that an HL load must
 *	not destroy DE but a DE load can destroy HL. Nothing may destroy
 *	BC. Loading HL can destroy A, this needs thought.
 *
 *	For the SM83 it's pretty easy to access stuff within a reasonable
 *	range of SP.
 */
unsigned gen_lref(unsigned v, unsigned size, unsigned to_hl)
{
	printf(";gen_lref offset %u size %u\n", v, size);
	/* Trivial case: if the variable is top of stack then just pop and
	   push it back */
	if (v == 0 && size == 2) {
		if (to_hl) {
			outputne("pop hl");
			outputne("push hl");
		} else {
			outputne("pop de");
			outputne("push de");
		}
		return 1;
	}
	/*
	 *	Shortest forms use ld hl, sp+n for range up to 127
	 *	Longer forms use add hl,sp.
	 */
	if (size <= 2) {
		hl_from_sp(v);		/* We can destroy HL in all cases */
		if (size == 2) {
			if (to_hl) {
				outputne("ldi a,(hl)");
				outputne("ld h,(hl)");
				outputne("ld l,a");
			} else {
				outputne("ld e,(hl)");
				outputne("inc hl");
				outputne("ld d,(hl)");
			}
		} else {
			if (to_hl)
				outputne("ld l,(hl)");
			else
				outputne("ld a,(hl)");
		}
		return 1;
	}
	return 0;
}

/*
 *	Try and generate shorter code for stuff we can directly access
 */

/*
 *	Return 1 if the node can be turned into direct access. The VOID check
 *	is a special case we need to handle stack clean up of void functions.
 */
static unsigned access_direct(struct node *n)
{
	unsigned op = n->op;

	/* We can direct access integer or smaller types that are constants
	   global/static or string labels */
	if (op != T_CONSTANT && op != T_NAME && op != T_NREF && op != T_LREF)
		 return 0;
	if (!PTR(n->type) && (n->type & ~UNSIGNED) > CSHORT)
		return 0;
	return 1;
}

/*
 *	Things we can point HL at.
 */
static unsigned can_point_hl_at(struct node *n)
{
	switch(n->op) {
	case T_NREF:
	case T_NSTORE:
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_LSTORE:
		return 1;
	}
	return 0;
}

/*
 *	Point HL at an object without damaging BCDE
 */
static unsigned point_hl_at(struct node *n)
{
	unsigned v = n->value;
	switch(n->op) {
	case T_NREF:
	case T_NSTORE:
		outputne("ld hl,%s+%u", namestr(n->snum), v);
		break;
	case T_ARGUMENT:
		v += frame_len + argbase;
	case T_LOCAL:
	case T_LREF:
	case T_LSTORE:
		v += sp;
		hl_from_sp(v);
		break;
	default:
		return 0;
	}
	return 1;
}

static void load_via_hl(unsigned r, unsigned s)
{
	if (s == 1) {
		if (r == 'h' || r == 'a')
			outputne("ld a,(hl)");
		else if (r == 'd')
			outputne("ld e,(hl)");
		else error("lhlr1");
	} else if (s == 2) {
		if (r == 'h') {
			outputne("ldi a,(hl)");
			outputne("ld h,(hl)");
			outputne("ld l,a");
		} else if (r == 'd') {
			outputne("ld e,(hl)");
			outputne("inc hl");
			outputne("ld d,(hl)");
		} else error("lhlr2");
	} else if (s == 4) {
		if (r == 'h')
			error("lhlr4");
		outputne("ld e,(hl)");
		outputne("inc hl");
		outputne("ld d,(hl)");
		outputne("inc hl");
		outputne("ld c,(hl)");
		outputne("inc hl");
		outputne("ld b,(hl)");
	} else
		error("lhlrs");
}

static void store_via_hl(unsigned s)
{
	if (s == 1) {
		outputne("ld (hl),a");
	} else {
		outputne("ld (hl),e");
		outputne("inc hl");
		outputne("ld (hl),d");
		if (s == 4) {
			outputne("inc hl");
			outputne("ld (hl),c");
			outputne("inc hl");
			outputne("ld (hl),b");
		}
	}
}

static void store_via_hl_const(struct node *r)
{
	unsigned v = r->value;
	unsigned s = get_size(r->type);

	outputne("ld (hl),%u", BYTE(v));
	if (s >= 2) {
		outputne("inc hl");
		outputne("ld (hl),%u", BYTE(v >> 8));
	}
	if (s == 4) {
		outputne("inc hl");
		outputne("ld (hl),%u", BYTE(r->value >> 16));
		outputne("inc hl");
		outputne("ld (hl),%u", BYTE(r->value >> 24));
	}
}

/*
 *	Get something that passed the access_direct check into a reg. Could
 *	we merge this with the similar hl one in the main table ?
 */
static unsigned load_r_with(const char *r, struct node *n)
{
	unsigned v = WORD(n->value);

	switch(n->op) {
	case T_NAME:
		outputne("ld %s,%s+%u", r, namestr(n->snum), v);
		return 1;
	case T_CONSTANT:
		/* We know this is not a long from the checks above */
		outputne("ld %s,%u", r, v);
		return 1;
	case T_NREF:
		if (*r == 'b')
			return 0;
		point_hl_at(n);
		load_via_hl(*r, 2);
		return 1;
	default:
		return 0;
	}
	return 1;
}

static unsigned load_bc_with(struct node *n)
{
	/* No lref direct to BC option for now */
	return load_r_with("bc", n);
}

static unsigned load_de_with(struct node *n)
{
	if (n->op == T_LREF)
		return gen_lref(n->value + sp, 2, 0);
	return load_r_with("de", n);
}

static unsigned can_load_hl_with(struct node *n)
{
	unsigned op = n->op;
	if (op == T_LREF || op == T_NREF || op == T_NAME || op == T_CONSTANT)
	    	return 1;
	return 0;
}

static unsigned load_hl_with(struct node *n)
{
	if (n->op == T_LREF)
		return gen_lref(n->value + sp, 2, 1);
	return load_r_with("hl", n);
}


static unsigned load_a_with(struct node *n, unsigned keep_hl)
{
	unsigned v = WORD(n->value);
	switch(n->op) {
	case T_CONSTANT:
		/* We know this is not a long from the checks above */
		outputne("ld a,%u", BYTE(v));
		break;
	case T_NREF:
		outputne("ld a,(%s+%u)", namestr(n->snum), v);
		break;
	case T_LREF:
		/* We don't want to trash HL as we may be doing an HL:A op */
		if (keep_hl) {
			sp += 2;
			outputne("push hl");
		}
		hl_from_sp(v + sp);
		outputne("ld a,(hl)");
		if (keep_hl) {
			sp -= 2;
			outputne("pop hl");
		}
		break;
	default:
		return 0;
	}
	return 1;
}

static void repeated_op(const char *o, unsigned n)
{
	while(n--)
		output(o);
}

/*
 *	We split the direct two operand stuff into two forms
 *	- Stuff with DE or A  and constants (in L or HL)
 *	- Anything else with HL or A and (HL)
 */
static unsigned gen_twoop(const char *op, struct node *n, struct node *r, unsigned sign, unsigned s)
{
	char opc[16];
	if (s > 2)
		return 0;
	/* Things you can't point HL at, so we call a differing helper */
	if (r->op == T_CONSTANT || r->op == T_NAME) {
		strcpy(opc, op);
		strcat(opc, "con");
		op = opc;
	}
	if (s == 2) {
		if (r->op == T_CONSTANT)
			outputne("ld hl,%u", WORD(r->value));
		else if (r->op == T_NAME)
			outputne("ld hl,%s+%u", namestr(r->snum), WORD(r->value));
		else if (can_point_hl_at(r)) {
			if (point_hl_at(r) == 0)
				error("cpha");
		} else
			return 0;
	} else if (s == 1) {
		if (r->op == T_CONSTANT)
			outputne("ld l,%u", WORD(r->value));
		else if (r->op == T_NAME)
			outputne("ld l,<%s+%u", namestr(r->snum), WORD(r->value));
		else if (point_hl_at(r) == 0)
			return 0;
		/* For now byte ops are done A,(HL) which works nicely */
	}
	/* twoops are invoked with BCDE as the working and HL pointing to
	   the second argument. con ops are invoked with BCDE as working
	   and HL as the second op. That means any _con op is actually
	   usable as part of a twoop if wanted by fronting it with
	   the relevant loads */
	if (sign)
		helper_s(n, op);
	else
		helper(n, op);
	return 1;
}

static unsigned gen_compc(const char *op, struct node *n, struct node *r, unsigned sign)
{
	/* Comparison sizing comes from the children */
	unsigned s = get_size(r->type);
	if (r->op == T_CONSTANT && r->value == 0 && r->type != FLOAT) {
		char buf[10];
		strcpy(buf, op);
		strcat(buf, "0");
		if (sign)
			helper_s(n, buf);
		else
			helper(n, buf);
		n->flags |= ISBOOL;
		ccvalid = CC_VALID;
		return 1;
	}
	if (gen_twoop(op, n, r, sign, s)) {
		n->flags |= ISBOOL;
		ccvalid = CC_VALID;
		return 1;
	}
	return 0;
}

static int count_mul_cost(unsigned n)
{
	int cost = 4;
	if ((n & 0xFF) == 0) {
		n >>= 8;
		cost += 3;		/* mov mvi */
	}
	while(n > 1) {
		if (n & 1)
			cost += 3;	/* push pop dad d */
		n >>= 1;
		cost++;			/* dad h */
	}
	return cost;
}

/* TODO: mul logic has to allow for 8bit now - so A * n */
/* Write the multiply for any value > 0 */
static void write_mul(unsigned n)
{
	unsigned pops = 0;
	if ((n & 0xFF) == 0) {
		outputne("ld h,l");
		outputne("ld l,0");
		n >>= 8;
	}
	while(n > 1) {
		if (n & 1) {
			pops++;
			outputne("push hl");
		}
		output("add hl,hl");
		n >>= 1;
	}
	while(pops--) {
		outputne("pop de");
		output("add hl,de");
	}
}

static unsigned can_fast_mul(unsigned s, unsigned n)
{
	/* Pulled out of my hat 8) */
	unsigned cost = 15 + 3 * opt;
	/* The base cost of the helper is 6 lxi de, n; call, but this may be too aggressive
	   given the cost of mulde TODO */
	if (optsize)
		cost = 10;
	if (s > 2)
		return 0;
	if (n == 2)
		return 1;
	if (n == 0 || count_mul_cost(n) <= cost)
		return 1;
	return 0;
}

static void gen_fast_mul(unsigned s, unsigned n)
{

	if (n == 0)
		outputne("ld de,0");
	else if (n == 2) {
		output("sla e");
		output("rl d");
	} else {
		load_hl_de();
		write_mul(n);
		load_de_hl();
	}
}

static unsigned gen_fast_div(unsigned s, unsigned n)
{
	return 0;
}

static unsigned gen_fast_udiv(unsigned n, unsigned s)
{
	if (s != 2)
		return 0;
	if (n == 1)
		return 1;
	/* TODO: powers of 2 unsigned are right shifts and should use
	   that helper - translate node as on 6502 ? */
	if (n == 2) {
		output("srl d");
		output("rr e");
	}
	if (n == 256) {
		outputne("ld e,d");
		outputne("ld d,0");
		return 1;
	}
	return 0;
}

static unsigned gen_logicc(struct node *n, unsigned s, const char *op, unsigned v, unsigned code)
{
	unsigned h = (v >> 8) & 0xFF;
	unsigned l = v & 0xFF;

	if (s > 2 || (n && n->op != T_CONSTANT))
		return 0;

	if (s == 2) {
		/* If we are trying to be compact only inline the short ones */
		if (optsize && ((h != 0 && h != 255) || (l != 0 && l != 255)))
			return 0;
		if (h == 0) {
			if (code == 1)
				outputne("ld d,0");
		}
		else if (h == 255 && code != 3) {
			if (code == 2)
				outputne("ld d, 255");
		} else {
			outputne("ld a,d");
			if (code == 3 && h == 255)
				output("cpl");
			else
				output("%s %u", op, h);
			outputne("ld d,a");
		}
		if (l == 0) {
			if (code == 1)
				outputne("ld e,0");
		} else if (l == 255 && code != 3) {
			if (code == 2)
				outputne("ld e,255");
		} else {
			outputne("ld a,e");
			if (code == 3 && l == 255)
				output("cpl");
			else
				output("%s %u", op, l);
			outputne("ld e,a");
		}
	} else {
		outputcc("%s %u", op, l);
	}
	return 1;
}

static unsigned gen_fast_remainder(unsigned n, unsigned s)
{
	unsigned mask;
	if (s != 2)
		return 0;
	if (n == 1) {
		outputne("ld de,0");
		return 1;
	}
	if (n == 256) {
		outputne("ld d, 0");
		return 1;
	}
	if (n & (n - 1))
		return 0;
	if (!optsize) {
		mask = n - 1;
		gen_logicc(NULL, s, "and", mask, 1);
		return 1;
	}
	return 0;
}

static void carry_to_bool(struct node *n)
{
	/* Not xor as we need to keep C */
	outputne("ld a,0");
	outputcc("rla");
	/* Now 0 or 1 if C was set */
	if (!(n->flags & CCONLY)) {
		outputne("ld e,a");
		outputne("ld d,0");
		n->flags |= ISBOOL;
	}
}

/*
 *	If possible turn this node into a direct access. We've already checked
 *	that the right hand side is suitable. If this returns 0 it will instead
 *	fall back to doing it stack based.
 *
 *	If your processor is good at subtracts you may also want to rewrite
 *	constant on the left subtracts in the rewrite rules into some kind of
 *	rsub operator.
 */
unsigned gen_direct(struct node *n)
{
	unsigned s = get_size(n->type);
	struct node *r = n->right;
	unsigned v;
	unsigned nr = n->flags & NORETURN;
	int b;
	unsigned is_byte = (n->flags & (BYTETAIL | BYTEOP)) == (BYTETAIL | BYTEOP);
	unsigned rs;

	/* We only deal with simple cases for now */
	if (r) {
		if (!access_direct(n->right))
			return 0;
		v = r->value;
		rs = get_size(r->type);
	}

	switch (n->op) {
	case T_CLEANUP:
		adjust_sp(v);
		sp -= v;
		return 1;
	case T_NSTORE:
		if (s > 2 && optsize)
			return 0;
		outputne("ld hl,%s+%u", namestr(n->snum), v);
		store_via_hl(s);
		return 1;
	case T_LSTORE:
		if (s > 2 && optsize)
			return 0;
		hl_from_sp(v);
		store_via_hl(s);
		return 1;
	case T_EQ:
		/* We should flip this around in shortcut so the bits
		   are in DE and then we could put the addr in HL */
		/* TODO */
		/* The address is in DE at this point */
		/* TODO: we can do the same as CONSTANT for name/label */
		if (r->op == T_CONSTANT && nr) {
			load_hl_de();
			store_via_hl_const(r);
			return 1;
		}
		/* TODO: once flipped we can sensibly handle all the other
		   cases directly as we can point HL at them */
		/* We can do this via HL and A */
		if (s == 1) {
			if (load_a_with(r, 1) == 0)
				return 0;
			outputne("ld (de),a");
			return 1;
		}
		return 0;
	case T_PLUS:
		/* TODO: we can do these for name/label too */
		if (r->op == T_CONSTANT) {
			if (s == 1) {
				outputcc("add %u", BYTE(v));
				return 1;
			} else  if (v < 4 && s == 2) {
				repeated_op("inc de", v);
				return 1;
			}
		}
		if (s == 1 && point_hl_at(r)) {
			output("add a,(hl)");
			return 1;
		}
		if (s == 2) {
			if (load_hl_with(r) == 0)
				return 0;
			output("add hl,de");
			load_de_hl();
			return 1;
		}
		return 0;
	case T_MINUS:
		if (s == 1) {
			if (point_hl_at(r)) {
				output("sub a,(hl)");
				return 1;
			}
			if (r->op == T_CONSTANT) {
				output("sub a,%u", BYTE(v));
				return 1;
			}
		}
		if (s == 2 && r->op == T_CONSTANT) {
			if (v == 0)
				return 1;
			if (s == 1) {
				outputcc("sub %u", BYTE(v));
				return 1;
			}
			if (v < 6 && s == 2) {
				repeated_op("dec de", v);
				return 1;
			}
			outputne("ld hl,-%u", WORD(v));
			output("add hl,de");
			load_de_hl();
			return 1;
		}
		return 0;
	case T_STAR:
		if (r->op == T_CONSTANT) {
			if (s <= 2 && can_fast_mul(s, v)) {
				gen_fast_mul(s, v);
				return 1;
			}
		}
		return gen_twoop("mul2op", n, r, 0, s);
	case T_SLASH:
		if (r->op == T_CONSTANT && s <= 2) {
			if (n->type & UNSIGNED) {
				if (gen_fast_udiv(s, v))
					return 1;
			} else {
				if (gen_fast_div(s, v))
					return 1;
			}
		}
		return gen_twoop("div2op", n, r, 1, s);
	case T_PERCENT:
		if (r->op == T_CONSTANT && (n->type & UNSIGNED)) {
			if (s <= 2 && gen_fast_remainder(s, v))
				return 1;
		}
		return gen_twoop("rem2op", n, r, 1, s);
	case T_AND:
		/* TODO: bitchecks on the direct andeq/oreq */
		/* Better to use bit for single bit set on word */
		/* TODO byte version */
		if (s == 2 && r->op == T_CONSTANT && (n->flags & CCONLY)) {
			b = bitcheck0(v, s);
			if (b >= 0) {
				/* Single set bit */
				if (b < 8)
					printf("\tres %u,e\n", b);
				else
					printf("\tres %u,d\n", b - 8);
				return 1;
			}
		}
		if (gen_logicc(r, s, "and", r->value, 1))
			return 1;
		return gen_twoop("band2op", n, r, 0, s);
	case T_OR:
		/* Better to use bit for single bit set on word */
		if (s == 2 && r->op == T_CONSTANT && (n->flags & CCONLY)) {
			b = bitcheck1(v, s);
			if (b >= 0) {
				/* Single set bit */
				if (b < 8)
					printf("\tset %u,e\n", b);
				else
					printf("\tset %u,d\n", b - 8);
				return 1;
			}
		}
		if (gen_logicc(r, s, "or", v, 2))
			return 1;
		return gen_twoop("bor2op", n, r, 0, s);
	case T_HAT:
		if (gen_logicc(r, s, "xor", v, 3))
			return 1;
		return gen_twoop("bxor2op", n, r, 0, s);
	case T_EQEQ:
		/* The sizes for comparisons are from the right node as the
		   output is always 1 / 0 */
		if (rs <= 2 && r->op == T_CONSTANT && (n->flags & CCONLY) && !(n->flags & CCFIXED)) {
			if (rs == 1) {
				outputinv("cp %u", BYTE(v));
				n->flags |= ISBOOL;
				return 1;
			}
			if (rs == 2) {
				load_hl(WORD(-v));
				output("add hl,de");
				output("ld a,h");
				outputinv("or l");
				n->flags |= ISBOOL;
				return 1;
			}
		}
		/* For 16bit we should make use of ld de,-nnnn add hl,de
		   ld hl,0 jr z,false inc l or similar */
		return gen_compc("cmpeq", n, r, 0);
	case T_GTEQ:
		/* We can do some nice tricks with these for 8bit */
		/* TODO: Expand 16bit versions of these when not -Os ? */
		if (r->type == UCHAR) {
			if (point_hl_at(r)) {
				outputcc("cp (hl)");
				carry_to_bool(n);
				return 1;
			} else if (r->op == T_CONSTANT) {
				output("cp %u", BYTE(r->value));
				output("ccf");
				carry_to_bool(n);
				return 1;
			}
		}
		return gen_compc("cmpgteq", n, r, 1);
	case T_LT:
		/* We can do some nice tricks with these for 8bit */
		/* TODO: Expand 16bit versions of these when not -Os ? */
		if (r->type == UCHAR) {
			if (point_hl_at(r)) {
				outputcc("cp (hl)");
				carry_to_bool(n);
				return 1;
			} else if (r->op == T_CONSTANT) {
				output("cp %u", BYTE(r->value));
				carry_to_bool(n);
				return 1;
			}
		}
		return gen_compc("cmplt", n, r, 1);
	case T_BANGEQ:
		if (rs == 1 && r->op == T_CONSTANT && (n->flags & CCONLY)) {
			if (rs == 1) {
				outputcc("cp %u", BYTE(v));
				n->flags |= ISBOOL;
				return 1;
			}
			if (rs == 2) {
				load_hl(WORD(-v));
				outputcc("add hl,de");
				output("ld a,h");
				outputcc("or l");
				n->flags |= ISBOOL;
				return 1;
			}
		}
		return gen_compc("cmpne", n, r, 0);
	case T_LTLT:
		if (s == 1 && r->op == T_CONSTANT) {
			repeated_op("add a,a", v & 7);
			return 1;
		}
		if (s == 2 && r->op == T_CONSTANT) {
			if (v >= 8) {
				outputne("ld d,e");
				outputne("ld e,0");
				v = v & 7;
			}
			/* Is it cheaper to go via HL ? */
			if (v > 4) {
				load_hl_de();
				repeated_op("add hl,hl", v);
				load_de_hl();
			} else while(v--) {
				output("sla e");
				output("rl d");
			}
			return 1;
		}
		return gen_twoop("shl2op", n, r, 0, s);
	case T_GTGT:
		/* >> by 8 unsigned */
		if ((n->type & UNSIGNED) && r->op == T_CONSTANT) {
			if (s == 2 && v == 8) {
				outputne("ld e,d");
				outputne("ld d,0");
				return 1;
			}
			if (s == 1) {
				v &= 7;
				while(v--) {
					outputcc("or a,a");
					outputcc("rra");
				}
				return 1;
			}
		}
		return gen_twoop("shr2op", n, r, 1, s);
	case T_PLUSPLUS:
		/* TODO would work better if flipped for HL in these
		   cases ? */
		if (s == 1) {
			outputne("ld a,(de)");
			outputne("ld e,a");
			if (v == 1)
				output("inc a");
			else
				/* Right is always a constant for n++ forms */
				output("add %u", BYTE(v));
			outputne("ld (de),a");
			outputne("ld a,e");
			return 1;
		}
		break;
	case T_PLUSEQ:
		if (s == 1) {
			if (r->op == T_CONSTANT && r->value < 4 && nr) {
				load_hl_de();
				repeated_op("inc (hl)", r->value);
			} else {
				/* May eat HL but not DE */
				if (load_a_with(r, 1) == 0)
					return 0;
				load_hl_de();
				outputcc("add a,(hl)");
				outputne("ld (hl),a");
			}
			return 1;
		}
		if (s == 2 && nr && r->op == T_CONSTANT) {
			if ((r->value & 0x00FF) == 0) {
				outputne("inc de");
				if ((r->value >> 8) < 4) {
					load_hl_de();
					repeated_op("inc (hl)", r->value >> 8);
					return 1;
				}
				outputne("ld a,(de)");
				output("add %u", BYTE(r->value >> 8));
				outputne("ld (de),a");
				return 1;
			}
			if (r->value == 1) {
				load_hl_de();
				output("inc (hl)");
				outputne("jr nz, X%u", ++label);
				outputne("inc hl");
				output("inc (hl)");
				output("X%u:", label);
				return 1;
			}
		}
		return gen_twoop("pluseq2op", n, r, 0, s);
	case T_MINUSMINUS:
		/* Again only for constant case where value is needed */
		if (s == 1) {
			outputne("ld a,(de)");
			outputne("ld l,a");
			/* Not the correct Z flag as we are on previous value */
			output("sub %u", BYTE(v));
			outputne("ld (de),a");
			outputne("ld a,l");
			return 1;
		}
		break;
	case T_MINUSEQ:
		/* Again would be good if could flip sides for EQ ops TODO */
		if (s == 1) {
			/* Shortcut for small 8bit values */
			if (r->op == T_CONSTANT && r->value < 4 && (n->flags & NORETURN)) {
				load_hl_de();
				repeated_op("dec (hl)", v);
			} else {
				/* Subtraction is not transitive so this is
				   messier */
				if (r->op == T_CONSTANT) {
#if 0
	/* TODO needs the sides flipping to work */
					if (r->value == 1) {
						output("dec (de)");
						outputne("ld a,(de)");
					} else
#endif
					{
						outputne("ld a,(de)");
						output("sub %u", BYTE(v));
						outputne("ld (de),a");
					}
				} else {
					if (load_a_with(r, 1) == 0)
						return 0;
					/* Tidy ? */
					output("cpl");
					output("inc a");
					outputne("ld l,a");
					outputne("ld a,(de)");
					output("add l");
					outputne("ld (de),a");
				}
			}
			return 1;
		}
		if (s == 2 && nr && r->op == T_CONSTANT) {
			if ((r->value & 0x00FF) == 0) {
				outputne("inc de");
				if ((v >> 8) < 4) {
					load_hl_de();
					repeated_op("dec (hl)", v >> 8);
					return 1;
				}
				outputne("ld a,(de)");
				output("sub %u", v >> 8);
				outputne("ld (de),a");
				return 1;
			}
			if (r->value == 1) {
				load_hl_de();
				output("dec (hl)");
				outputne("jr nc, X%u", ++label);
				outputne("inc hl");
				output("dec (hl)");
				outputne("X%u:", label);
				return 1;
			}
		}
		return gen_twoop("minuseq2op", n, r, 0, s);
	case T_ANDEQ:
		if (s == 1) {
			if (load_a_with(r, 1) == 0)
				return 0;
			load_hl_de();
			outputcc("and (hl)");
			outputne("ld (hl),a");
			return 1;
		}
		return gen_twoop("andeq2op", n, r, 0, s);
	case T_OREQ:
		if (s == 1) {
			if (load_a_with(r, 1) == 0)
				return 0;
			load_hl_de();
			 outputcc("or (hl)");
			 outputne("ld (hl),a");
			 return 1;
		}
		return gen_twoop("oreq2op", n, r, 0, s);
	case T_HATEQ:
		/* TODO: we need to reverse these higher up so we end up
		   with HL as the pointer automatically */
		if (s == 1) {
			if (load_a_with(r, 1) == 0)
				return 0;
			load_hl_de();
			outputcc("xor (hl)");
			outputne("ld (hl),a");
			return 1;
		}
		return gen_twoop("xoreq2op", n, r, 0, s);
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
	return 0;
}

/* Operators where we can push CCONLY downwards */
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

/* Operators that we known to handle as CCONLY if possible
   TODO: add logic ops as we can BIT many of them */
static unsigned is_cconly(struct node *n)
{
	register unsigned op = n->op;
	if (op == T_EQEQ || op == T_BANGEQ ||
		op == T_ANDAND || op == T_OROR ||
		op == T_BOOL || op == T_BTST)
		return 1;
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
	unsigned sz = get_size(n->type);
	unsigned val;

	l = n->left;
	r = n->right;


/*	printf("; considering %x %x\n", n->op, n->flags); */
	/* Only do this for nodes that are CCONLY. For example if we hit
	   an EQ (assign) then whilst the result of the assign may be
	   CC only, the subtree of the assignment is most definitely not */
	if (n->op != T_AND && !is_cconly(n) && !(n->flags & CCONLY))
		return;

	/* We have to special case BIT unfortunately, and this is ugly */

	/* A common C idiom is if (a & bit) which we can rewrite into
	   bit n,h or bit n,l */

	if (n->op == T_AND) {
/*		printf(";AND %x %x %x\n", n->op, r->op, n->flags); */
		if (r->op == T_CONSTANT && sz == 2) {
			val = bitcheck1(r->value, sz);
			if (val != -1) {
				n->op = T_BTST;
				n->value = val;
				free_node(r);
				n->right = l;
				n->left = NULL;
				r = l;
				l = NULL;
			}
		} else
			return;
	}
	n->flags |= CCONLY;
	/* Deal with the CCFIXED limitations for now */
	if (n->flags & CCFIXED) {
		if (l)
			l->flags |= CCFIXED;
		if (r)
			r->flags |= CCFIXED;
	}
/*	printf(";made cconly %x\n", n->op); */
	/* Are we a node that can CCONLY downwards */
	if (is_ccdown(n)) {
/*		printf(";ccdown of %x L\n", n->op); */
		if (l)
			propogate_cconly(l);
/*		printf(";ccdown cont %x R\n", n->op); */
		if (r)
			propogate_cconly(r);
/*		printf(";ccdown done %x\n", n->op); */
	}
}

/* Perform an operation between (HL) and working. Do some basic optimisations */
static void perform_op_hl(const char *op, const char *os, unsigned s, unsigned nr)
{
	if (s == 1) {
		output("%s (hl)", op);
		outputne("ld (hl),a");
	} else {
		outputne("ld a,(hl)");
		output("%s a,e", op);
		outputne("ldi (hl),a");
		if (!nr)
			outputne("ld e,a");
		outputne("ld a,(hl)");
		output("%s a,d", os);
		if (s == 2) {
			outputne("ld (hl),a");
			if (!nr)
				outputne("ld d,a");
		} else {
			outputne("ldi (hl),a");
			if (!nr)
				outputne("ld d,a");
			outputne("ld a,(hl)");
			output("%s a,c", os);
			outputne("ldi (hl),a");
			if (!nr)
				outputne("ld c,a");
			outputne("ld a,(hl)");
			output("%s a,b", os);
			outputne("ld (hl),a");
			if (!nr)
				outputne("ld b,a");
		}
	}
}

static void perform_op_name(const char *op, const char *op2, unsigned s, unsigned nr, unsigned t, struct node *n)
{
	unsigned v = WORD(n->value);
	if (s == 1) {
		outputne("ld a, <%s+%u", namestr(n->snum), v);
		output("%s a,(hl)", op);
		outputne("ld (hl),a");
		return;
	}
	outputne("ld a, <%s+%u", namestr(n->snum), v);
	output("%s a,(hl)", op);
	outputne("ldi (hl),a");
	if (nr)
		output("ld e,a");
	outputne("ld a, >%s+%u", namestr(n->snum), v + 1);
	output("%s a,(hl)", op2);
	outputne("ld (hl),a");
	if (nr)
		outputne("ld d,a");
}

static void perform_op_label(const char *op, const char *op2, unsigned s, unsigned nr, unsigned t, struct node *n)
{
	unsigned v = WORD(n->value);
	if (s == 1) {
		outputne("ld a, <T%u+%u", n->snum, v);
		output("%s a,(hl)", op);
		outputne("ld (hl),a");
		return;
	}
	outputne("ld a, T%u+%u", n->snum, v);
	output("%s a,(hl)", op);
	outputne("ldi (hl),a");
	if (nr)
		outputne("ld e,a");
	outputne("ld a, T%u+%u", n->snum, v);
	output("%s a,(hl)", op2);
	outputne("ld (hl),a");
	if (nr)
		outputne("ld d,a");
}

/* TODO: rework to avoid ldi when we can just ld if the lower byte is work
   and the upper is not */
static void perform_byte_const(const char *op, unsigned nr, unsigned t, unsigned v, unsigned inc)
{
	unsigned b = BYTE(v);
	/* |= 0 ^=0 are no ops */
	if (b == 0 &&  (t == 2 || t == 3)) {
		if (!nr) {
			if (inc)
				outputne("ldi a,(hl)");
			else
				outputne("ld a,(hl)");
		} else if (inc)
			outputne("inc hl");
		return;
	}
	/* and 0xFF is a no op */
	if (b == 255 && t == 1) {
		if (!nr) {
			if (inc)
				outputne("ldi a,(hl)");
			else
				outputne("ld a,(hl)");
		} else if (inc)
			outputne("inc hl");
		return;
	}
	/* or 0xFF is 0xFF */
	if (b == 255 && t == 2)
		outputne("ld a,255");
	else if (b == 0 && t == 1)
		output("xor a");
	else {
		/* In type 0 case (add/adc etc we can't xor due to flags */
		if (b == 0 && t)
			output("xor a");
		else
			outputne("ld a, %u", b);
		outputne("%s a,(hl)", op);
	}
	if (inc)
		outputne("ldi (hl),a");
	else
		outputne("ld (hl),a");
}

/* TODO: for the add/adc case we should avoid adding 0 and push the primary
   op up as many bytes as possible when only updating high values. We also
   need to catch inc (hl) stuff */
static void perform_op_const(const char *op, const char *op2, unsigned s, unsigned nr, unsigned t, unsigned long v)
{
	if (s == 1) {
		perform_byte_const(op, nr, t, BYTE(v), 0);
		return;
	}
	if (s == 2) {
		perform_byte_const(op, nr, t, v, 1);
		if (!nr)
			outputne("ld e,a");
		perform_byte_const(op2, nr, t, v >> 8, 0);
		if (!nr)
			outputne("ld d,a");
		return;
	}
	perform_byte_const(op, nr, t, v, 1);
	if (!nr)
		outputne("ld e,a");
	perform_byte_const(op2, nr, t, v >> 8, 1);
	if (!nr)
		outputne("ld d,a");
	perform_byte_const(op2, nr, t, v >> 16, 1);
	if (!nr)
		outputne("ld c,a");
	perform_byte_const(op2, nr, t, v >> 24, 0);
	if (!nr)
		outputne("ld b,a");
}

/* Look to optimise ops by doing the other side first */
static unsigned backop(struct node *n, const char *op, const char *os, unsigned t, unsigned nr)
{
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned s = get_size(n->type);
	/* If we can quickly load the object being referenced into DE then
	   do so */
	if (can_load_hl_with(l)) {
		printf("; backop l %04X r %04X\n", l->op, r->op);
		/* TODO: optimised special rules for inc (hl) forms for word */
		if (r->op == T_CONSTANT) {
			load_hl_with(l);
			if (t == 0 && nr && s == 1 && r->value <= 4)
				output("inc (hl)");
			else
				perform_op_const(op, os, s, nr, t, r->value);
			return 1;
		}
		if (r->op == T_NAME && s <= 2) {
			load_hl_with(l);
			perform_op_name(op, os, s, nr, t, r);
			return 1;
		}
		printf("; backop via codegen\n");
		codegen_lr(r);
		if (s == 1) {
			outputne("push af");
			sp += 2;
		}
		load_hl_with(l);
		if (s == 1) {
			sp -= 2;
			output("pop af");
		}
		perform_op_hl(op, os, s, nr);
		return 1;
	}
	/* The other useful case is that the right hand side is short form
	   and then gen_direct will pick it up. We don't hava general
	   'do tree in HL' although maybe we should */
	return 0;
}

/*
 *	Allow the code generator to short cut any subtrees it can directly
 *	generate.
 */
unsigned gen_shortcut(struct node *n)
{
	unsigned s = get_size(n->type);
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned nr = n->flags & NORETURN;
	unsigned op = n->op;

	/* Unreachable code we can shortcut into nothing whee.be.. */
	if (unreachable)
		return 1;

	/* Try and rewrite this node subtree for CC only */
	if (n->flags & CCONLY)
		propogate_cconly(n);

	/* The comma operator discards the result of the left side, then
	   evaluates the right. Avoid pushing/popping and generating stuff
	   that is surplus */

	switch(op) {
	case T_COMMA:
		l->flags |= NORETURN;
		codegen_lr(l);
		/* Parent determines child node requirements */
		r->flags |= nr;
		codegen_lr(r);
		return 1;
	/* We don't know if the result has set the condition flags
	 * until we generate the subtree. So generate the tree, then
	 * either do nice things or use the helper */
	case T_BOOL:
		codegen_lr(r);
		if (r->flags & ISBOOL)
			return 1;
		s = get_size(r->type);
		if (s <= 2 && (n->flags & CCONLY)) {
			if (ccvalid == CC_UNDEF) {
				if (s == 2 && !(n->flags & BYTEOP)) {
					outputne("ld a,d");
					outputcc("or e");
				} else
					outputcc("or a");
			}
			return 1;
		}
		/* TODO: Can we get the case where we have a bool of a cc */
		/* If we will need to turn a CC into a value */
		/* Too big or value needed */
		n->flags |= ISBOOL;
		helper(n, "bool");
		ccvalid = CC_VALID;
		return 1;
	case T_BANG:
		codegen_lr(r);
		if (r->flags & ISBOOL) {
			printf(";BOOL - invert cc from %d\n", ccvalid);
			if (ccvalid == CC_INVERSE)
				ccvalid = CC_VALID;
			else if (ccvalid == CC_VALID)
				ccvalid = CC_INVERSE;
			else
				error("bangcc");
			return 1;
		}
		s = get_size(r->type);
		if (s <= 2 && (n->flags & CCONLY) && !(n->flags & CCFIXED)) {
			printf(";BOOL - process cc from %d\n", ccvalid);
			if (ccvalid == CC_INVERSE)
				ccvalid = CC_VALID;
			else if (ccvalid == CC_VALID)
				ccvalid = CC_INVERSE;
			else if (ccvalid == CC_UNDEF) {
				if (s == 2 && !(n->flags & BYTEOP)) {
					outputne("ld a,d");
					outputinv("or e");
				} else
					outputinv("or a");
			}
			return 1;
		}
		/* Too big or value needed */
		n->flags |= ISBOOL;
		helper(n, "not");
		ccvalid = CC_VALID;
		return 1;
	case T_LSTORE:
		/* The common foo = const case we want to shortcut */
		if (nr && r->op == T_CONSTANT) {
			hl_from_sp(n->value);
			store_via_hl_const(r);
			return 1;
		}
		return 0;
	/* EQ ops are best done backwards in many cases */
	case T_ANDEQ:
		return backop(n, "and", "and", 1, nr);
	case T_OREQ:
		return backop(n, "or", "or", 2, nr);
	case T_HATEQ:
		return backop(n, "xor", "xor", 3, nr);
	case T_PLUSEQ:
		if (s == 2 && l->op == T_LOCAL && l->value == 0 &&
			r->op == T_CONSTANT && r->value <= 4 && sp == 0) {
			outputne("pop de");
			repeated_op("inc de", r->value);
			outputne("push de");
			return 1;
		}
		return backop(n, "add", "adc", 0, nr);
	case T_MINUSEQ:
		if (s == 2 && n->op == T_MINUSEQ && l->op == T_LOCAL && l->value == 0 &&
			r->op == T_CONSTANT && r->value <= 4 && sp == 0) {
			outputne("pop de");
			repeated_op("dec de", r->value);
			outputne("push de");
			return 1;
		}
		return 0;
	}
	return 0;
}

/* Stack the node which is currently in the working register */
unsigned gen_push(struct node *n)
{
	unsigned size = get_size(n->type);

	/* Our push will put the object on the stack, so account for it */
	sp += size;

	switch(size) {
	case 1:
		outputne("push af");
		outputne("inc sp");
		return 1;
	case 2:
		outputne("push de");
		return 1;
	case 4:
		outputne("push bc");
		outputne("push de");
		return 1;
	default:
		return 0;
	}
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

	/* Propogate boolean state through casts to clean up after
	   byte optimizer */
	if ((n->right->flags & ISBOOL) && ls <= 2)
		n->flags |= ISBOOL;

	/* No type casting needed as computing byte sized */
	if (n->flags & BYTEOP)
		return 1;

	rs = get_size(rt);

	/* Size shrink is not always free for us as it's a reg change */
	if (ls < rs) {
		/* Going from integer to char is a move into a */
		if (rs > 1 && ls == 1)
			outputne("ld a,e");
		return 1;
	}
	if (ls == rs)
		return 1;
	/* Don't do the harder ones */
	if (!(rt & UNSIGNED))
		return 0;
	if (rs == 1) {
		outputne("ld e,a");
		outputne("ld d,0");
	}
	if (ls == 4)
		outputne("ld bc,0");
	return 1;
}

unsigned gen_node(struct node *n)
{
	unsigned size = get_size(n->type);
	unsigned v;
	unsigned nr = n->flags & NORETURN;
	unsigned se = n->flags & SIDEEFFECT;

	/* We adjust sp so track the pre-adjustment one too when we need it */
	v = n->value;

	/* An operation with a left hand node will have the left stacked
	   and the operation will consume it so adjust the stack.

	   The exception to this is comma and the function call nodes
	   as we leave the arguments pushed for the function call */

	if (n->left && n->op != T_ARGCOMMA && n->op != T_CALLNAME && n->op != T_FUNCCALL)
		sp -= get_size(n->left->type);

	switch (n->op) {
		/* Load from a name */
	case T_NREF:
		if (nr && !se)
			return 1;
		if (size == 1) {
			outputne("ld a,(%s+%u)", namestr(n->snum), v);
			return 1;
		}
		point_hl_at(n);
		load_via_hl('d', size);
		return 1;
	case T_LREF:
		/* We are loading something then not using it, and it's local
		   so can go away */
		/* printf(";L sp %u %s(%ld)\n", sp, namestr(n->snum), n->value); */
		if (nr)
			return 1;
		v += sp;
		if (size == 4) {
			hl_from_sp(v);
			load_via_hl('d', size);
			return 1;
		}
		return gen_lref(v, size, 0);
	case T_NSTORE:
		if (size == 1)
			outputne("ld (%s+%u),a", namestr(n->snum), v);
		else {
			point_hl_at(n);
			store_via_hl(size);
		}
		return 1;
	case T_LSTORE:
		v += sp;
		/* Store A or DE to local */
		if (size == 1) {
			hl_from_sp(v);
			outputne("ld (hl),a");
			return 1;
		}
		/* Word is tricker */
		if (v == 0 && size == 2 ) {
			output("pop af");
			outputne("push de");
			return 1;
		}
		hl_from_sp(v);
		store_via_hl(size);
		return 1;
		/* Call a function by name */
	case T_CALLNAME:
		output("call %s+%u", namestr(n->snum), v);
		return 1;
	case T_EQ:
		/* (TOS) = hl and (TOS) = a */
		outputne("pop hl");
		store_via_hl(size);
		return 1;
	case T_DEREF:
		if (nr && !se)
			return 1;
		/* Get (de) */
		if (size == 1) {
			outputne("ld a,(de)");
			return 1;
		}
		load_hl_de();
		load_via_hl('d', size);
		return 1;
	case T_FUNCCALL:
		output("call __callde");
		return 1;
	case T_CONSTANT:
		if (nr)
			return 1;
		switch(size) {
		case 4:
			outputne("ld bc,%u", WORD(n->value >> 16));
		case 2:
			outputne("ld de,%u", WORD(v));
			return 1;
		case 1:
			outputne("ld a,%u", BYTE(v));
			return 1;
		}
		break;
	case T_NAME:
		if (nr)
			return 1;
		return load_de_with(n);
	case T_ARGUMENT:
		v += frame_len + argbase;
	case T_LOCAL:
		if (nr)
			return 1;
		v += sp;
		hl_from_sp(v);
		/* This one is uglier using DE as working but rarely happens
		   and once we fix LDEREF/LSTORE etc for long pretty much
		   won't happen */
		load_de_hl();
		return 1;
	case T_CAST:
		if (nr)
			return 1;
		return gen_cast(n);
	case T_PLUS:
		if (size == 1) {
			outputne("pop de");
			outputcc("add a,e");	/* Check where push af put it */
			return 1;
		} else if (size <= 2) {
			outputne("pop hl");
			output("add hl,de");
			load_de_hl();
			return 1;
		}
		break;
	case T_BANG:
		if ((n->flags & CCONLY) && !(n->flags & CCFIXED)) {
			n->flags |= ISBOOL;
			if (ccvalid) {
				/* Just remember flags are reversed */
				ccvalid = 3 - ccvalid;
				return 1;
			}
			if (size == 1) {
				outputinv("or a");
				return 1;
			}
			if (size == 2) {
				outputne("ld a,d");
				outputinv("or e");
				return 1;
			}
		}
		printf(";not cconly - %x\n", n->flags);
	case T_BTST:
		/* Always CCONLY */
		if (v < 8)
			printf("\tbit %u, e\n", v);
		else
			printf("\tbit %u, d\n", v - 8);
		return 1;
	case T_TILDE:
		if (size == 1) {
			outputcc("cpl");
			return 1;
		}
		break;
	case T_NEGATE:
		if (size == 1) {
			output("cpl");
			outputcc("inc a");
			return 1;
		}
		break;
	case T_PLUSEQ:
		/* Deal with the case of complex += complex for byte */
		if (size == 1) {
			outputne("pop hl");
			output("add (hl)");
			output("ld (hl),a");
			return 1;
		}
		break;
	}
	return 0;
}

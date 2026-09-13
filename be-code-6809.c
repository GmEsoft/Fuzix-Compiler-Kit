#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6809.h"

static unsigned op8r_on_node(struct node *r, char reg, const char *op, unsigned off);
static unsigned can_load_r8_simple(struct node *r);
static unsigned load_r_with(char reg, struct node *r);

static void dump_tracking(void);

unsigned label;		/* Used to hand out local labels in the form X%u */

/*
 *	Start with some simple X versus S tracking
 *	to clean up the local accesses
 */

#define R_D	0
#define R_X	1
#define R_Y	2
#define R_U	3
#define R_DS	4		/* D store track */
#define NR_R	5

static struct node rnode[NR_R];
/* We track A & B constant specially */
uint8_t a_val;
uint8_t b_val;
unsigned a_valid;
unsigned b_valid;
const char *trackf;
const char stdflags[] = "neeq";	/* bne true, beq false */
const char fixedflags[] = "raxx";	/* bra true, nothing false */
unsigned cc_invert;

void invalidate_all(void)
{
	rnode[R_D].op = 0;
	rnode[R_DS].op = 0;
	rnode[R_X].op = 0;
	rnode[R_Y].op = 0;
	rnode[R_U].op = 0;
	a_valid = 0;
	b_valid = 0;
}

/* Invalidate the node tracking for D. Doesn't however
   invalidate constant tracking */
static void inv_d_node(void)
{
	rnode[R_D].op = 0;
	rnode[R_DS].op = 0;
}

void invalidate_x(void)
{
	rnode[R_X].op = 0;
}

void invalidate_y(void)
{
	rnode[R_X].op = 0;
}

void invalidate_u(void)
{
	rnode[R_X].op = 0;
}

/* If we lose tracking on A or B then defacto we lost tracking on D */
void invalidate_a(void)
{
	a_valid = 0;
	inv_d_node();
}

void invalidate_b(void)
{
	b_valid = 0;
	inv_d_node();
}

/* Invalidate D entirely including constant tracking. Also Y as we might
   be working in longs. This isn't ideal and wants optimizing more */
void invalidate_work(void)
{
	inv_d_node();
	a_valid = 0;
	b_valid = 0;
	rnode[R_Y].op = 0;
}

/* Any registers that are copies of a variable might now be wrong as
   we wrote through a pointer */
void invalidate_mem(void)
{
	struct node *n = rnode;
	while(n < rnode + NR_R) {
		if (n->op == T_LREF || n->op == T_NREF)
			n->op = 0;
		n++;
	}
}

/* This will get more complex when we try and tackle relational compares */
void set_z_flag(unsigned n)
{
	cc_invert = n;
	trackf = stdflags;
}

/* Z flag definitively known */
static void set_z_known(unsigned n)
{
	cc_invert = n;
	trackf = fixedflags;
}

void set_r_node(register struct node *r, struct node *n)
{
	/* Don't track 32bit types yet */
	if (get_size(n->type) > 2) {
		invalidate_work();
		return;
	}
	memcpy(r, n, sizeof(struct node));
	switch(r->op) {
	case T_LSTORE:
		r->op = T_LREF;
		break;
	case T_NSTORE:
		r->op = T_NREF;
		break;
	case T_RSTORE:
		r->op = T_RREF;
		break;
	case T_LREF:
	case T_NREF:
	case T_RREF:
	case T_NAME:
	case T_LOCAL:
	case T_ARGUMENT:
		break;
	default:
		r->op = 0;
		/* Any constant form on D is still fine */
		return;
	}
}

/* D holds the content pointed to by n */
void set_r_node_ptr(register struct node *r, struct node *n)
{
	memcpy(r, n, sizeof(struct node));
	switch(r->op) {
	case T_NAME:
		r->op = T_NREF;
		break;
	case T_ARGUMENT:
		r->value += argbase + frame_len;
		/* Fall through */
	case T_LOCAL:
		r->op = T_LREF;
		break;
	default:
		r->op = 0;
		return;
	}
}

/* Do we need to check fields by type or will the default filling
   be sufficient ? */

/* TODO: Constants for D need separate handling */
unsigned r_holds_node(struct node *r, struct node *n)
{
	if (r->op == 0)
		return 0;
	if (r->op != n->op)
		return 0;
	if (r->snum != n->snum)
		return 0;
	if (r->value != n->value)
		return 0;
	return 1;
}

void set_d_node(struct node *n)
{
	set_r_node(rnode + R_D, n);
	a_valid = b_valid = 0;
	if (n->op == T_CONSTANT) {
		set_b(BYTE(n->value));
		if (get_size(n->type) == 2)
			set_a(BYTE(WORD(n->value) >> 8));
	}
	rnode[R_DS].op = 0;
}

void set_d_node_ptr(struct node *n)
{
	set_r_node_ptr(rnode + R_D, n);
	rnode[R_DS].op = 0;
}

unsigned d_holds_node(struct node *n)
{
	if (r_holds_node(rnode + R_D, n)) {
//		printf(";D was n %04X %04X\n", rnode[R_D].op, WORD(rnode[R_D].value));
		return 1;
	}
	if (r_holds_node(rnode + R_DS, n)) {
//		printf(";DS was n %04X %04X\n", rnode[R_D].op, WORD(rnode[R_D].value));
		return 1;
	}
	return 0;
}

unsigned d_type_constant(void)
{
	if (a_valid && b_valid)
		return 1;
	return 0;
}

unsigned d_const_value(void)
{
	return (a_val << 8) | b_val;
}

void modify_a(uint8_t val)
{
	a_val = val;
	inv_d_node();
}

void modify_b(uint8_t val)
{
	b_val = val;
	inv_d_node();
}

void set_a(uint8_t val)
{
	a_val = val;
	a_valid = 1;
	inv_d_node();
}

void set_b(uint8_t val)
{
	b_val = val;
	b_valid = 1;
	inv_d_node();
}

static void set_r_to(unsigned r, uint16_t v)
{
	register struct node *rp = rnode + r;
	if (r == R_D) {
		set_a(v >> 8);
		set_b(BYTE(v));
	}
	rp->op = T_CONSTANT;
	rp->value = v;
	rp->snum = 0;
}

static void modify_r_by(unsigned r, uint16_t v)
{
	register struct node *rp = rnode + r;
	if (r == R_D) {
		uint16_t n = (a_val << 8) | b_val;
		n += v;
		b_val = BYTE(n);
		if (a_valid && b_valid)
			a_val = BYTE(n >> 8);
	}
	rp->op = T_CONSTANT;
	rp->value = v;
	rp->snum = 0;
}

/* Make sure the D node properly reflects the A and B tracking state */
static void sync_d_in(void)
{
	/* If the bytes are valid make this a constant mode */
	if (a_valid && b_valid)
		set_r_to(R_D, (a_val << 8) | b_val);
	/* If they are not and it was still marked constant undo it */
	else if (rnode[R_D].op == T_CONSTANT)
		rnode[R_D].op = 0;
}

/* We use this if we have copied data into D from X Y or U and we need
   to update the A and B tracking. If it's called at other times it will
   do the wrong thing */
static void sync_d_out(void)
{
	if (rnode[R_D].op == T_CONSTANT) {
		set_a(rnode[R_D].value >> 8);
		set_b(rnode[R_D].value);
	} else if (rnode[R_DS].op == T_CONSTANT) {
		set_a(rnode[R_DS].value >> 8);
		set_b(rnode[R_DS].value);
	} else {
		a_valid = 0;
		b_valid = 0;
	}
}

static void track_swap(unsigned a, unsigned b)
{
	struct node tmp;
	/* TODO: if we only have values in DS as sources we should use
	   them, but that needs a bunch of tricky invalidation logic */
	if (a == R_D || b == R_D)
		sync_d_in();
	memcpy(&tmp, rnode + a, sizeof(struct node));
	memcpy(rnode + a, rnode + b, sizeof(struct node));
	memcpy(rnode + b, &tmp, sizeof(struct node));
	sync_d_out();
	/* If we wrote to D then DS is now invalid */
	/* We never swap with DS at this point */
	if (a == R_D || b == R_D)
		rnode[R_DS].op = 0;
}

static void track_copy(unsigned a, unsigned b)
{
	/* If D is unknown try DS */
	if (a == R_D && rnode[R_D].op == 0)
		a = R_DS;
	/* Ensure the constant state for A B tracking is correct */
	if (a == R_D || a == R_DS)
		sync_d_in();
	memcpy(rnode + b, rnode + a, sizeof(struct node));
	/* Update constant tracking, and also deal with the fact that
	   a copy to D invalidates DS */
	if (b == R_D)
		rnode[R_DS].op = 0;
	if (b == R_DS)
		rnode[R_D].op = 0;
	sync_d_out();
}

void set_x_node(struct node *n)
{
	set_r_node(rnode + R_X, n);
}

void set_u_node(struct node *n)
{
	set_r_node(rnode + R_U, n);
}

void set_ds_node(struct node *n)
{
	set_r_node(rnode + R_DS, n);
	dump_tracking();
}

void set_ds_node_ptr(struct node *n)
{
	set_r_node_ptr(rnode + R_DS, n);
}

/* Icky so really need to work this back out */
static unsigned to_r(char c)
{
	if (c == 'd')
		return R_D;
	if (c == 'x')
		return R_X;
	if (c == 'y')
		return R_Y;
	if (c == 'u')
		return R_U;
	error("2r");
	return 0;
}

static unsigned dump_r(struct node *n, char *r)
{
	unsigned v = n->value;

	if (n->op == 0)
		return 0;

	printf(";%s:", r);
	switch(n->op) {
	case T_LOCAL:
		printf("LOCAL %X ", v);
		break;
	case T_ARGUMENT:
		printf("ARG %X ", v);
		break;
	case T_LREF:
		printf("LREF %X ", v);
		break;
	case T_NAME:
		printf("NAME _%s ", namestr(n->snum));
		break;
	case T_NREF:
		printf("NREF _%s ", namestr(n->snum));
		break;
	case T_RREF:
		printf("RREF");
		break;
	default:
		printf("BAD %04X ", n->op);
		break;
	}
	return 1;
}

static void dump_tracking(void)
{
#ifdef DEBUG_TRACKING
	unsigned any = 0;
	if (a_valid) {
		printf(";A=%02X", a_val);
		any = 1;
	}
	if (b_valid) {
		printf(";B=%02X", b_val);
		any = 1;
	}

	any |= dump_r(rnode + R_D, "D");
	any |= dump_r(rnode + R_DS, "DS");
	any |= dump_r(rnode + R_X, "X");
	any |= dump_r(rnode + R_Y, "Y");
	any |= dump_r(rnode + R_U, "U");
	if (trackf) {
		printf(";F:%s", trackf);
		any = 1;
	}
	if (any)
		printf("\n");
#endif
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
		case 'U':
			/* Not tracked yet */
			invalidate_u();
			break;
		case 'X':
			invalidate_x();
			break;
		case 'Y':
			invalidate_y();
			break;
		case 'Z':
			set_z_flag(0);
			break;
		case ' ':
			break;
		default:
			printf("%c", *p);
			error("ot");
		}
		p++;
	}
	return p;
}

static char buf[81];

void output_nf(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	vsnprintf(buf, 80, p, v);
	p = outflags(buf);
	putchar('\t');
	puts(p);
	va_end(v);
	dump_tracking();
}

void output(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	vsnprintf(buf, 80, p, v);
	trackf = NULL;		/* Flags no longer known */
	p = outflags(buf);
	putchar('\t');
	puts(p);
	va_end(v);
	dump_tracking();
}

void outlabel(const char *p, ...)
{
	va_list v;
	va_start(v, p);
	vprintf(p, v);
	putchar(':');
	putchar('\n');
	va_end(v);
	dump_tracking();

}

/* Management of function call stacks. This gets a bit hairy as we want
   to optimize for the multi-register push */

#define RUSED_A		1
#define RUSED_B		2
#define RUSED_X		4
#define RUSED_Y		8
#define RUSED_U		16

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
static uint8_t astk_s[NCALL];		/* Size of arg we didn't push */
static uint8_t *astk = astk_s;

void push_d(void)
{
	output_nf("pshs d");
}

void pop_d(void)
{
	output_nf("Dpuls d");
}

void tfr_u_d(void)
{
	if (d_holds_node(rnode + R_U)) {
		printf(";tfr u,d removed\n");
		return;
	}
	/* tfr u,d does not set flags but it does make any existing
	   flag state no longer reflect D */
	output("tfr u,d");
	track_copy(R_U, R_D);
}

void tfr_d_u(void)
{
	if (r_holds_node(rnode + R_U, rnode + R_D)) {
		printf("; tfr d,u removed\n");
		return;
	}
	output_nf("Utfr d,u");
	track_copy(R_D, R_U);
}

void tfr_d_x(void)
{
	if (r_holds_node(rnode + R_X, rnode + R_D)) {
		printf("; tfr d,x removed\n");
		return;
	}
	output_nf("Xtfr d,x");
	track_copy(R_D, R_X);
}

void tfr_x_d(void)
{
	if (r_holds_node(rnode + R_D, rnode + R_X)) {
		printf("; tfr x,d removed\n");
		return;
	}
	if (r_holds_node(rnode + R_DS, rnode + R_X)) {
		printf("; tfr x,d removed\n");
		return;
	}
	/* tfr u,d does not set flags but it does make any existing
	   flag state no longer reflect D */
	output("tfr x,d");
	track_copy(R_X, R_D);
}

void tfr_d_y(void)
{
	if (r_holds_node(rnode + R_Y, rnode + R_D)) {
		printf("; tfr d,y removed\n");
		return;
	}
	output_nf("Ytfr d,y");
	track_copy(R_D, R_Y);
}

void adjust_s(int n, unsigned unused)
{
	if (n)
		output_nf("leas %d,s", n);
	return;
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
		add_y_const(1);
		outlabel("X%u", label);
	}
	if (h)
		add_y_const(h);
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
void gen_frame(unsigned size, unsigned aframe, unsigned s1)
{
	argbase = ARGBASE;
	frame_len = size;

	if (s1 != VOID && !(func_flags & F_VARARG))
		s1 = get_size(s1);
	else
		s1 = 0;
	func_a1size = s1;

	if (func_flags & F_REG(1)) {
		/* reg with long argument, fold together */
		if (s1) {
			if (s1 == 1)
				output_nf("pshs u,b");
			else {
				output_nf("pshs u,d");
				if (s1 == 4)
					output_nf("pshs y");
			}
			adjust_s(-size, 0);
		} else {
			if (size <= 6)
				pshframe(size + 2);
			else {
				output_nf("pshs u");
				adjust_s(-size, 0);
			}
		}
		argbase += 2;
	} else {
		/* We can do better here in some cases.
		   TODO:
		   fall through if s1 == 4 && last push would be y from pshframe
		   of size + 2 */
		if (s1) {
			if (s1 == 1)
				output_nf("pshs b");
			else {
				output_nf("pshs d");
				if (s1 == 4)
					output_nf("pshs y");
			}
		}
		if (size > 2 && size <= 8)
			pshframe(size);
		else
			adjust_s(-size, 0);
	}
	if (s1 && s1 < 4) {
		/* Working holds the top of the stack */
		rnode[R_D].op = T_LREF;
		rnode[R_D].value = 0;
		rnode[R_D].snum = 0;
	}
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

	/* Clean up arguments we pushed ourselves */
	size += func_a1size;

	/* FIXME: it would be better if the front end passed backends
	   the type of the first arg and return not this flag. Then we
	   could also use Y reliably for int returns and A for char */
	if (vr)
		max = 6;

	if (cpufeat & 2) {
		/* use-dp */
		byteok = 2;
		max++;
	}
	if (sp)
		error("sp");

	if (unreachable)
		return;

	/* FIXME: this logic is wrong we should not use DP unless cpufeat 2 */
/*	printf(";size %u byteok %u vr %u\n", size, byteok, vr); */
	if (size && size <= max && (byteok | !(size & 1))) {
		if (size & 1) {
			if (byteok == 2)
				str = rd + 7 - size;
			else
				str = rb + 5 - size;
		} else
			str = rw + 6 - size;
		output_nf("puls %s%s,pc", str, r ? ",u" : "");
	} else {
		adjust_s(size, 0);
		if (r)
			output_nf("puls u,pc");
		else
			output_nf("rts");
	}
	unreachable = 1;
}

/* This will match each call exactly 1 to 1 */
void gen_cleanup(struct node *n)
{
	uint16_t v = n->right->value;
	v -= *--astk;
	sp -= v;
	adjust_s(v, 0);
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


static void pushregs(void)
{
	char *rptr = "abxyu";

	if (rused == 0)
		return;

	/* FIXME: needs to run via output_nf() eventually */

	/* We have the registers loaded, now push and clean up the
	   tracking stack */
	printf("\tpshs ");
	if (rused & RUSED_A)
		sp++;
	if (rused & RUSED_B)
		sp++;
	if (rused & RUSED_X)
		sp += 2;
	if (rused & RUSED_Y)
		sp += 2;
	if (rused & RUSED_U)
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
	unsigned v = n->value;
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
		if (n->op == T_CONSTANT)
			load_a_const(v);
		else
			op8r_on_node(n, 'a', "ld", 0);
		break;
	case USE_B:
		if (n->op == T_CONSTANT)
			load_b_const(v);
		else
			op8_on_node(n, "ld", 0);
		break;
	case USE_D:
		codegen_lr(n);
		break;
	case USE_X:
		v = load_x_with(n);
		add_x_const(v);
		break;
	case USE_Y:
		v= load_r_with('y', n);
		add_y_const(v);
		break;
	case USE_U:
		/* Already usable */
		break;
	/* Longs not yet fully converted */
	case USE_L:
		/* Should try and generically load swapping the halves into
		   the next two registers */
		if (n->op == T_CONSTANT) {
			load_d_const(n->value >> 16);
			load_x_const(v);
			rused = RUSED_A | RUSED_B | RUSED_X;
			pushregs();
			break;
		}
		codegen_lr(n);
		gen_push(n);
		break;
	}
}

static void queue_push(void)
{
	/* Push any outstanding as the current op */
	if (stackop) {
		*fstk++ = stackop;
		*rstk++ = rused;
	}
	/* Prepare again */
	stackop = 0;
	rused = 0;
}

static void prep_push(struct node *n)
{
	unsigned s = get_size(n->type);

	stackop <<= 3;
	/* Downward we try and assign things to the ideal register */
	if (s == 1) {
		if (rused == 0 && can_load_r8_simple(n)) {
			stackop |= USE_A;
			rused |= RUSED_A;
		} else if (rused == RUSED_A && can_load_r8_simple(n)) {
			stackop |= USE_B;
			rused |= RUSED_B;
		} else {
			queue_push();
			/* Push ourselves an op to codegen_lr() then push B */
			*fstk++ = USE_D << 3;
			*rstk++ = RUSED_B;
		}
	} else if (s == 2) {
		/* A or B or both pushed already ? */
		/* We might have X used but not D because of pushing
		   a local address */
		if (n->op == T_RREF) {
			stackop |= USE_U;
			*fstk++ = stackop << 3;
			*rstk++ = rused | RUSED_U;
			stackop = 0;
			rused = 0;
		} else if (rused == 0 && can_load_r_simple(n) && n->op != T_LOCAL && n->op != T_ARGUMENT) {
			stackop |= USE_D;
			rused |= RUSED_A|RUSED_B;
		} else if (can_load_r_with(n) && (rused & (RUSED_X|RUSED_Y)) != (RUSED_X|RUSED_Y)) {
			/* X and Y not yet used so can use X */
			if (rused & RUSED_X) {
				stackop |= USE_Y;
				rused |= RUSED_Y;
			} else {
				stackop |= USE_X;
				rused |= RUSED_X;
			}
		} else {
			queue_push();
			/* Push ourselves an op */
			*fstk++ = USE_D << 3;
			*rstk++ = RUSED_A | RUSED_B;
		}
	} else {
		/* Will tackle long stuff later */
		queue_push();
		*fstk++ = USE_L << 3;
		*rstk++ = 0;	/* handled by gen_push instead */
		/* Handled via the gen_push so it will fix sp itself */
	}
}

static void do_fcall(struct node *n)
{
	/* No further arguments */
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

static void do_topcall(struct node *n)
{
	unsigned s;
	/* How to tell if varargs ? */
	/* TODO */
	/* Multiple arguments */
	if (n->op == T_ARGCOMMA) {
		do_fcall(n->left);
		n = n->right;	/* Last arg */
	}
	pushregs();
	s = get_size(n->type);
	*astk++ = s;
	/* and just deal with the last one as a straight generate */
	codegen_lr(n);
	/* Argument now in D - will need changes to indirect function calls */
}

void gen_fcall(struct node *n, unsigned vararg)
{
	/* No arguments */
	if (n == NULL) {
		*astk++ = 0;	/* Nothing to do */
		return;
	}
	*fstk++ = stackop;
	*rstk++ = rused;
	stackop = 0;
	rused = 0;
	/* Varargs we push all arguments */
	if (vararg) {
		do_fcall(n);
		/* No adjustment needed */
		*astk++ = 0;
	} else
		do_topcall(n);
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
	inv_d_node();
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
	inv_d_node();
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
	inv_d_node();
}

void add_d_const(uint16_t n)
{
	register uint_fast8_t hi = BYTE(n >> 8);
	register uint_fast8_t lo = BYTE(n);

	if (n == 0)
		return;

	/* TODO: can do better in terms of obj/offset but not clear it is
	   that useful */

	inv_d_node();

	if (lo) {
		output("addd #%u", n);
		a_valid = a_valid && b_valid;
	} else
		output("adda #%u", hi);

	if (BYTE(b_val + lo) < b_val)
		modify_a(a_val + hi + 1);
	else
		modify_a(a_val + hi);
	modify_b(b_val + lo);
}

void add_b_const(uint8_t n)
{
	if (n == 0)
		return;
	output("addb #%u", n & 0xFF);
	b_val += n;
	inv_d_node();

}

void load_a_b(void)
{
	output_nf("tfr b,a");
	a_val = b_val;
	a_valid = b_valid;
	inv_d_node();
}

void load_b_a(void)
{
	output_nf("tfr a,b");
	b_val = a_val;
	b_valid = a_valid;
	inv_d_node();
}

void add_x_const(uint16_t v)
{
	if (v) {
		output("leax %u,x", v);
		modify_r_by(R_X, v);
	}
}

void add_y_const(uint16_t v)
{
	if (v) {
		output("leay %u,y", v);
		modify_r_by(R_Y, v);
	}
}

void load_x_const(uint16_t v)
{
	output("ldx #%u", v);
	set_r_to(R_X, v);
}

void load_y_const(uint16_t v)
{
	output("ldy #%u", v);
	set_r_to(R_Y, v);
}

void load_u_const(uint16_t v)
{
	output("ldu #%u", v);
	set_r_to(R_U, v);
}

void move_s_d(void)
{
	output_nf("Dtfr s,d");
}

void move_d_s(void)
{
	output_nf("tfr d,s");
}

void swap_d_y(void)
{
	track_swap(R_D, R_Y);
	output_nf("exg d,y");
}

void swap_d_x(void)
{
	track_swap(R_D, R_X);
	output_nf("exg d,x");
}

/* Get D into X (may trash D) */
void make_x_d(void)
{
	track_copy(R_D, R_X);
	output_nf("tfr d,x");
}

/* Get X into D (may trash X) */
void make_d_x(void)
{
	track_copy(R_X, R_D);
	output_nf("tfr x,d");
}

void pop_x(void)
{
	output_nf("Xpuls x");
}

/*
 *	There are multiple strategies depnding on chip features
 *	available.
 */
void op8_on_ptr(const char *op, unsigned off)
{
	output("B%sb %u,x", op, off);
}

/* Do the low byte first in case it's add adc etc */
void op16_on_ptr(const char *op, const char *op2, unsigned off)
{
	/* Big endian */
	output("B%sb %u,x", op, off + 1);
	output("A%sa %u,x", op2, off);
}

/* Operations where D can be used on later processors */
void op16d_on_ptr(const char *op, unsigned off)
{
	/* Big endian */
	output("D%sd %u,x", op, off);
}

static void op8_on_tos(const char *op)
{
	output("B%sb ,s+", op);
}

static void op32_on_ptr(const char *op, const char *op2, unsigned off)
{
	output("B%sb %u,x", op, off + 3);
	output("A%sa %u,x", op2, off + 2);
	swap_d_y();
	output("B%sb %u,x", op2, off + 1);
	output("A%sa %u,x", op2, off);
	swap_d_y();
}

void op32d_on_ptr(const char *op, const char *op2, unsigned off)
{
	output("D%sd %u,x", op, off + 2);
	swap_d_y();
	output("B%sb %u,x", op2, off + 1);
	output("A%sa %u,x", op2, off);
	swap_d_y();
}

void load_via_x(struct node *n, unsigned off)
{
	switch(get_size(n->type)) {
	case 1:
		output("Bldb %u,x", off);
		break;
	case 2:
		output("ldd %u,x", off);
		set_d_node(n);
		break;
	case 4:
		output("Yldy %u,x", off);
		output("Dldd %u,x", off + 2);
		break;
	}
}

void store_via_x(struct node *n, unsigned off)
{
	switch(get_size(n->type)) {
	case 1:
		output("Mstb %u,x", off);
		break;
	case 2:
		output("Mstd %u,x", off);
		set_ds_node(n);
		break;
	case 4:
		output("Msty %u,x", off);
		output("Mstd %u,x", off + 2);
		break;
	}
}

void uniop_on_ptr(register const char *op, register unsigned off,
						register unsigned size)
{
	off += size;
	while(size--)
		output("%s %u,x", op, --off);
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
unsigned can_load_d_nox(struct node *n)
{
	register unsigned op = n->op;
	if (op == T_CONSTANT || (cpu_pic == 0 && op == T_NAME) || op == T_NREF)
		return 1;
	return 0;
}

/* These functions must not touch X on the 6809, they can on others */
static unsigned op8r_on_node(struct node *r, char reg, const char *op, unsigned off)
{
	unsigned v = r->value;
	unsigned store = 0;

	if (r->op == T_NAME && cpu_pic)
		return 0;

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
	case T_NAME:
	case T_CONSTANT:
	case T_NREF:
	case T_RDEREF:
		output("%s%c %s", op, reg, addr_form(r, off, 1));
		break;
	default:
		return 0;
	}
	/* FIXME load tracking etc - should be in the caller */
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

	if (r->op == T_NAME && cpu_pic)
		return 0;

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
	case T_NAME:
	case T_NREF:
	case T_RDEREF:
		output("%sa %s", op, addr_form(r, off, 1));
		output("%sb %s", op2, addr_form(r, off + 1, 1));
		break;
	default:
		return 0;
	}
	/* FIXME: constant load */
	if (store)
		invalidate_mem();
	else
		invalidate_work();
	return 1;
}

unsigned op16d_on_node(struct node *r, const char *op, unsigned off)
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
	case T_NAME:
		/* Name vaule as constant, not something we can
		   do directly */
		if (cpu_pic) {
			if (strcmp(op, "Zld") == 0) {
				load_x_with(r);
				tfr_x_d();
				break;
			}
			/* Need to fix other cases like adding a sym name and
			   more generally don't treat NAME as a valid right
			   side for shortcuts in pic mode */
			return 0;
		}
		output("%sd %s", op, addr_form(r, off, 2));
		break;
	case T_NSTORE:
		store = 1;
	case T_CONSTANT:
	case T_NREF:
	case T_RDEREF:
		output("%sd %s", op, addr_form(r, off, 2));
		break;
	default:
		return 0;
	}
	/* FIXME: constant load */
	if (store)
		invalidate_mem();
	else
		invalidate_work();
	return 1;
}

/* OP16R forms are always a single register so the caller can pass any
   invalidations around */
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
	case T_RDEREF:
		output("%s%c %s", op, reg, addr_form(r, off, 2));
		break;
	/* These two cases will be doable on the left with right const */
	case T_MINUS:
	case T_PLUS:
		v = r->right->value;
		if (r->op == T_MINUS)
			v = -v;
		op16r_on_node(r->left, reg, op, off);
		modify_r_by(to_r(reg), v);
		output("lea%c %u,%c", reg, WORD(v), reg);
		break;
	default:
		return 0;
	}
	/* TODO: const case tracking */
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

unsigned write_opd(struct node *r, const char *op, unsigned off)
{
	unsigned s = get_size(r->type);
	if (s == 2)
		return op16d_on_node(r, op, off);
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
	case T_RDEREF:
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
	output("A%sa ,s+", op);
	output("B%sb ,s+", op);
}

void op16d_on_tos(const char *op)
{
	output("D%sd ,s++", op);
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
		output("D%sd 2,s", op);
		swap_d_y();
		output("B%sb 1,s", op2);
		output("A%sa ,s", op2);
		swap_d_y();
		output_nf("leas 4,s");
		return 1;
	} else if (s == 2)
		op16d_on_tos(op);
	else
		op8_on_tos(op);
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
	/* FIXME: tracking if constant */
	if (v >= 16)
		load_d_const(0);
	else if (v >= 8) {
		load_a_b();
		load_b_const(0);
		v -= 8;
		if (v)
			repeated_op(v, "Alsla");
	} else {
		if (optsize && v > 2)
			output("Djsr __lsl%u", v);
		else while(v--) {
			output("Blslb");
			output("Arola");
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
		repeated_op(v, "Blslb");
		return 1;
	}
	if (s == 2) {
		left16(v);
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
		if (optsize && v > 2)
			output("Djsr __%s%u", op, v);
		else while(v--) {
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
	switch(r->op) {
	case T_ARGUMENT:
	case T_LOCAL:
	case T_LREF:
	case T_CONSTANT:
	case T_NREF:
	case T_NAME:
	case T_RDEREF:
		return 1;
	}
	return 0;
}

static unsigned can_load_r8_simple(struct node *r)
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
unsigned can_load_r_with(struct node *r)
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
		if (!can_load_r_simple(r->left))
			return 0;
		if (r->right->op != T_CONSTANT)
			return 0;
		return 1;
	}
	return 0;
}

/* For 6800 at least it is usually cheaper to reload even if the value
   we want is in D */
static unsigned load_r_with(char reg, struct node *r)
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
	case T_NAME:
		/* Except for D we can load name forms directly */
		if (cpu_pic) {
			output("lea%c %s+%u,pc", reg, namestr(r->snum), v);
			break;
		}
		/* Otherwise it's the usual story */
	case T_CONSTANT:
	case T_NREF:
	case T_RDEREF:
		output("ld%c %s", reg, addr_form(r, 0, 2));
		break;
	case T_RREF:
		if (reg != 'u')
			output_nf("tfr u,%c", reg);
		break;
	case T_PLUS:
		/* Special case array/struct */
		if (can_load_r_simple(r->left) &&
			r->right->op == T_CONSTANT) {
			load_r_with(reg, r->left);
			return r->right->value;
		}
		break;
	case T_MINUS:
		if (can_load_r_simple(r->left) &&
			r->right->op == T_CONSTANT) {
			load_r_with(reg, r->left);
			return -r->right->value;
		}
		break;
	default:
		error("lxw");
	}
	/* Caller does reg tracking for now - review */
	return 0;
}

unsigned load_x_with(struct node *r)
{
	if (r_holds_node(rnode + R_X, r))
		return 1;
	return load_r_with('x', r);
}

/* 6809 specific register loading */
unsigned load_u_with(struct node *r)
{
	if (r_holds_node(rnode + R_U, r))
		return 1;
	return load_r_with('u', r);
}

unsigned cmp_known(struct node *n, unsigned s, unsigned t, unsigned v)
{
	unsigned match = 1;
	/* We don't yet track Y well enough TOOD */
	if (s == 4)
		return 0;
	if (n->flags & CCFIXED)
		return 0;
	if (!(n->flags & CCONLY))
		return 0;
	if (!b_valid || (s == 2 && !a_valid))
		return 0;
	/* NE match */
	if (b_val != BYTE(v))
		match = 2;
	else if (s == 2 && BYTE(WORD(v >> 8)) != a_val)
		match = 2;
	if (t == match)
		set_z_known(2 - t);
	else
		set_z_known(t - 1);
	n->flags |= USECC;
	return 1;
}

/* 6809 we have SUBD and access to lots of things */
unsigned cmp_direct(struct node *n, const char *uop, const char *sop, unsigned t)
{
	struct node *r = n->right;
	unsigned s = get_size(r->type);
	unsigned v = r->value;
	unsigned zop = 0;
	const char *op = sop;

	printf(";cmp_direct %u\n", s);
	if (s == 4)
		return 0;

	if (!can_load_r_simple(r))
		return 0;

	if (cmp_known(n, s, t, v))
		return 1;

	/* Needs thought, also need to allow for CCFIXED */
	if (t && v == 0 && (r->flags & USECC) && r->op == T_CONSTANT && v == 0 && (n->flags & CCONLY)) {
		if (t != 1 || !(n->flags & CCFIXED)) {
			set_z_flag(2 - t);
			n->flags |= USECC;
			return 1;
		}
	}
	if (r->type & UNSIGNED)
		op = uop;
	/* Use == 0 helper */
	if (v == 0 && s == 2 && r->op == T_CONSTANT)
		zop = 1;
	/* Do byte compare as we know the high one matches */
	if (s == 2 && a_valid && (v & 0xFF00) == a_val && r->op == T_CONSTANT) {
		if (BYTE(v) == 0)
			output("Ztstb");
		else
			output("BZsubb #%u", BYTE(v));
		/* We implicitly did the signed first byte comparison */
		op = uop;
	} else if (s == 1 && op8_on_node(r, "BZsub", 0) == 0)
		return 0;
	/* We can trust the flags off this on 6809 as we have subd */
	else if (s == 2 && !zop && op16d_on_node(r, "DZsub", 0) == 0)
		return 0;
	/* Figure out if we need to do a boolify */
	if (t && (n->flags & CCONLY)) {
		if (t != 1 || !(n->flags & CCFIXED)) {
			if (zop) {
				if (s == 2)
					output("Dsubd #0");
				else
					output("tstb");
			}
			n->flags |= USECC;
			set_z_flag(2 - t);
			return 1;
		}
	}
	if (!zop)
		output("BZ%s %s", jsr_op, op);
	else
		output("BZ%s %s0", jsr_op, op);
	set_a(0);
	set_z_flag(0);
	n->flags |= ISBOOL | USECC;
	return 1;
}

/*
 *	Do fast multiplies were we can
 */
static int mul_stack_cost(unsigned n)
{
	int cost = 0;
	while(n > 1) {
		if (n & 1)
			cost += 4;	/* pshs d addd ,s++ */
		n >>= 1;
		cost += 2;
	}
	return cost;
}

int count_mul_cost(unsigned n)
{
	int cost = 0;
	if (a_valid && b_valid)
		return 1;	/* ldd # */
	if ((n & 0xFF) == 0) {
		n >>= 8;
		cost += 3;		/* tfr a,b clrb */
	}
	if (a_valid && a_val == 0 && n < 256) {
		return cost + 3;	/* lda #; mul */
	}
	return cost + mul_stack_cost(n);
}

/* Write the multiply for any value > 0 */
void write_mul(unsigned n)
{
	unsigned pops = 0;
	if (a_valid && b_valid) {
		unsigned m = (a_val << 8) | b_val;
		load_d_const((n * m) & 0xFFFF);
		return;
	}
	if ((n & 0xFF) == 0) {
		if (b_valid && b_val == 0) {
			load_d_const(0);
			return;
		}
		load_a_b();
		load_b_const(0);
		n >>= 8;
	}
	if (mul_stack_cost(n) > 5 && a_valid && a_val == 0 && n < 256) {
		load_a_const(n);
		output("Dmul");
		return;
	}
	while(n > 1) {
		if (n & 1) {
			pops++;
			push_d();
		}
		output("Blslb");
		output("Arola");
		n >>= 1;
	}
	while(pops--)
		output("Daddd ,s++");
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
		output("Xldx #%u", n);
		output("D%s __mulx", jsr_op);
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
			output("Alsra");
			output("Brorb");
			n >>= 1;
		}
	} else {
		output("tsta");
		output_nf("bpl X%u", ++label);
		add_d_const(n - 1);
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
		op16d_on_ptr(op, off);
	else
		op32d_on_ptr(op, op2, off);
}

/* TODO; compare and flip the boolify test rather than go via stack
   when we can */
unsigned cmp_op(struct node *n, const char *uop, const char *op, unsigned t)
{
	unsigned s = get_size(n->right->type);
	if (n->right->type & UNSIGNED)
		op = uop;
	if (s > 2)	/* For now anyway */
		return 0;
	/* We can do this versus s+ or s++ */
	if (s == 1)
		op8_on_tos("BZsub");
	else if (s == 2)
		op16d_on_tos("DZsub");
	if (t && (n->flags & CCONLY)) {
		if (t != 1 || !(n->flags & CCFIXED)) {
			set_z_flag(2 - t);
			n->flags |= USECC;
			return 1;
		}
	}
	output("DZ%s %s", jsr_op, op);
	set_a(0);
	set_z_flag(0);
	n->flags |= ISBOOL | USECC;
	return 1;
}

unsigned gen_push(struct node *n)
{
	unsigned size = get_size(n->type);
	/* Our push will put the object on the stack, so account for it */
	sp += get_stack_size(n->type);
	switch(size) {
	case 1:
		output_nf("pshs b");
		return 1;
	case 2:
		push_d();
		return 1;
	case 4:	/* Have to split them to get the order right */
		/* Or we could go PDP11 style mixed endian long ? */
		push_d();
		output_nf("pshs y");
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
	if (unreachable)
		return 1;
	unreachable = 1;
	if (frame_len + func_a1size || (func_flags & F_REG(1))) {
		output_nf("%s L%d%s", jmp_op, n, tail);
		return 0;
	}
	output_nf("rts");
	return 1;
}

void gen_jump(const char *tail, unsigned n)
{
	if (unreachable)
		return;
	output_nf("%s L%d%s", jmp_op, n, tail);
	unreachable = 1;
}

static void do_bra(const char *tail, unsigned n, unsigned v)
{
	if (cc_invert)
		v ^= 2;
	if (unreachable)
		return;
	if (trackf == NULL)
		error("j");
	/* Don't emit any branch never */
	if (trackf[v] == 'x')
		return;
	/* Always causes unreach */
	output_nf("b%c%c L%d%s", trackf[v], trackf[v + 1], n, tail);
	if (trackf[v] == 'r')
		unreachable = 1;
}

void gen_jfalse(const char *tail, unsigned n)
{
	do_bra(tail, n, 2);
}

void gen_jtrue(const char *tail, unsigned n)
{
	do_bra(tail, n, 0);
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

/* our C call and helper ABI are the same so C helpers need no changes here */
void gen_helpcall(struct node *n)
{
	invalidate_all();
	trackf = NULL;
	printf("\t%s __", jsr_op);
}

void gen_helptail(struct node *n)
{
}

/* On the return path however the helpers did their own cleanup but C did
   not. In addition the C helper won't have set flags properly if boolean */
void gen_helpclean(struct node *n)
{
	if (c_style(n)) {
		if (n->left)
			adjust_s(get_size(n->left->type), 1);
		/* C style ops that are ISBOOL didn't set the bool flags */
		if (n->flags & ISBOOL)
			output("Ztstb");
	}
}

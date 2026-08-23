/*
 *	General glue between the backend and 680x code generator
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6800.h"

unsigned target_ptr = UINT;	/* Until we tackle 6809 large model */

unsigned cpu_has_d;		/* 16bit ops and 'D' are present */
unsigned cpu_has_xgdx;		/* XGDX is present */
unsigned cpu_has_abx;		/* ABX is present */
unsigned cpu_has_pshx;		/* Has PSHX PULX */
unsigned cpu_has_y;		/* Has Y register */
unsigned cpu_has_lea;		/* Has LEA. For now 6809 but if we get to HC12... */
unsigned cpu_is_09;		/* Bulding for 6x09 so a bit different */
unsigned cpu_pic;		/* Position independent output (6809 only) */

const char *jmp_op = "jmp";
const char *jsr_op = "jsr";
const char *pic_op = "";

/*
 *	State for the current function
 */
unsigned frame_len;		/* Number of bytes of stack frame */
unsigned argbase;		/* Argument offset in current function */
unsigned sp;			/* Stack pointer offset tracking */
unsigned unreachable;		/* Code following an unconditional jump */

/* Export the C symbol */
void gen_export(const char *name)
{
	output(".export %s", name);
}

void gen_segment(unsigned s)
{
	switch(s) {
	case A_CODE:
		output(".%s", codeseg);
		break;
	case A_DATA:
		output(".data");
		break;
	case A_LITERAL:
		output(".literal");
		break;
	case A_BSS:
		output(".bss");
		break;
	default:
		error("gseg");
	}
}

void gen_switch(unsigned n, unsigned type)
{
	/* Can't do nicely with output but should be fine */
	output("ldx #Sw%u", n);
	printf("\t%s __switch", jmp_op);
	helper_type(type, 0);
	putchar('\n');
}

void gen_switchdata(unsigned n, unsigned size)
{
	outlabel("Sw%u", n);
	output_nf(".word %d", size);
}

void gen_case_label(unsigned tag, unsigned entry)
{
	unreachable = 0;
	invalidate_all();
	outlabel("Sw%d_%d", tag, entry);
}

void gen_case_data(unsigned tag, unsigned entry)
{
	output_nf(".word Sw%d_%d", tag, entry);
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
			output("tstb");
	}
}

void gen_data_label(const char *name, unsigned align)
{
	outlabel("%s", name);
}

void gen_space(unsigned value)
{
	output_nf(".ds %d", value);
}

void gen_text_data(struct node *n)
{
	output_nf(".word T%d", n->snum);
}

void gen_literal(unsigned n)
{
	if (n)
		outlabel("T%d", n);
}

void gen_name(struct node *n)
{
	output(".word %s+%d", namestr(n->snum), WORD(n->value));
}

void gen_value(unsigned type, unsigned long value)
{
	if (PTR(type)) {
		output_nf(".word %u", WORD(value));
		return;
	}
	switch (type) {
	case CCHAR:
	case UCHAR:
		output_nf(".byte %u", BYTE(value));
		break;
	case CSHORT:
	case USHORT:
		output_nf(".word %d", WORD(value));
		break;
	case CLONG:
	case ULONG:
	case FLOAT:
		/* We are big endian */
		output_nf(".word %d", WORD(value >> 16));;
		output_nf(".word %d", WORD(value));
		break;
	default:
		error("unsuported type");
	}
}

void gen_start(void)
{
	switch(cpu) {
	case 6309:
	case 6809:
		cpu_pic = cpufeat & 1;
		cpu_is_09 = 1;
		cpu_has_d = 1;
		cpu_has_pshx = 1;
		cpu_has_y = 1;
		cpu_has_abx = 1;
		cpu_has_xgdx = 1;
		cpu_has_lea = 1;
		jmp_op = "bra";	/* Maybe a choice will be needed for jmp v bra/lbra ? */
		if (cpu_pic) {
			jsr_op = "lbsr";
			pic_op = ",pcr";
		} else
			jsr_op = "jsr";
		break;
	case 6811:
		cpu_has_y = 1;
	case 6303:
		cpu_has_xgdx = 1;
		/* Fall through */
	case 6803:
		cpu_has_d = 1;
		cpu_has_abx = 1;
		cpu_has_pshx = 1;
	case 6800:
		break;
	}
	/* For the moment. Needs adding to assembler for 6809 v 6309 */
	if (cpu != 6809 && cpu != 6811)
		output(".setcpu %u", cpu);
	output(".code");
}

void gen_end(void)
{
}

void gen_tree(struct node *n)
{
	codegen_lr(n);
	puts(";");
}


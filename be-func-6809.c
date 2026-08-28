/*
 *	General glue between the backend and 680x code generator
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "backend.h"
#include "backend-6809.h"

unsigned target_ptr = UINT;	/* Until we tackle 6809 large model */

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
unsigned func_a1size;		/* Number of argument bytes pushed by callee */

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
	cpu_pic = cpufeat & 1;
	jmp_op = "bra";	/* Maybe a choice will be needed for jmp v bra/lbra ? */
	if (cpu_pic) {
		jsr_op = "lbsr";
		pic_op = ",pcr";
	} else
		jsr_op = "jsr";
	/* If we add 6309 some day we may need to .setcpu here or similar */
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

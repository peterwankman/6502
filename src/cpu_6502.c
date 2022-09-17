/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <stdio.h>
#include <stdlib.h>

#include "leakcheck.h"

#include "cpu_6502.h"
#include "mem.h"
#include "status.h"
#include "vm.h"

#define NMI_VECTOR		0xfffa
#define RES_VECTOR		0xfffc
#define BRK_VECTOR		0xfffe

#define SET_FLAG(flag) (cpu->flags |= (flag))
#define CLEAR_FLAG(flag) (cpu->flags &= (~flag))
#define QUERY_FLAG(flag) ((cpu->flags & (flag)) ? 1 : 0)

struct cpu_6502_t {
	uint8_t flags;
	uint16_t pc;	/* Program Counter */
	uint8_t sp;		/* Stack Pointer */

	uint8_t ir;		/* Instruction Register */
	union {			/* Instruction argument */
		uint8_t arg8;
		uint16_t arg;
	};

	uint8_t a;		/* Accumulator */
	uint8_t x;		/* X Index Register */
	uint8_t y;		/* Y Index Register */
 
	void *vm;
};

typedef int (*op_proc)(struct cpu_6502_t*, int*);

static void push(cpu_6502_t *cpu, const uint8_t val) {
	write_mem(cpu->vm, cpu->sp + 0x0100, val);
	cpu->sp--;
}

static void pull(cpu_6502_t *cpu, uint8_t *val) {
	cpu->sp++;
	*val = read_mem(cpu->vm, cpu->sp + 0x100);
}

static void cmp_flags(cpu_6502_t *cpu, const uint8_t reg, const uint8_t target) {
	if((reg - target) & 0x80)
		SET_FLAG(FLAG_NEGATIVE);
	else
		CLEAR_FLAG(FLAG_NEGATIVE);

	if(reg >= target)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	if(reg == target)
		SET_FLAG(FLAG_ZERO);
	else
		CLEAR_FLAG(FLAG_ZERO);
}

static void flip_flags(cpu_6502_t *cpu, uint8_t val) {
	if(val & 0x80) {
		SET_FLAG(FLAG_NEGATIVE);
	} else {
		CLEAR_FLAG(FLAG_NEGATIVE);
	}

	if(val == 0) {
		SET_FLAG(FLAG_ZERO);
	} else {
		CLEAR_FLAG(FLAG_ZERO);
	}
}

static void adc_binary(cpu_6502_t *cpu, const uint8_t b) {
	uint8_t sum8 = cpu->a + b + (QUERY_FLAG(FLAG_CARRY) ? 1 : 0);
	uint16_t sum16 = cpu->a + b + (QUERY_FLAG(FLAG_CARRY) ? 1 : 0);

	if((cpu->a ^ sum8) & (b ^ sum8) & 0x80)
		SET_FLAG(FLAG_OVERFLOW);
	else
		CLEAR_FLAG(FLAG_OVERFLOW);

	if(sum16 > 255)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	cpu->a = sum8;
}

static void adc_decimal(cpu_6502_t *cpu, const uint8_t b) {
	int abin, bbin, rbin;
	int carry = QUERY_FLAG(FLAG_CARRY) ? 1 : 0;

	abin = (cpu->a >> 4) * 10 + (cpu->a & 0x0f);
	bbin = (b >> 4) * 10 + (b & 0x0f);
	rbin = abin + bbin + carry;

	if(rbin > 99) 
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	cpu->a = ((rbin % 100) / 10) << 4 | (rbin % 10);
}

static void sbc_decimal(cpu_6502_t *cpu, const uint8_t b) {
	int abin, bbin, rbin;
	int carry = QUERY_FLAG(FLAG_CARRY) ? 0 : 1;

	abin = (cpu->a >> 4) * 10 + (cpu->a & 0x0f);
	bbin = (b >> 4) * 10 + (b & 0x0f);
	rbin = abin - bbin - carry;
	if(rbin < 0) {
		CLEAR_FLAG(FLAG_CARRY);
		rbin += 100;
	} else {
		SET_FLAG(FLAG_CARRY);
	}

	cpu->a = ((rbin % 100) / 10) << 4 | (rbin % 10);
}

static void sbc_binary(cpu_6502_t *cpu, const uint8_t b) {
	uint8_t sum8 = cpu->a - b - (QUERY_FLAG(FLAG_CARRY) ? 0 : 1);
	uint16_t sum16 = cpu->a - b - (QUERY_FLAG(FLAG_CARRY) ? 0 : 1);

	if((cpu->a ^ sum8) & (~b ^ sum8) & 0x80)
		SET_FLAG(FLAG_OVERFLOW);
	else
		CLEAR_FLAG(FLAG_OVERFLOW);

	if(sum16 > 0xff)
		CLEAR_FLAG(FLAG_CARRY);
	else
		SET_FLAG(FLAG_CARRY);

	cpu->a = sum8;
}

static uint16_t read_ptr_zp(vm_t *vm, const uint16_t addr) {
	return read_ptr_wrap(vm, addr & 0xff);
}

static int interrupt(cpu_6502_t *cpu, const uint16_t vector, int *cyc) {
	cpu->pc += 2;
	push(cpu, (cpu->pc >> 8) & 0xff);
	push(cpu, cpu->pc & 0xff);	
	push(cpu, cpu->flags);

	SET_FLAG(FLAG_INTERRUPT);
	cpu->pc = read_ptr(cpu->vm, vector);
	*cyc = 6;

	return RET_JUMP;
}

/* Opcode implementations */

/* RMW instructions */
static int asl(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target;
	
	switch(cpu->ir) {
		case 0x0a:	/* ASL A */
			target = &(cpu->a);						
			*cyc=2; break;

		case 0x06:	/* ASL $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0x16:	/* ASL $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0x0e:	/* ASL $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0x1e:	/* ASL $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	if(*target >> 7)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	*target = *target << 1;

	flip_flags(cpu, *target);
	return RET_OK;
}

static int dec(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target;

	switch(cpu->ir) {
		case 0xc6:	/* DEC $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0xd6:	/* DEC $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0xce:	/* DEC $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0xde:	/* DEC $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	(*target)--;
	flip_flags(cpu, *target);
	return RET_OK;
}

static int inc(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target = NULL;

	switch(cpu->ir) {
		case 0xe6:	/* INC $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0xf6:	/* INC $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0xee:	/* INC $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0xfe:	/* INC $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	(*target)++;
	flip_flags(cpu, *target);
	return RET_OK;
}

static int lsr(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target;
	
	switch(cpu->ir) {
		case 0x4a:	/* LSR A */
			target = &(cpu->a);
			*cyc=2; break;

		case 0x46:	/* LSR $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0x56:	/* LSR $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0x4e:	/* LSR $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0x5e:	/* LSR $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	if(*target & 0x01)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	*target = *target >> 1;
	flip_flags(cpu, *target);

	return RET_OK;
}

static int rol(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target;
	int carry_in;
	
	switch(cpu->ir) {
		case 0x2a:	/* ROL A */
			target = &(cpu->a);
			*cyc=2; break;

		case 0x26:	/* ROL $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0x36:	/* ROL $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0x2e:	/* ROL $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0x3e:	/* ROL $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	carry_in = QUERY_FLAG(FLAG_CARRY) ? 1 : 0;

	if(*target >> 7)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	*target = *target << 1;
	*target |= carry_in;
	flip_flags(cpu, *target);

	return RET_OK;
}

static int ror(cpu_6502_t *cpu, int *cyc) {
	uint8_t *target;
	int carry_in;
	
	switch(cpu->ir) {
		case 0x6a:	/* ROR A */
			target = &(cpu->a);						
			*cyc=2; break;

		case 0x66:	/* ROR $xx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=5; break;

		case 0x76:	/* ROR $xx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=6; break;

		case 0x6e:	/* ROR $xxxx */
			target = get_pointer(cpu->vm, cpu->arg);
			*cyc=6; break;

		case 0x7e:	/* ROR $xxxx, X */
			target = get_pointer(cpu->vm, cpu->arg + cpu->x);
			*cyc=7; break;

		default:
			return RET_ERR_INSTR;
	}

	carry_in = QUERY_FLAG(FLAG_CARRY) ? 1 : 0;

	if(*target & 0x01)
		SET_FLAG(FLAG_CARRY);
	else
		CLEAR_FLAG(FLAG_CARRY);

	*target = *target >> 1;
	*target |= (carry_in << 7);
	flip_flags(cpu, *target);

	return RET_OK;
}

/* Simple instructions */
static int adc(cpu_6502_t *cpu, int *cyc) {
	void (*addfunc)(cpu_6502_t*, uint8_t);
	uint8_t operand;

	if(QUERY_FLAG(FLAG_DECIMAL)) 
		addfunc = adc_decimal;
	else
		addfunc = adc_binary;

	switch(cpu->ir) {
		case 0x69:	/* ADC #$xx */
			operand = cpu->arg8;
			*cyc=2; break;

		case 0x65:	/* ADC $xx */
			operand = read_mem(cpu->vm, cpu->arg8);
			*cyc=3; break;

		case 0x75:	/* ADC $xx, X */
			operand = read_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff);
			*cyc=4; break;

		case 0x6d:	/* ADC $xxxx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0x7d:	/* ADC $xxxx, X */
			operand = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;	/**/

		case 0x79:	/* ADC $xxxx, Y */
			operand = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break;	/**/

		case 0x61:	/* ADC ($xx, X) */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0x71:	/* ADC ($xx), Y */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break;	/**/

		default:
			return RET_ERR_INSTR;
	}

	addfunc(cpu, operand);
	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int and(cpu_6502_t *cpu, int *cyc) {
	uint8_t operand;

	switch(cpu->ir) {
		case 0x29:	/* AND #$xx */
			operand = cpu->arg8;
			*cyc=2; break;

		case 0x25:	/* AND $xx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0x35:	/* AND $xx, X */
			operand = read_mem(cpu->vm, (cpu->arg8 + cpu->x) & 0xff);
			*cyc=4; break;
			
		case 0x2d:	/* AND $xxxx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0x3d:	/* AND $xxxx, X */
			operand = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;	/**/

		case 0x39:	/* AND $xxxx, Y */
			operand = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break;	/**/

		case 0x21:	/* AND ($xx, X) */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg8 + cpu->x));
			*cyc=6; break;

		case 0x31:	/* AND ($xx), Y */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break;	/**/

		default:
			return RET_ERR_INSTR;
	}

	cpu->a &= operand;
	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int bit(cpu_6502_t *cpu, int *cyc) {
	uint8_t pattern;

	switch(cpu->ir) {
		case 0x24:	/* BIT $xx */
			pattern = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0x2c:	/* BIT $xxxx */
			pattern = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	if(pattern & 0x80)
		SET_FLAG(FLAG_NEGATIVE);
	else
		CLEAR_FLAG(FLAG_NEGATIVE);

	if(pattern & 0x40)
		SET_FLAG(FLAG_OVERFLOW);
	else
		CLEAR_FLAG(FLAG_OVERFLOW);

	if(pattern & cpu->a)
		CLEAR_FLAG(FLAG_ZERO);
	else
		SET_FLAG(FLAG_ZERO);

	return RET_OK;
}

static int bra(cpu_6502_t *cpu, int *cyc) {
	int taken = 0;
	int8_t distance = cpu->arg8;

	switch(cpu->ir) {
		case 0x10: /* BPL */
			if(!QUERY_FLAG(FLAG_NEGATIVE)) taken = 1; break;

		case 0x30: /* BMI */
			if(QUERY_FLAG(FLAG_NEGATIVE)) taken = 1; break;

		case 0x50: /* BVC */
			if(!QUERY_FLAG(FLAG_OVERFLOW)) taken = 1; break;

		case 0x70: /* BVS */
			if(QUERY_FLAG(FLAG_OVERFLOW)) taken = 1; break;

		case 0x90: /* BCC */
			if(!QUERY_FLAG(FLAG_CARRY)) taken = 1; break;
			
		case 0xb0: /* BCS */
			if(QUERY_FLAG(FLAG_CARRY)) taken = 1; break;

		case 0xd0: /* BNE */
			if(!QUERY_FLAG(FLAG_ZERO)) taken = 1; break;

		case 0xf0: /* BEQ */
			if(QUERY_FLAG(FLAG_ZERO)) taken = 1; break;

		default:
			return RET_ERR_INSTR;
	}
	
	*cyc = 2; /**/
	if(taken) {
		*cyc++;
		cpu->pc += distance + 2;
		return RET_JUMP;
	}
	return RET_OK;
}

static int brk(cpu_6502_t *cpu, int *cyc) {
	if(cpu->ir != 0x00) return RET_ERR_INSTR;
	SET_FLAG(FLAG_BREAK);

	return interrupt(cpu, BRK_VECTOR, cyc);
}

static int cmp(cpu_6502_t *cpu, int *cyc) {
	uint8_t target;

	switch(cpu->ir) {
		case 0xc9:	/* CMP #$xx */
			target = cpu->arg8;
			*cyc=2; break;

		case 0xc5:	/* CMP $xx */
			target = read_mem(cpu->vm, cpu->arg); 
			*cyc=3; break;

		case 0xd5:	/* CMP $xx, X */
			target = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;

		case 0xcd:	/* CMP $xxxx */
			target = read_mem(cpu->vm, cpu->arg); 
			*cyc=4; break;

		case 0xdd:	/* CMP $xxxx, X */
			target = read_mem(cpu->vm, cpu->arg + cpu->x); 
			*cyc=4; break; /**/

		case 0xd9:	/* CMP $xxxx, Y */
			target = read_mem(cpu->vm, cpu->arg + cpu->y); 
			*cyc=4; break; /**/

		case 0xc1:	/* CMP ($xx, X) */
			target = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0xd1:	/* CMP ($xx), Y */
			target = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break; /**/

		default:
			return RET_ERR_INSTR;
	}

	cmp_flags(cpu, cpu->a, target);
	return RET_OK;
}

static int cpx(cpu_6502_t *cpu, int *cyc) {
	uint8_t target;

	switch(cpu->ir) {
		case 0xe0:	/* CPX #$xx */
			target = cpu->arg8;
			*cyc=2; break;

		case 0xe4:	/* CPX $xx */
			target = read_mem(cpu->vm, cpu->arg8);
			*cyc=3; break;

		case 0xec:	/* CPX $xxxx */
			target = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	cmp_flags(cpu, cpu->x, target);
	return RET_OK;
}

static int cpy(cpu_6502_t *cpu, int *cyc) {
	uint8_t target;

	switch(cpu->ir) {
		case 0xc0:	/* CPY #$xx */
			target = cpu->arg8;
			*cyc=2; break;

		case 0xc4:	/* CPY $xx */
			target = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0xcc:	/* CPY $xxxx */
			target = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	cmp_flags(cpu, cpu->y, target);
	return RET_OK;
}

static int eor(cpu_6502_t *cpu, int *cyc) {
	uint8_t operand;

	switch(cpu->ir) {
		case 0x49:	/* EOR #$xx */
			operand = cpu->arg8;
			*cyc=2; break;

		case 0x45:	/* EOR $xx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0x55:	/* EOR $xx, X */
			operand = read_mem(cpu->vm, (cpu->arg + cpu->x & 0xff));
			*cyc=4; break;
			
		case 0x4d:	/* EOR $xxxx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0x5d:	/* EOR $xxxx, X */
			operand = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;	/**/

		case 0x59:	/* EOR $xxxx, Y */
			operand = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break;	/**/

		case 0x41:	/* EOR ($xx, X) */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0x51:	/* EOR ($xx), Y */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break;	/**/

		default:
			return RET_ERR_INSTR;
	}

	cpu->a ^= operand;
	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int flg(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0x18:	/* CLC */
			CLEAR_FLAG(FLAG_CARRY);
			break;

		case 0x38:	/* SEC */
			SET_FLAG(FLAG_CARRY);
			break;

		case 0x58:	/* CLI */
			CLEAR_FLAG(FLAG_INTERRUPT);
			break;

		case 0x78:	/* SEI */
			SET_FLAG(FLAG_INTERRUPT);
			break;

		case 0xb8:	/* CLV */
			CLEAR_FLAG(FLAG_OVERFLOW);
			break;

		case 0xd8:	/* CLD */
			CLEAR_FLAG(FLAG_DECIMAL);
			break;

		case 0xf8:	/* SED */
			SET_FLAG(FLAG_DECIMAL);
			break;

		default:
			return RET_ERR_INSTR;
	}

	*cyc=2;
	return RET_OK;
}

static int jmp(cpu_6502_t *cpu, int *cyc) {
	uint16_t target;

	switch(cpu->ir) {
		case 0x4c:	/* JMP $xxxx */
			target = cpu->arg;
			*cyc=3; break;

		case 0x6c:	/* JMP ($xxxx) */
			target = read_ptr_wrap(cpu->vm, cpu->arg);
			*cyc=5; break;

		default:
			return RET_ERR_INSTR;
	}

	cpu->pc = target;
	return RET_JUMP;
}

static int jsr(cpu_6502_t *cpu, int *cyc) {
	if(cpu->ir != 0x20) return RET_ERR_INSTR;

	cpu->pc += 2;
	push(cpu, (cpu->pc >> 8) & 0xff);
	push(cpu, cpu->pc & 0xff);
	cpu->pc = cpu->arg;

	*cyc = 6;
	return RET_JUMP;
}

static int lda(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0xa9:	/* LDA #$xx */
			cpu->a = cpu->arg8;
			*cyc=2; break;

		case 0xa5:	/* LDA $xx */
			cpu->a = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0xb5:	/* LDA $xx, X */
			cpu->a = read_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff);
			*cyc=4; break;

		case 0xad:	/* LDA $xxxx */
			cpu->a = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0xbd:	/* LDA $xxxx, X */
			cpu->a = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break; /**/

		case 0xb9:	/* LDA $xxxx, Y */
			cpu->a = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break; /**/

		case 0xa1:	/* LDA ($xx, X) */
			cpu->a = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0xb1:	/* LDA ($xx), Y */
			cpu->a = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break; /**/

		default:
			return RET_ERR_INSTR;
	}

	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int ldx(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0xa2:	/* LDX #$xx */
			cpu->x = cpu->arg8;
			*cyc=2; break;

		case 0xa6:	/* LDX $xx */
			cpu->x = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0xb6:	/* LDX $xx, Y */
			cpu->x = read_mem(cpu->vm, (cpu->arg + cpu->y) & 0xff);
			*cyc=4; break;

		case 0xae:	/* LDX $xxxx */
			cpu->x = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0xbe:	/* LDX $xxxx, Y */
			cpu->x = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break; /**/

		default:
			return RET_ERR_INSTR;
	}

	flip_flags(cpu, cpu->x);
	return RET_OK;
}

static int ldy(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0xa0:	/* LDY #$xx */
			cpu->y = cpu->arg8;
			*cyc=2; break;

		case 0xa4:	/* LDY $xx */
			cpu->y = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0xb4:	/* LDY $xx, X */
			cpu->y = read_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff);
			*cyc=4; break;

		case 0xac:	/* LDY $xxxx */
			cpu->y = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0xbc:	/* LDY $xxxx, X */
			cpu->y = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break; /**/

		default:
			return RET_ERR_INSTR;
	}

	flip_flags(cpu, cpu->y);
	return RET_OK;
}

static int nop(cpu_6502_t *cpu, int *cyc) {
	if(cpu->ir != 0xea) return RET_ERR_INSTR;
	*cyc=2;
	return RET_OK;
}

static int ora(cpu_6502_t *cpu, int *cyc) {
	uint8_t operand;

	switch(cpu->ir) {
		case 0x09:	/* ORA #$xx */
			operand = cpu->arg8;
			*cyc=2; break;

		case 0x05:	/* ORA $xx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0x15:	/* ORA $xx, X */
			operand = read_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff);
			*cyc=4; break;
			
		case 0x0d:	/* ORA $xxxx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0x1d:	/* ORA $xxxx, X */
			operand = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;	/**/

		case 0x19:	/* ORA $xxxx, Y */
			operand = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break;	/**/

		case 0x01:	/* ORA ($xx, X) */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0x11:	/* ORA ($xx), Y */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break;	/**/

		default:
			return RET_ERR_INSTR;
	}

	cpu->a |= operand;
	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int reg(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0xaa:	/* TAX */
			cpu->x = cpu->a;
			flip_flags(cpu, cpu->x);
			break;
			
		case 0x8a:	/* TXA */
			cpu->a = cpu->x;
			flip_flags(cpu, cpu->a);
			break;

		case 0xca:	/* DEX */
			cpu->x--;
			flip_flags(cpu, cpu->x);
			break;

		case 0xe8:	/* INX */
			cpu->x++;
			flip_flags(cpu, cpu->x);
			break;

		case 0xa8:	/* TAY */
			cpu->y = cpu->a;
			flip_flags(cpu, cpu->y);
			break;

		case 0x98:	/* TYA */
			cpu->a = cpu->y;
			flip_flags(cpu, cpu->a);
			break;

		case 0x88:	/* DEY */
			cpu->y--;
			flip_flags(cpu, cpu->y);
			break;

		case 0xc8:	/* INY */
			cpu->y++;
			flip_flags(cpu, cpu->y);
			break;

		default:
			return RET_ERR_INSTR;
	}

	*cyc = 2;
	return RET_OK;
}

static int rti(cpu_6502_t *cpu, int *cyc) {
	uint8_t hi, lo;
	uint16_t addr;

	if(cpu->ir != 0x40) return RET_ERR_INSTR;
	*cyc = 6;

	pull(cpu, &(cpu->flags));
	pull(cpu, &lo);
	pull(cpu, &hi);

	addr = hi << 8 | lo;
	cpu->pc = addr;

	return RET_JUMP;
}

static int rts(cpu_6502_t *cpu, int *cyc) {
	uint8_t hi, lo;
	uint16_t addr;

	if(cpu->ir != 0x60) return RET_ERR_INSTR;
	*cyc = 6;

	pull(cpu, &lo);
	pull(cpu, &hi);
	addr = hi << 8 | lo;
	cpu->pc = addr + 1;

	return RET_JUMP;
}

static int sbc(cpu_6502_t *cpu, int *cyc) {
	uint8_t operand;
	void (*subfunc)(cpu_6502_t*, uint8_t);

	if(QUERY_FLAG(FLAG_DECIMAL))
		subfunc = sbc_decimal;
	else
		subfunc = sbc_binary;

	switch(cpu->ir) {
		case 0xe9:	/* SBC #$xx */
			operand = cpu->arg8;
			*cyc=2; break;

		case 0xe5:	/* SBC $xx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=3; break;

		case 0xf5:	/* SBC $xx, X */
			operand = read_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff);
			*cyc=4; break;

		case 0xed:	/* SBC $xxxx */
			operand = read_mem(cpu->vm, cpu->arg);
			*cyc=4; break;

		case 0xfd:	/* SBC $xxxx, X */
			operand = read_mem(cpu->vm, cpu->arg + cpu->x);
			*cyc=4; break;	/**/

		case 0xf9:	/* SBC $xxxx, Y */
			operand = read_mem(cpu->vm, cpu->arg + cpu->y);
			*cyc=4; break;	/**/

		case 0xe1:	/* SBC ($xx, X) */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x));
			*cyc=6; break;

		case 0xf1:	/* SBC ($xx), Y */
			operand = read_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y);
			*cyc=5; break;	/**/

		default:
			return RET_ERR_INSTR;
	}

	subfunc(cpu, operand);
	flip_flags(cpu, cpu->a);
	return RET_OK;
}

static int sta(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0x85: /* STA $xx */
			write_mem(cpu->vm, cpu->arg, cpu->a);
			*cyc=3;
			break;

		case 0x95: /* STA $xx, X */
			write_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff, cpu->a);
			*cyc=4; break;

		case 0x8d: /* STA $xxxx */
			write_mem(cpu->vm, cpu->arg, cpu->a);
			*cyc=4; break;

		case 0x9d: /* STA $xxxx, X */
			write_mem(cpu->vm, cpu->arg + cpu->x, cpu->a);
			*cyc=5; break;

		case 0x99: /* STA $xxxx, Y */
			write_mem(cpu->vm, cpu->arg + cpu->y, cpu->a);
			*cyc=5; break;

		case 0x81: /* STA ($xx, X) */
			write_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg + cpu->x), cpu->a);
			*cyc=6; break;

		case 0x91: /* STA ($xx), Y */
			write_mem(cpu->vm, read_ptr_zp(cpu->vm, cpu->arg) + cpu->y, cpu->a);
			*cyc=6; break;

		default:
			return RET_ERR_INSTR;

	}

	return RET_OK;
}

static int stk(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0x9a:	/* TXS */
			cpu->sp = cpu->x;
			*cyc=2; break;

		case 0xba:	/* TSX */
			cpu->x = cpu->sp;
			flip_flags(cpu, cpu->x);
			*cyc=2; break;

		case 0x48:	/* PHA */
			push(cpu, cpu->a);
			*cyc=3; break;

		case 0x68:	/* PLA */
			pull(cpu, &(cpu->a));
			flip_flags(cpu, cpu->a);
			*cyc=4; break;

		case 0x08:	/* PHP */
			push(cpu, cpu->flags | FLAG_BREAK);
			*cyc=3; break;

		case 0x28:	/* PLP */
			pull(cpu, &(cpu->flags));
			SET_FLAG(FLAG_RESERVED);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	return RET_OK;
}

static int stx(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0x86:	/* STX $xx */
			write_mem(cpu->vm, cpu->arg, cpu->x);
			*cyc=3; break;

		case 0x96: /* STX $xx, Y */
			write_mem(cpu->vm, (cpu->arg + cpu->y) & 0xff, cpu->x);
			*cyc=4; break;

		case 0x8e: /* STX $xxxx */
			write_mem(cpu->vm, cpu->arg, cpu->x);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	return RET_OK;
}

static int sty(cpu_6502_t *cpu, int *cyc) {
	switch(cpu->ir) {
		case 0x84:	/* STY $xx */
			write_mem(cpu->vm, cpu->arg, cpu->y);
			*cyc=3; break;

		case 0x94: /* STY $xx, X */
			write_mem(cpu->vm, (cpu->arg + cpu->x) & 0xff, cpu->y);
			*cyc=4; break;

		case 0x8c: /* STY $xxxx */
			write_mem(cpu->vm, cpu->arg, cpu->y);
			*cyc=4; break;

		default:
			return RET_ERR_INSTR;
	}

	return RET_OK;
}

/* Illegal instruction */
static int x(cpu_6502_t *cpu, int *cyc) {
	return RET_ERR_INSTR;
}

static op_proc instr_table[256] = {
	brk, ora, x,   x,   x,   ora, asl, x,   stk, ora, asl, x,   x,   ora, asl, x,   
	bra, ora, x,   x,   x,   ora, asl, x,   flg, ora, x,   x,   x,   ora, asl, x,   
	jsr, and, x,   x,   bit, and, rol, x,   stk, and, rol, x,   bit, and, rol, x,   
	bra, and, x,   x,   x,   and, rol, x,   flg, and, x,   x,   x,   and, rol, x,   
	rti, eor, x,   x,   x,   eor, lsr, x,   stk, eor, lsr, x,   jmp, eor, lsr, x,   
	bra, eor, x,   x,   x,   eor, lsr, x,   flg, eor, x,   x,   x,   eor, lsr, x,   
	rts, adc, x,   x,   x,   adc, ror, x,   stk, adc, ror, x,   jmp, adc, ror, x,   
	bra, adc, x,   x,   x,   adc, ror, x,   flg, adc, x,   x,   x,   adc, ror, x,   
	x,   sta, x,   x,   sty, sta, stx, x,   reg, x,   reg, x,   sty, sta, stx, x,   
	bra, sta, x,   x,   sty, sta, stx, x,   reg, sta, stk, x,   x,   sta, x,   x,   
	ldy, lda, ldx, x,   ldy, lda, ldx, x,   reg, lda, reg, x,   ldy, lda, ldx, x,   
	bra, lda, x,   x,   ldy, lda, ldx, x,   flg, lda, stk, x,   ldy, lda, ldx, x,   
	cpy, cmp, x,   x,   cpy, cmp, dec, x,   reg, cmp, reg, x,   cpy, cmp, dec, x,   
	bra, cmp, x,   x,   x,   cmp, dec, x,   flg, cmp, x,   x,   x,   cmp, dec, x,   
	cpx, sbc, x,   x,   cpx, sbc, inc, x,   reg, sbc, nop, x,   cpx, sbc, inc, x,   
	bra, sbc, x,   x,   x,   sbc, inc, x,   flg, sbc, x,   x,   x,   sbc, inc, x
};

static int len[256] = {
/*		0 1 2 3 4 5 6 7 8 9 a b c d e f */
/*0*/	1,2,0,0,0,2,2,0,1,2,1,0,0,3,3,0,
/*1*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
/*2*/	3,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
/*3*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
/*4*/	1,2,0,0,0,2,2,0,1,2,1,0,3,3,3,0,
/*5*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
/*6*/	1,2,0,0,0,2,2,0,1,2,1,0,3,3,3,0,
/*7*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
/*8*/	0,2,0,0,2,2,2,0,1,0,1,0,3,3,3,0,
/*9*/	2,2,0,0,2,2,2,0,1,3,1,0,0,3,0,0,
/*a*/	2,2,2,0,2,2,2,0,1,2,1,0,3,3,3,0,
/*b*/	2,2,0,0,2,2,2,0,1,3,1,0,3,3,3,0,
/*c*/	2,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
/*d*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0,
/*e*/	2,2,0,0,2,2,2,0,1,2,1,0,3,3,3,0,
/*f*/	2,2,0,0,0,2,2,0,1,3,0,0,0,3,3,0
};

void *cpu_6502_init(void *vm) {
	cpu_6502_t *out = malloc(sizeof(cpu_6502_t));

	if(out == NULL)
		return NULL;

	out->vm = vm;
	return out;
}

void cpu_6502_quit(cpu_6502_t *cpu) {
	free(cpu);
}

void cpu_6502_reset(cpu_6502_t *cpu) {
	cpu->a = 0;
	cpu->x = 0;
	cpu->y = 0;
	cpu->sp = 0xff;
	cpu->flags = FLAG_RESERVED;

	cpu->pc = read_ptr(cpu->vm, RES_VECTOR);
}

void cpu_6502_fetch_instr(cpu_6502_t *cpu) {
	cpu->ir = read_mem(cpu->vm, cpu->pc);

	cpu->arg = read_mem(cpu->vm, (cpu->pc + 1) & 0xffff);

	if(len[cpu->ir] == 3)
		cpu->arg |= read_mem(cpu->vm, (cpu->pc + 2) & 0xffff) << 8;
}

int cpu_6502_exec_instr(cpu_6502_t *cpu, int *cyc) {
	int status;

	status = instr_table[cpu->ir](cpu, cyc);

	if(status != RET_JUMP)
		cpu->pc += len[cpu->ir];

	return status;
}

int cpu_6502_nmi(cpu_6502_t *cpu, int *cyc) {
	return interrupt(cpu, NMI_VECTOR, cyc);
}

int cpu_6502_irq(cpu_6502_t *cpu, int *cyc) {
	return interrupt(cpu, BRK_VECTOR, cyc);
}

uint16_t cpu_6502_get_pc(cpu_6502_t *cpu) {
	return cpu->pc;
}

void cpu_6502_set_pc(cpu_6502_t *cpu, const uint16_t pc) {
	cpu->pc = pc;
}

int cpu_6502_count_instr(void) {
	int i;
	int count = 0;

	for(i = 0; i < 256; i++) {
		if(instr_table[i] != x) {
			count++;
			if(len[i] < 1 || len[i] > 3)
				fprintf(stderr, "WARNING: Length(%02x) out of range!\n", i);
		} else if(len[i] != 0) {
			fprintf(stderr, "WARNING: Length(%02x) for unknown instruction!\n", i);
		}
	}

	return count;
}

#define FLAG_DISP(flag, sym) ((cpu->flags & (flag)) ? sym : '-')

void cpu_6502_print_state(cpu_6502_t *cpu, const uint32_t step) {
	printf("ST: %8d PC: %04x I: %02x A: %02x X: %02x Y: %02x SP: 01%02x [%c%c%c%c%c%c%c%c]\n", 
		step, cpu->pc, read_mem(cpu->vm, cpu->pc), 
		cpu->a, cpu->x, cpu->y, cpu->sp,
		FLAG_DISP(FLAG_NEGATIVE, 'N'),
		FLAG_DISP(FLAG_OVERFLOW, 'V'),
		FLAG_DISP(FLAG_RESERVED, 'R'),
		FLAG_DISP(FLAG_BREAK, 'B'),
		FLAG_DISP(FLAG_DECIMAL, 'D'),
		FLAG_DISP(FLAG_INTERRUPT, 'I'),
		FLAG_DISP(FLAG_ZERO, 'Z'),
		FLAG_DISP(FLAG_CARRY, 'C'));
}

DEF_CPU_INTERFACE(cpu_6502, cpu_6502_init, cpu_6502_quit, cpu_6502_reset, cpu_6502_fetch_instr, cpu_6502_exec_instr, cpu_6502_nmi, cpu_6502_irq, cpu_6502_get_pc, cpu_6502_set_pc, cpu_6502_print_state);
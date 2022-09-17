/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef CPU_INTERFACE_H_
#define CPU_INTERFACE_H_

#include <stdint.h>

typedef void* (*cpu_init_proc)(void*);
typedef void (*cpu_quit_proc)(void*);
typedef void (*cpu_reset_proc)(void*);
typedef void (*cpu_fetch_proc)(void*);
typedef int (*cpu_exec_proc)(void*, int*);
typedef int (*cpu_int_proc)(void*, int*);
typedef uint16_t (*cpu_getreg_proc)(void*);
typedef void (*cpu_setreg_proc)(void*, const uint16_t);
typedef void (*cpu_state_proc)(void*, const uint32_t);

typedef struct cpudef_t {
	cpu_init_proc init;
	cpu_quit_proc quit;
	cpu_reset_proc reset;
	cpu_fetch_proc fetch;
	cpu_exec_proc exec;
	cpu_int_proc nmi;
	cpu_int_proc irq;
	cpu_getreg_proc get_pc;
	cpu_setreg_proc set_pc;
	cpu_state_proc print_state;
} cpudef_t;

#define DEC_CPU_INTERFACE(id) \
	cpudef_t id

#define DEF_CPU_INTERFACE(id, init, quit, reset, fetch, exec, nmi, irq, getpc, setpc, print) \
	cpudef_t id = { \
		init, quit, reset, fetch, exec, nmi, irq, getpc, setpc, print \
	}

#endif
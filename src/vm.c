/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <stdint.h>
#include <stdlib.h>

#include "leakcheck.h"

#include "cpu_interface.h"
#include "mem.h"
#include "status.h"
#include "vm.h"

#include "cpu_6502.h"
#include "io_6820.h"

vm_t *vm_init(cpudef_t cpudef, int *status) {
	vm_t *out = malloc(sizeof(vm_t));

	*status = RET_ERR_ALLOC;

	if(out == NULL)
		return NULL;

	out->cpu_def = cpudef;

	if((out->cpu_state = cpudef.init(out)) == NULL) {
		free(out);
		return NULL;
	}
	
	init_mem(out);

	if(pia_init(out) != RET_OK) {
		free(out);
		return NULL;
	}

	out->quit = 0;
	out->step = 0;

	*status = RET_OK;
	return out;
}

void vm_clean(vm_t *vm) {
	vm->cpu_def.quit(vm->cpu_state);
	pia_clean();
	free(vm);
}

void vm_step(vm_t *vm, int *status) {
	cpudef_t cpu_def = vm->cpu_def;
	uint16_t old_pc = vm->cpu_def.get_pc(vm->cpu_state);
	int ret, cycles;

	cpu_def.fetch(vm->cpu_state);
	ret = vm->cpu_def.exec(vm->cpu_state, &cycles);

	pia_step(vm);
	
	vm->step++;
	vm->cycle += cycles;

	if(ret == RET_OK || ret == RET_JUMP)
		*status = RET_OK;

	if(vm->cpu_def.get_pc(vm->cpu_state) == old_pc)
		*status = RET_LOOP;

	if(vm->quit)
		*status = RET_QUIT;
}

void vm_reset(vm_t *vm) {
	vm->cpu_def.reset(vm->cpu_state);
}
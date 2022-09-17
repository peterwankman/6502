/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#undef main

#include "leakcheck.h"

#include "input.h"
#include "cpu_6502.h"
#include "mem.h"
#include "status.h"
#include "vm.h"

#define ENTRY_POINT	0

static void memdump(vm_t *vm) {
	FILE *ram = fopen("ram.bin", "wb");
	FILE *mem = fopen("mem.bin", "wb");

	if(ram == NULL) return;
	if(mem == NULL) goto closeram;

	fwrite(vm->ram, 65536, 1, ram);
	fwrite(vm->mem, 65536, 1, mem);

	fclose(mem);
closeram:
	fclose(ram);
}

static int load_and_mount(vm_t *vm, const char *filename, const uint16_t base, const size_t size) {
	int ret;

	if((ret = load_rom(vm, base, filename)) != RET_OK) {
		fprintf(stderr, "ERROR: read_rom() failed.\n");
		return ret;
	}
	mount_rom(vm, base, size);

	return ret;
}

int global_setup(void) {
	int ret;

	if((ret = input_init()) != RET_OK) return ret;
	if((ret = mmio_init()) != RET_OK) return ret;

	return RET_OK;
}

void global_clean(void) {
	input_clean();
	mmio_clean();
}

int main(void) {
	int status, show = 1;
	vm_t *vm;

	if(global_setup() != RET_OK) {
		fprintf(stderr, "ERROR: global_setup() failed.\n");
		return EXIT_FAILURE;
	}

	vm = vm_init(cpu_6502, &status);
	if(status != RET_OK) {
		fprintf(stderr, "ERROR: vm_init() failed.\n");
		return EXIT_FAILURE;
	}

/**/
	/* 
	 * Running BASIC:
	 *
	 * Put the Woz monitor into a1boot.bin and the BASIC into
	 * a1basic.bin. Boot up the machine, and go "E000R" to have
	 * tons of fun with BASIC!
	 */
/*
	if(load_and_mount(vm, "rom/a1boot.bin", 0xff00, 0x0100) != RET_OK) return EXIT_FAILURE;
	if(load_and_mount(vm, "rom/a1basic.bin", 0xe000, 0x1000) != RET_OK) return EXIT_FAILURE;
	vm->cpu_def.reset(vm->cpu_state);
*/

	/* This is a free thing to test the 6502 Emulator.  */
	if(load_and_mount(vm, "rom/test.bin", 0x0000, 0x10000) != RET_OK) return EXIT_FAILURE;
	vm->cpu_def.reset(vm->cpu_state);
	vm->cpu_def.set_pc(vm->cpu_state, 0x400);

	while(vm->quit == 0) {
		input_get();
		input_dispatch();

		vm_step(vm, &status);

		if(show) vm->cpu_def.print_state(vm->cpu_state, vm->step);

		if(status == RET_LOOP)
			vm->quit = 1;
	}

	vm_clean(vm);
	global_clean();

#ifdef _DEBUG
	mem_stats(stdout);
#endif

	return EXIT_SUCCESS;
}
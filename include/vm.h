/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef VM_H
#define VM_H

#include <stdint.h>
#include "cpu_interface.h"

#define LOC_RAM		0
#define LOC_ROM		1

typedef struct vm_t {
	uint8_t mem[65536];
	uint8_t rom[65536];
	uint8_t ram[65536];
	uint8_t mem_map[65536];
	uint32_t cycle, step;
	int quit;

	cpudef_t cpu_def;
	void *cpu_state;
} vm_t;

void mount_rom(vm_t *vm, const uint16_t addr, const size_t size);
void umount_rom(vm_t *vm, const uint16_t addr, const size_t size);
int load_rom(vm_t *vm, const size_t addr, const char *filename);

vm_t *vm_init(cpudef_t cpu_def, int *status);
void vm_clean(vm_t *vm);

void vm_step(vm_t *vm, int *status);
void vm_reset(vm_t *vm);

#endif
/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef MEM_H_
#define MEM_H_

#include <stdint.h>
#include "vm.h"

#define MEM_IGNORED		0
#define MEM_USED		1
#define MEM_INTERCEPTED	2

typedef enum mmio_type_t { MMIO_READ, MMIO_WRITE } mmio_type_t;

typedef int (*read_proc_t)(const uint16_t, uint8_t*);
typedef int (*write_proc_t)(const uint16_t, const uint8_t);

void *get_pointer(vm_t *vm, const size_t offset);

void write_mem(vm_t *vm, const uint16_t addr, const uint8_t val);
uint8_t read_mem(vm_t *vm, const uint16_t addr);
uint16_t read_ptr(vm_t *vm, const uint16_t addr);
uint16_t read_ptr_wrap(vm_t *vm, const uint16_t addr);
void init_mem(vm_t *vm);

int mmio_reg(void *proc, const mmio_type_t mmio_type);
int mmio_init(void);
void mmio_clean(void);

#endif
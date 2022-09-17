/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

/* Writes to ROM areas fall through to RAM.
 * Virtual memory keeps ROM values. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leakcheck.h"

#include "mem.h"
#include "status.h"
#include "vm.h"

typedef struct mmioproc_list_t {
	size_t n_read_reg, n_read_alloced;
	read_proc_t *read_proc;

	size_t n_write_reg, n_write_alloced;
	write_proc_t *write_proc;
} mmioproc_list_t;

//readproc_list_t *readproc_list = NULL;
mmioproc_list_t *mmioproc_list = NULL;

void *get_pointer(vm_t *vm, const size_t offset) {
	return vm->mem + offset;
}

void write_mem(vm_t *vm, const uint16_t addr, const uint8_t val) {
	int i;

	for(i = 0; i < mmioproc_list->n_write_reg; i++)
		if(mmioproc_list->write_proc[i](addr, val) == MEM_INTERCEPTED)
			return;

	vm->ram[addr] = val;
//	if(vm->mem_map[addr] == LOC_RAM)
		vm->mem[addr] = val;
}

uint8_t read_mem(vm_t *vm, const uint16_t addr) {
	int i;
	uint8_t res;

	for(i = 0; i < mmioproc_list->n_read_reg; i++)
		if(mmioproc_list->read_proc[i](addr, &res) == MEM_INTERCEPTED)
			return res;

	return vm->mem[addr];
}

uint16_t read_ptr(vm_t *vm, const uint16_t addr) {
	return read_mem(vm, addr) | (read_mem(vm, addr + 1) << 8);
}

uint16_t read_ptr_wrap(vm_t *vm, const uint16_t addr) {
	uint16_t hibyte_addr;
	hibyte_addr = (addr & 0xff00) + ((addr + 1) & 0xff);
	return read_mem(vm, addr) | (read_mem(vm, hibyte_addr) << 8);
}

void mount_rom(vm_t *vm, const uint16_t addr, const size_t size) {
	memcpy(vm->mem + addr, vm->rom + addr, size);
	memset(vm->mem_map, LOC_ROM, size);
}

void umount_rom(vm_t *vm, const uint16_t addr, const size_t size) {
	memcpy(vm->mem + addr, vm->ram + addr, size);
	memset(vm->mem_map, LOC_RAM, size);
}

static void read_rom(vm_t *vm, const uint16_t addr, const uint8_t *data, const size_t size) {
	memcpy(vm->rom + addr, data, size);
}

int load_rom(vm_t *vm, const size_t addr, const char *filename) {
	FILE *fp;
	size_t size;
	uint8_t *data;
	int ret = RET_ERR_ALLOC;

	if((fp = fopen(filename, "rb")) == NULL)
		return RET_ERR_OPEN;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	ret = RET_ERR_ALLOC;
	if((data = malloc(size)) == NULL)  goto close;
	if(fread(data, 1, size, fp) != size) goto freedata;
	read_rom(vm, addr, data, size);
	ret = RET_OK;

freedata:
	free(data);
close:
	fclose(fp);
	return ret;
}

void init_mem(vm_t *vm) {
	int i;
	uint8_t val = 0;
	size_t pos = 0;
	
	for(i = 0; i < 1024; i++) {
		memset(vm->ram + pos, 0, 0x40);
		memset(vm->mem + pos, 0, 0x40);
		memset(vm->mem_map, LOC_RAM, 0x40);
		pos += 0x40;
		val = ~val;
	}
}

static void *expandlist(void *list, size_t *n_reg, size_t *n_alloced) {
	void *newdata = list;

	if(*n_reg == *n_alloced) {
		if((newdata = malloc((*n_reg + PREALLOC_LIST) * sizeof(void*))) == NULL)
			return list;
		memcpy(newdata, list, *n_reg * sizeof(void*));
		*n_alloced += PREALLOC_LIST;
	}

	return newdata;
}

static int readproc_reg(const read_proc_t proc) {
	read_proc_t *procs = mmioproc_list->read_proc;
	size_t n_reg = mmioproc_list->n_read_reg;
	size_t n_alloced = mmioproc_list->n_read_alloced;

	if((procs = expandlist(procs, &n_reg, &n_alloced)) == NULL)
		return RET_ERR_ALLOC;

	mmioproc_list->read_proc = procs;
	mmioproc_list->read_proc[n_reg] = proc;
	mmioproc_list->n_read_reg++;

	return RET_OK;
}

static int writeproc_reg(const write_proc_t proc) {
	write_proc_t *procs = mmioproc_list->write_proc;
	size_t n_reg = mmioproc_list->n_write_reg;
	size_t n_alloced = mmioproc_list->n_write_alloced;

	if((procs = expandlist(procs, &n_reg, &n_alloced)) == NULL)
		return RET_ERR_ALLOC;

	mmioproc_list->write_proc = procs;
	mmioproc_list->write_proc[n_reg] = proc;
	mmioproc_list->n_write_reg++;

	return RET_OK;
}

int mmio_reg(void *proc, const mmio_type_t mmio_type) {
	switch(mmio_type) {
		case MMIO_READ:		return readproc_reg(proc); break;
		case MMIO_WRITE:	return writeproc_reg(proc);	break;
	}	

	return RET_ERR_INVAL;
}

int mmio_init(void) {
	if((mmioproc_list = malloc(sizeof(mmioproc_list_t))) == NULL) goto fail;
	if((mmioproc_list->write_proc = malloc(PREALLOC_LIST * sizeof(write_proc_t))) == NULL) goto freelist;
	if((mmioproc_list->read_proc = malloc(PREALLOC_LIST * sizeof(read_proc_t))) == NULL) goto freewrite;
	mmioproc_list->n_read_reg = 0;
	mmioproc_list->n_read_alloced = PREALLOC_LIST;
	mmioproc_list->n_write_reg = 0;
	mmioproc_list->n_write_alloced = PREALLOC_LIST;

	return RET_OK;

freewrite:
	free(mmioproc_list->write_proc);
freelist:
	free(mmioproc_list);
fail:
	return RET_ERR_ALLOC;
}

void mmio_clean(void) {
	free(mmioproc_list->read_proc);
	free(mmioproc_list->write_proc);
	free(mmioproc_list);
}
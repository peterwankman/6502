/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef CPU_6502_H
#define CPU_6502_H

#include "cpu_interface.h"
#include "vm.h"

#define FLAG_NEGATIVE	0x80
#define FLAG_OVERFLOW	0x40
#define FLAG_RESERVED	0x20
#define FLAG_BREAK		0x10
#define FLAG_DECIMAL	0x08
#define FLAG_INTERRUPT	0x04
#define FLAG_ZERO		0x02
#define FLAG_CARRY		0x01

typedef struct cpu_6502_t cpu_6502_t;

cpudef_t cpu_6502;

#endif
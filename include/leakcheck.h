/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifdef _DEBUG

#ifndef LEAKCHECK_H_
#define LEAKCHECK_H_

#include <stdio.h>

#define malloc(size) mem_malloc((size), __FILE__, __LINE__)
#define free(memory) mem_free(memory)

int n_mallocs, n_frees;

void *mem_malloc(size_t size, char *file, int line);
void mem_free(void *memory);
size_t mem_stats(FILE *fp);

#endif
#endif
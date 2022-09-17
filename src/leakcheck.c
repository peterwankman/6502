/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

int n_mallocs = 0;
int n_frees = 0;

typedef struct alloclist_t {
	void *memory;
	char *file;
	int line;
	struct alloclist_t *next;
} alloclist_t;

alloclist_t *alloc_list = NULL;

static alloclist_t *make_entry(void *memory, char *file, int line) {
	alloclist_t *out;
	
	if((out = malloc(sizeof(alloclist_t))) == NULL)
		return NULL;	

	out->memory = memory;
	out->line = line;
	out->file = file;	
	out->next = NULL;

	return out;
}

static void addtolist(alloclist_t *entry) {
	alloclist_t *current = alloc_list, *last = NULL;
	if(!current) {
		alloc_list = entry;
		return;
	}

	while(current) {		
		last = current;
		current = current->next;
	}
	last->next = entry;
}

static int delfromlist(void *memory) {
	alloclist_t *current = alloc_list, *last = NULL;

	while(current) {
		if(current->memory == memory) {
			if(last) {
				last->next = current->next;
			} else {
				alloc_list = current->next;
			}
			free(current);
			return 1;
		}
		last = current;
		current = current->next;
	}

	return 0;
}

void *mem_malloc(size_t size, char *file, int line) {
	void *memory = malloc(size);
	alloclist_t *newentry;	

	if((newentry = make_entry(memory, file, line)))
		addtolist(newentry);

	n_mallocs++;
	return memory;
}

void mem_free(void *memory) {
	if(delfromlist(memory)) {
		n_frees++;
		free(memory);
	} else {
		fprintf(stderr, "WARNING: Called free() on unknown pointer.\n");
	}
}

size_t mem_stats(FILE *fp) {
	alloclist_t *current = alloc_list, *buf;

	fprintf(fp, "\n--- Allocation summary ---\n");
	if(n_frees < n_mallocs) {
		fprintf(fp, "Showing unfreed memory:\n");
		while(current) {
			fprintf(fp, "%s, %d\n", current->file, current->line);
			buf = current;
			current = current->next;
			free(buf);
		}
	}

	fprintf(fp, "%d allocs; %d frees.\n", n_mallocs, n_frees);
	if(n_mallocs == n_frees)
		fprintf(fp, "All allocated blocks were free'd. No leaks detected.\n");
	printf("--- end summary ---\n");

	return n_mallocs - n_frees;
}
/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <SDL.h>

#include "leakcheck.h"

#include "input.h"
#include "status.h"

typedef struct input_queue_t {
	input_t *event;
	struct input_queue_t *next;
} input_queue_t;

typedef struct handler_list_t {
	size_t n_registered, n_alloced;
	input_proc_t *proc;
} handler_list_t;

input_queue_t *queue_start = NULL;
input_queue_t *queue_end = NULL;

static handler_list_t *kb_handlers;
static handler_list_t *btn_handlers;
static handler_list_t *move_handlers;

static key_input_t *get_keys(SDL_KeyboardEvent ev, Uint32 type) {
	key_input_t *out;

	if((out = malloc(sizeof(key_input_t))) == NULL)
		return NULL;
	
	switch(type) {
		case SDL_KEYDOWN:	out->type = DOWN;	break;
		case SDL_KEYUP:		out->type = UP;		break;
		default:			out->type = NONE;
	}

	out->Keysym = ev.keysym;

	return out;
}

static mouse_input_t *get_mouse_button(SDL_MouseButtonEvent ev, Uint32 type) {
	mouse_input_t *out;

	if((out = malloc(sizeof(mouse_input_t))) == NULL)
		return NULL;

	switch(type) {
		case SDL_MOUSEBUTTONUP:		out->dir = UP;		break;
		case SDL_MOUSEBUTTONDOWN:	out->dir = DOWN;	break;
		default:					out->dir = NONE;
	}

	out->type = BUTTON;
	out->Button = ev;

	return out;
}

static mouse_input_t *get_mouse_motion(SDL_MouseMotionEvent ev) {
	mouse_input_t *out;

	if((out = malloc(sizeof(mouse_input_t))) == NULL)
		return NULL;
	
	out->dir = NONE;
	out->type = MOTION;
	out->Motion = ev;
	return out;
}

static mouse_input_t *get_mouse_wheel(SDL_MouseWheelEvent ev) {
	mouse_input_t *out;

	if((out = malloc(sizeof(mouse_input_t))) == NULL)
		return NULL;

	if(ev.y > 0)
		out->dir = UP;
	else if(ev.y < 0)
		out->dir = DOWN;
	else
		out->dir = NONE;

	out->type = WHEEL;
	out->Wheel = ev;
	return out;
}

static int add_to_queue(input_t *input) {
	input_queue_t *newent;

	if((newent = malloc(sizeof(input_queue_t))) == NULL)
		return 0;

	newent->event = input;
	newent->next = NULL;

	if(queue_end == NULL) {
		queue_start = newent;
	} else {
		queue_end->next = newent;
	}

	queue_end = newent;
	return 1;
}

void input_get(void) {
	SDL_Event ev;
	input_t *input;
	key_input_t *key_input;
	mouse_input_t *mouse_input;

	while(SDL_PollEvent(&ev)) {
		input = NULL;
		key_input = NULL;
		mouse_input = NULL;

		switch(ev.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				key_input = get_keys(ev.key, ev.type);
				break;
			/*
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
				mouse_input = get_mouse_button(ev.button, ev.type);
				break;
			case SDL_MOUSEMOTION:
				mouse_input = get_mouse_motion(ev.motion);
				break;
			case SDL_MOUSEWHEEL:
				mouse_input = get_mouse_wheel(ev.wheel);
				break;
			*/
		}

		if(key_input) {
			if((input = malloc(sizeof(input_t))) == NULL) {
				free(key_input);
				continue;
			}
			input->type = KEYBOARD;
			input->key = key_input;
		} else if(mouse_input) {
			if((input = malloc(sizeof(input_t))) == NULL) {
				free(mouse_input);
				continue;
			}
			input->type = MOUSE;
			input->mouse = mouse_input;
		}

		if(input) {
			add_to_queue(input);
		}
	}
}

static int dispatch_keyboard(key_input_t *key_input) {
	int ret = INPUT_IGNORED;
	size_t i;

	if(key_input == NULL)
		return INPUT_IGNORED;

	for(i = 0; i < kb_handlers->n_registered; i++)
		if((ret = kb_handlers->proc[i](key_input)) == INPUT_CONSUMED) goto done;

done:
	free(key_input);
	return ret;
}

static int dispatch_mouse(mouse_input_t *mouse_input) {
	int ret = INPUT_IGNORED;
	size_t i;

	if(mouse_input == NULL)
		return ret;

	if(mouse_input->type != MOTION) {
		for(i = 0; i < btn_handlers->n_registered; i++)
			if(btn_handlers->proc[i](mouse_input) == INPUT_CONSUMED) goto done;
	} else {
		for(i = 0; i < move_handlers->n_registered; i++)
			move_handlers->proc[i](mouse_input);
	}

done:
	free(mouse_input);
	return ret;
}

void input_dispatch(void) {
	input_queue_t *current;
	input_t *input;

	while(queue_start != NULL) {
		current = queue_start;
		input = current->event;
		
		if(input) {
			if(input->type == KEYBOARD) {
				dispatch_keyboard(input->key);
			} else if(input->type == MOUSE) {
				dispatch_mouse(input->mouse);
			} free(input);
		}

		if(current == queue_end)
			queue_end = NULL;

		queue_start = queue_start->next;
		free(current);
	}
}

int input_reg(input_proc_t proc, const handler_type_t handler_type) {
	handler_list_t *handler;
	input_proc_t *newprocs;

	switch(handler_type) {
		case HPROC_KEYBOARD:	handler = kb_handlers;		break;
		case HPROC_MBUTTON:	handler = btn_handlers;		break;
		case HPROC_MOTION:	handler = move_handlers;	break;

		default:
			return RET_ERR_INVAL;
	}

	if(handler->n_registered == handler->n_alloced) {
		if((newprocs = malloc((handler->n_registered + PREALLOC_LIST) * sizeof(input_proc_t))) == NULL)
			return RET_ERR_ALLOC;

		memcpy(newprocs, handler->proc, handler->n_registered * sizeof(input_proc_t));
		free(handler->proc);
		handler->proc = newprocs;
		handler->n_alloced += PREALLOC_LIST;
	}

	handler->proc[handler->n_registered] = proc;
	handler->n_registered++;

	return RET_OK;
}

int input_init(void) {
	if((kb_handlers = malloc(sizeof(handler_list_t))) == NULL) goto fail;
	if((kb_handlers->proc = malloc(PREALLOC_LIST * sizeof(input_proc_t))) == NULL) goto freekb;
	kb_handlers->n_registered = 0;
	kb_handlers->n_alloced = PREALLOC_LIST;

	if((btn_handlers = malloc(sizeof(handler_list_t))) == NULL) goto freekbproc;
	if((btn_handlers->proc = malloc(PREALLOC_LIST * sizeof(input_proc_t))) == NULL) goto freebtn;
	btn_handlers->n_registered = 0;
	btn_handlers->n_alloced = PREALLOC_LIST;

	if((move_handlers = malloc(sizeof(handler_list_t))) == NULL) goto freebtnproc;
	if((move_handlers->proc = malloc(PREALLOC_LIST * sizeof(input_proc_t))) == NULL) goto freemove;
	move_handlers->n_registered = 0;
	move_handlers->n_alloced = PREALLOC_LIST;

	return RET_OK;

freemove:
	free(move_handlers);
freebtnproc:
	free(btn_handlers->proc);
freebtn:
	free(btn_handlers);
freekbproc:
	free(kb_handlers->proc);
freekb:
	free(kb_handlers);
fail:
	return RET_ERR_ALLOC;
}

void input_clean(void) {
	free(kb_handlers->proc);
	free(kb_handlers);

	free(btn_handlers->proc);
	free(btn_handlers);

	free(move_handlers->proc);
	free(move_handlers);
}
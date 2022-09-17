/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef INPUT_H_
#define INPUT_H_

#include <SDL.h>

#define INPUT_CONSUMED	1
#define INPUT_SHARED	2
#define INPUT_IGNORED	3

typedef enum handler_type_t { HPROC_KEYBOARD, HPROC_MBUTTON, HPROC_MOTION } handler_type_t;
typedef enum mouse_input_type_t { BUTTON, MOTION, WHEEL } mouse_input_type_t;
typedef enum key_input_type_t { UP, DOWN, NONE } key_input_type_t;
typedef enum input_type_t { KEYBOARD, MOUSE } input_type_t;

typedef struct mouse_input_t {
	mouse_input_type_t type;
	key_input_type_t dir;
	union {
		SDL_MouseButtonEvent Button;
		SDL_MouseMotionEvent Motion;
		SDL_MouseWheelEvent Wheel;
	};
} mouse_input_t;

typedef struct key_input_t {
	key_input_type_t type;
	SDL_Keysym Keysym;
} key_input_t;

typedef struct input_t {
	input_type_t type;
	union {
		mouse_input_t *mouse;
		key_input_t *key;
	};
} input_t;

typedef int (*input_proc_t)(void*);

void input_get(void);
void input_dispatch(void);

int input_reg(input_proc_t proc, const handler_type_t handler_type);
int input_init(void);
void input_clean(void);

#endif
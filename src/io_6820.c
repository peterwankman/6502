/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#include <ctype.h> /*!!*/
#include <stdio.h>

#include <SDL.h>

#include "leakcheck.h"

#include "input.h"
#include "mem.h"
#include "status.h"
#include "vm.h"

#define CHAR_WIDTH	6
#define CHAR_HEIGHT 8
#define CHAR_COL_R	0xff
#define CHAR_COL_G	0xff
#define CHAR_COL_B	0xff

#define SCR_COLS	60
#define SCR_ROWS	36
#define SCR_SCALE	2

#define SCR_WIDTH	(CHAR_WIDTH * SCR_COLS * SCR_SCALE)
#define SCR_HEIGHT	(CHAR_HEIGHT * SCR_ROWS * SCR_SCALE)

#define SCR_TITLE	"A1 Console"

#define scrpos(x, y) ((y) * SCR_COLS + (x))

#define KBD_DATA		0xd010
#define KBD_CR			0xd011
#define DSP_DATA		0xd012
#define DSP_CR			0xd013

#define	DSP_READY		0x80
#define BLINK_DELAY		400

#define CSR_CHAR		'@'		/* For more authenticity */
//#define CSR_CHAR		'_'		/* For more beauticity */

typedef struct vidinfo_t {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *char_texture[128];
} vidinfo_t;

typedef struct scrinfo_t {
	uint8_t cell[SCR_COLS * SCR_ROWS];
	uint32_t col, row;
	int last_blink, show_cursor;
} scrinfo_t;

typedef struct reginfo_t {
	uint8_t kbd_data, kbd_cr;
	uint8_t dsp_data, dsp_cr;
} reginfo_t;

static vidinfo_t *video;
static scrinfo_t screen;
static vm_t *g_vm = NULL;
static reginfo_t reginfo;

static void init_screen(void) {
	int i;
	for(i = 0; i < SCR_COLS * SCR_ROWS; i++)
		screen.cell[i] = 0;

	screen.col = screen.row = 0;
}

static void pia_reset(void) {
	init_screen();

	reginfo.kbd_cr = 0;
	reginfo.kbd_data = 0x80;
	reginfo.dsp_cr = 0;
	reginfo.dsp_data = 0;
}

static void scroll(void) {
	int x, y;

	for(y = 1; y < SCR_ROWS; y++) {
		for(x = 0; x < SCR_COLS; x++) {
			screen.cell[scrpos(x, y - 1)] = screen.cell[scrpos(x, y)];
		}
	}
	for(x = 0; x < SCR_COLS; x++)
		screen.cell[scrpos(x, SCR_ROWS - 1)] = 0;
}

static void pia_chrout(void) {
	uint8_t data, c;

	data = reginfo.dsp_data;

	data &= 0x7f;

	if(data == '\n' || data == '\r') {
		screen.col = 0;
		screen.row++;
	} else {
		c = (data > 0x5f) ? data & 0x5f : data;
		screen.cell[screen.row * SCR_COLS + screen.col] = c;
		screen.col++;
	}

	if(screen.col == SCR_COLS) {
		screen.col = 0;
		screen.row++;
	}

	if(screen.row == SCR_ROWS) {
		scroll();
		screen.row--;
	}

	reginfo.dsp_data = data & 0x7f;
}

static uint8_t shift(const uint8_t key) {
	switch(key) {
		case '1': return '!';
		case '2': return '"';
		case '3': return '?';
		case '4': return '$';
		case '5': return '%';
		case '6': return '&';
		case '7': return '/';
		case '8': return '(';
		case '9': return ')';
		case '0': return '=';

		case '.': return ':';
		case ',': return ';';
		case '<': return '>';

		case '-': return '_';
		case '+': return '*';
		case '#': return '\'';
		
		case 'q': return '@';
	}

	if((key >= 'a') && (key <= 'z'))
		return key - 'a' + 'A';

	return 0xff;
}

static int pia_keyboard(key_input_t *key_input) {
	uint8_t key = key_input->Keysym.sym;
	uint8_t c;

	if(key_input->type == DOWN) {
		if(key_input->Keysym.sym == SDLK_ESCAPE) {
			g_vm->quit = 1;
		} else if(key_input->Keysym.sym == SDLK_F1) {
			pia_reset();
			vm_reset(g_vm);
			init_screen();
		} else {
			if(key_input->Keysym.mod & KMOD_SHIFT) {
				key = shift(key);
			}
			if(key == '\b') key = 0x5f;

			c = key & 0x7f;
			
			if ((c > 0x60) && (c < 0x7b))
				c &= 0x5f;

			if(c < 0x60) {
				reginfo.kbd_data = (c | 0x80);
				reginfo.kbd_cr = 0xa7;
			}
		}
	}
	return INPUT_CONSUMED;
}

static int load_charmap(const char *filename) {
	int x, y, n;
	uint8_t c;
	FILE *fp = fopen(filename, "rb");
	SDL_Texture *texture;
	uint32_t *pixels;
	uint32_t pixel_off, pixel_on;
	SDL_PixelFormat *format;
	int pitch;

	if((format = SDL_AllocFormat(SDL_GetWindowPixelFormat(video->window))) == NULL)
		return RET_ERR_SDL;

	pixel_off = SDL_MapRGB(format, 0, 0, 0);
	pixel_on = SDL_MapRGB(format, CHAR_COL_R, CHAR_COL_G, CHAR_COL_B);

	if(fp == NULL) 
		return RET_ERR_OPEN;

	for(n = 0; n < 128; n++) {
		if((texture = SDL_CreateTexture(video->renderer, 
			SDL_GetWindowPixelFormat(video->window), SDL_TEXTUREACCESS_STREAMING,
			CHAR_WIDTH, CHAR_HEIGHT)) == NULL) {
				return RET_ERR_SDL;
		}

		SDL_LockTexture(texture, NULL, &pixels, &pitch);

		for(y = 0; y < CHAR_HEIGHT; y++) {
			c = fgetc(fp);
			for(x = 0; x < CHAR_WIDTH; x++) {
				if(c & (1 << x))
					pixels[y * CHAR_WIDTH + x] = pixel_on;
				else
					pixels[y * CHAR_WIDTH + x] = pixel_off;
			}
		}

		SDL_UnlockTexture(texture);
		video->char_texture[n] = texture;
		SDL_FreeFormat(format);

	}

	fclose(fp);

	return RET_OK;
}

static void render(int redraw) {
	int x, y;
	uint8_t c;
	SDL_Rect rect;

	if(SDL_GetTicks() - screen.last_blink > BLINK_DELAY) {
		screen.show_cursor = screen.show_cursor ? 0 : 1;
		screen.last_blink = SDL_GetTicks();
		redraw = 1;
	}

	if(redraw) {
		SDL_RenderClear(video->renderer);

		rect.w = CHAR_WIDTH * SCR_SCALE;
		rect.h = CHAR_HEIGHT * SCR_SCALE;

		for(x = 0; x < SCR_COLS; x++) {
			for(y = 0; y < SCR_ROWS; y++) {
				rect.x = x * CHAR_WIDTH * SCR_SCALE;
				rect.y = y * CHAR_HEIGHT * SCR_SCALE;
			
				c = screen.cell[y * SCR_COLS + x];

				SDL_RenderCopy(video->renderer, video->char_texture[c], NULL, &rect);
			}
		}

		if(screen.show_cursor) {
			rect.x = screen.col * CHAR_WIDTH * SCR_SCALE;
			rect.y = screen.row * CHAR_HEIGHT * SCR_SCALE;
			SDL_RenderCopy(video->renderer, video->char_texture['_'], NULL, &rect);
		}
		SDL_RenderPresent(video->renderer);
	}
}

void pia_step(vm_t *vm) {
	int redraw = 0;

	if(reginfo.dsp_data & 0x80) {
		pia_chrout();
		redraw = 1;
	}

	render(redraw);
}

static int hook_read(uint16_t addr, uint8_t *res) {
	switch(addr) {
		case KBD_DATA:
			reginfo.kbd_cr = 0x27;
			*res = reginfo.kbd_data;
			return MEM_INTERCEPTED;
		
		case KBD_CR:
			*res = reginfo.kbd_cr;
			return MEM_INTERCEPTED;

		case DSP_DATA:
			*res = reginfo.dsp_data;
			return MEM_INTERCEPTED;

		case DSP_CR:
			*res = reginfo.dsp_cr;
			return MEM_INTERCEPTED;
	}

	return MEM_IGNORED;
}

static int hook_write(const uint16_t addr, const uint8_t val) {
	int ret = MEM_IGNORED;

	switch(addr) {
		case KBD_DATA:
			reginfo.kbd_data = val;
			ret = MEM_USED; break;

		case KBD_CR:
			if(reginfo.kbd_cr == 0)
				reginfo.kbd_cr = 0x27;
			else
				reginfo.kbd_cr = val;
			ret = MEM_USED; break;

		case DSP_DATA:
			if(reginfo.dsp_cr & 0x04)
				reginfo.dsp_data = val | 0x80;
			ret = MEM_USED; break;

		case DSP_CR:
			reginfo.dsp_cr = val;
			ret = MEM_USED; break;
	}

	return ret;
}

int pia_init(vm_t *vm) {
	int ret = RET_ERR_SDL;
	
	if((video = malloc(sizeof(vidinfo_t))) == NULL) {
		fprintf(stderr, "pia_init(): ERROR! malloc() failed.\n");
		return RET_ERR_ALLOC;
	}

	if((video->window = SDL_CreateWindow(SCR_TITLE, SDL_WINDOWPOS_UNDEFINED, 
		SDL_WINDOWPOS_UNDEFINED, SCR_WIDTH, SCR_HEIGHT, SDL_WINDOW_SHOWN)) == NULL) {

		fprintf(stderr, "pia_init(): ERROR! SDL_CreateWindow() failed: %s\n", SDL_GetError());
		goto freevid;
	}

	if((video->renderer = SDL_CreateRenderer(video->window, -1, SDL_RENDERER_ACCELERATED)) == NULL) {
		fprintf(stderr, "pia_init(): ERROR! SDL_CreateRenderer() failed: %s\n", SDL_GetError());
		goto freewindow;
	}

	if((ret = load_charmap("rom/a1chr.bin")) != RET_OK) {
		goto freerenderer;
	}
	
	SDL_SetRenderDrawColor(video->renderer, 0x00, 0x00, 0x00, 0xff);

	screen.last_blink = SDL_GetTicks();
	screen.show_cursor = 0;

	input_reg(pia_keyboard, HPROC_KEYBOARD);
	mmio_reg(hook_write, MMIO_WRITE);
	mmio_reg(hook_read, MMIO_READ);

	g_vm = vm;

	pia_reset();
	return RET_OK;

freerenderer:
	SDL_DestroyRenderer(video->renderer);
freewindow:
	SDL_DestroyWindow(video->window);
freevid:
	free(video);
	video = NULL;

	return ret;
}

void pia_clean(void) {
	int i;

	for(i = 0; i < 128; i++)
		SDL_DestroyTexture(video->char_texture[i]);

	SDL_DestroyRenderer(video->renderer);
	SDL_DestroyWindow(video->window);
	free(video);
}

/*
 * Copyright (c) 2023, Ilya Kurdyukov
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <time.h>
#include <sys/time.h>
static uint64_t get_time_usec() {
	struct timeval time;
	gettimeofday(&time, NULL);
	return time.tv_sec * 1000000LL + time.tv_usec;
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DISP_CHECK_START 176
#define DISP_CHECK_END 216
#define DISP_CHECK_SIZE (DISP_CHECK_END - DISP_CHECK_START)

#ifndef USE_GAMEPAD
#define USE_GAMEPAD 1
#endif

#if USE_GAMEPAD
#include <linux/joystick.h>
#include <sys/poll.h>
#include <fcntl.h>
#endif

// to display memory map without flickering
#ifndef NO_FLICKER
#define NO_FLICKER 200
#endif

typedef struct {
	struct termios tcattr;
#ifdef DECOMPILED
	uint64_t last_time;
	unsigned tmr_frac;
	uint32_t randseed;
#endif
#if NO_FLICKER
	uint16_t memcopy[256];
#endif
#if USE_GAMEPAD
	int js_fd;
	uint32_t js_keys;
	int js_axes, js_buttons;
	int8_t *js_ax;
	int8_t *js_btn;
#endif
	unsigned hold_time, sleep_ticks, sleep_delay, timer_inc;
	uint32_t misc;
	uint32_t keys;
	uint64_t key_timers[8];
	uint16_t old_rows[20];
	uint32_t old_score;
	uint16_t old_next, old_speed, old_level;
	uint8_t old_mem[DISP_CHECK_SIZE];
	uint8_t disp_mask[DISP_CHECK_SIZE];
	uint16_t disp_pos[DISP_CHECK_SIZE][4];
	char disp_buf[1024];
} sysctx_t;

#if USE_GAMEPAD
#include <linux/joystick.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>

static void sys_gamepad_init(sysctx_t *sys) {
	uint8_t axmap[ABS_CNT];
	uint16_t btnmap[KEY_MAX - BTN_MISC + 1];
	uint8_t buttons, axes;
	int i;

	if (ioctl(sys->js_fd, JSIOCGAXES, &axes) < 0)
		ERR_EXIT("ioctl(JSIOCGAXES) failed\n");
	if (ioctl(sys->js_fd, JSIOCGAXMAP, axmap) < 0)
		ERR_EXIT("ioctl(JSIOCGAXMAP) failed\n");

	sys->js_axes = axes;
	sys->js_ax = malloc(axes * sizeof(*sys->js_ax));
	if (!sys->js_ax) ERR_EXIT("malloc failed\n");
	memset(sys->js_ax, -1, axes * sizeof(*sys->js_ax));
	for (i = 0; i < axes; i++)
		switch (axmap[i]) {
		case ABS_X:
		case ABS_HAT0X:
			sys->js_ax[i] = 0x32; // left/right
			break;
		case ABS_Y:
		case ABS_HAT0Y:
			sys->js_ax[i] = 0x01; // up/down
			break;
		}

	if (ioctl(sys->js_fd, JSIOCGBUTTONS, &buttons) < 0)
		ERR_EXIT("ioctl(JSIOCGBUTTONS) failed\n");
	if (ioctl(sys->js_fd, JSIOCGBTNMAP, btnmap) < 0)
		ERR_EXIT("ioctl(JSIOCGBTNMAP) failed\n");

	sys->js_buttons = buttons;
	sys->js_btn = malloc(buttons * sizeof(*sys->js_btn));
	if (!sys->js_btn) ERR_EXIT("malloc failed\n");
	memset(sys->js_btn, -1, buttons * sizeof(*sys->js_btn));
	for (i = 0; i < buttons; i++)
		switch (btnmap[i]) {
		case BTN_A:
		case BTN_B:
		case BTN_X:
		case BTN_Y:
			sys->js_btn[i] = 0; // rotate
			break;
		case BTN_TL: // L1
		case BTN_TR: // R1
			sys->js_btn[i] = 17; // memory map
			break;
		case BTN_SELECT:
			sys->js_btn[i] = 5; // mute
			break;
		case BTN_START:
			sys->js_btn[i] = 4; // start/pause
			break;
		case BTN_MODE:
			sys->js_btn[i] = 6; // on/off
			break;
		}
}

static void sys_gamepad_events(sysctx_t *sys) {
	struct js_event event;
	struct pollfd fds = { 0 };
	fds.fd = sys->js_fd;
	fds.events = POLLIN;
	while (poll(&fds, 1, 0)) {
		int key, state;
		int n = read(sys->js_fd, &event, sizeof(event));
		if (n != sizeof(event)) {
			close(sys->js_fd);
			sys->js_fd = -1;
			break;
		}
		key = -1;
		if (event.type == JS_EVENT_BUTTON) {
			if (event.number < sys->js_buttons)
				key = sys->js_btn[event.number];
		} else if (event.type == JS_EVENT_AXIS) {
			if (event.number < sys->js_axes)
				key = sys->js_ax[event.number] | -0x100;
		}

		if (key == -1) continue;
		if (key & 0x100) {
			int mask, value = event.value;
			int thr = 0x4000; // 0.5

			mask = 1 << (key >> 4 & 15);
			if (value <= -thr) sys->js_keys |= mask;
			else sys->js_keys &= ~mask;

			mask = 1 << (key & 15);
			if (value >= thr) sys->js_keys |= mask;
			else sys->js_keys &= ~mask;

		} else if (key == 17) { // memory map
			if (event.value == 1) 
				sys->js_keys ^= 1 << key;
		} else if (event.value == 1) { // press
			sys->js_keys |= 1 << key;
		} else if (event.value == 0) { // release
			sys->js_keys &= ~(1 << key);
		}
	}
}
#endif

static inline int sys_keys(sysctx_t *sys) {
#if USE_GAMEPAD
	return sys->keys | sys->js_keys;
#else
	return sys->keys;
#endif
}

// ps: start/pause, mute, on/off
// pp: rotate, down, right, left

static int sys_events(sysctx_t *sys) {
	uint64_t time = get_time_usec();
	unsigned hold_time = sys->hold_time * 1000;
	int i;
	for (i = 0; i < 8; i++)
		if (time - sys->key_timers[i] > hold_time)
			sys->keys &= ~(1 << i);

#define SET_KEY(key) do { \
	sys->keys |= 1 << key; \
	sys->key_timers[key] = time; \
} while (0)

	for (;;) {
		int a, n, status = 0;
		char buf[8];
		n = read(0, &buf, sizeof(buf));
		for (i = 0; i < n; i++) {
			int key = -1;
			a = buf[i];
			if (a == 0x1b) status = 1;
			else if (a == 0x5b && status == 1) status = 2;
			else if (status == 2) {
				if (a == 0x41) /* UP */ key = 0;
				else if (a == 0x42) /* DOWN */ key = 1;
				else if (a == 0x43) /* RIGHT */ key = 2;
				else if (a == 0x44) /* LEFT */ key = 3;
				status = 0;
			}
			else if (a == 10) key = 4; // enter = start/pause
			else if (a == 32) key = 0; // space = rotate
			else if (a == 9) sys->keys ^= 1 << 17; // tab = memory map
			else switch (a | 32) {
			case 'w': key = 0; break; // w = up
			case 'a': key = 3; break; // a = left
			case 's': key = 1; break; // s = down
			case 'd': key = 2; break; // d = right
			case 'p': key = 4; break; // p = start/pause
			case 'm': key = 5; break; // m = mute
			case 'r': key = 6; break; // r = on/off
			default: status = 0;
			}
			if (key >= 0) SET_KEY(key);
		}
		if (n != sizeof(buf)) {
			if (status == 1) // escape = exit
				sys->keys |= 1 << 16;
			break;
		}
	}
#undef SET_KEY
#if USE_GAMEPAD
	if (sys->js_fd >= 0) sys_gamepad_events(sys);
#endif
	return sys_keys(sys);
}

typedef struct {
	uint8_t off, bit;
	char row, col, empty;
	const char *str;
} disp_item_t;

static const disp_item_t disp_item[] = {
	{ 177,1, 3,24, -1,"GAME OVER" },
	{ 177,2, 1,30, -1,"0" }, // score xxxxx0x
	{ 177,3, 1,31, -1,"0" }, // score xxxxxx0
	{ 178,0, 8,33, -1,"!" }, // food 2, starfish
	{ 178,1, 9,33, -1,"@" }, // food 3, mushroom
	{ 178,3, 7,33, -1,"~" }, // food 1
	{ 180,0, 13,33, -1,"^" }, // food 7, strawberry
	{ 180,1, 14,33, -1,"&" }, // food 8, lime
	{ 180,2, 12,33, -1,"%" }, // food 6, radish
	{ 180,3, 15,33, -1,"*" }, // food 9, pumpkin
	{ 181,0, 16,33, -1,"+" }, // food 10, grapes
	{ 181,1, 17,33, -1,"=" }, // food 11, tomato
	{ 181,2, 19,33, -1,"o" }, // food 13, cherry
	{ 181,3, 18,33, -1,"x" }, // food 12, banana
	{ 182,0, 15,25, -1,"GAME A" },
	{ 182,1, 16,25, -1,"GAME B" },
	{ 182,2, 13,24, -1,"LEVEL" },
	{ 182,3, 17,25, -1,"ROTATE" },
	{ 183,0, 18,26, -1,"<--" },
	{ 183,1, 19,27, -1,"-->" },
	{ 183,2, 23,24, -1,"TEA TIME" },
	{ 183,3, 21,25, -1,"PAUSE" },
	{ 187,0, 5,24, -1,"NEXT" },
	{ 193,0, 1,16, -1,"LINES" },
	{ 193,2, 1,10, -1,"SCORE" },
	{ 193,3, 1,25, -1,"1" }, // score 1xxxx__
	{ 195,0, 2,4, -1,"SOUND" },
	{ 195,2, 1,7, -1,"HI-" },
	{ 197,0, 10,33, -1,"#" }, // food 4, eggplant
	{ 197,1, 11,24, -1,"SPEED" },
	{ 202,2, 11,30, -1,"1" }, // speed 1x
	{ 205,0, 11,33, -1,"$" }, // food 5
	{ 210,2, 13,30, -1,"1" }, // level 1x
	{ 0 }
};

static void sys_init(sysctx_t *sys) {
	struct termios tcattr_new;

	memset(sys, 0, sizeof(*sys));

	tcgetattr(0, &sys->tcattr);
	tcattr_new = sys->tcattr;
	tcattr_new.c_lflag &= ~(ICANON|ECHO);
	tcattr_new.c_cc[VMIN] = 0;
	tcattr_new.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &tcattr_new);

	{
		uint64_t time = get_time_usec();
		int i;
		for (i = 0; i < 7; i++) sys->key_timers[i] = time;
	}

	printf("\33[2J\33[?25l"); // clear screen, hide cursor
	{
		int y = 3;
		printf("\33[%uH/--------------------\\", y++);
		for (; y <= 3 + 20; y++)
			printf("\33[%uH|                    |", y);
		printf("\33[%uH\\--------------------/", y);
		printf("\33[H\n"); // refresh screen
	}

	{
		int n = sizeof(sys->disp_buf);
		char *d = sys->disp_buf, *e = d + n;
		char buf[16];
		const disp_item_t *item = disp_item;

		for (; item->str; item++) {
			int len1, len2 = strlen(item->str), len3;
			int off = item->off - DISP_CHECK_START;
			sys->disp_mask[off] |= 1 << item->bit;
			sys->disp_pos[off][item->bit] = d + 2 - sys->disp_buf;
			snprintf(buf, sizeof(buf), "\33[%u;%uH", item->row, item->col);
			len1 = strlen(buf);
			len2 = strlen(item->str);
			len3 = item->empty; if (len3 < 0) len3 = len2;
			if (e - d < 2 + len1 * 2 + len2 + len3)
				ERR_EXIT("disp_buf overflow");
			*d++ = len1 + len2;
			*d++ = len1 + len3;
			memcpy(d, buf, len1); d += len1;
			memcpy(d, item->str, len2); d += len2;
			memcpy(d, buf, len1); d += len1;
			memset(d, ' ', len3); d += len3;
		}
	}
}

static void sys_close(sysctx_t *sys) {
	tcsetattr(0, TCSANOW, &sys->tcattr);
	printf("\33[m\33[2J\33[?25h\33[H"); // show cursor
#if USE_GAMEPAD
	if (sys->js_fd >= 0) close(sys->js_fd);
	if (sys->js_ax) free(sys->js_ax);
	if (sys->js_btn) free(sys->js_btn);
#endif
}

static void sys_redraw(sysctx_t *sys, uint8_t *mem) {
	int i, j;
	for (i = 0; i < DISP_CHECK_SIZE; i++) {
		int val = mem[i + DISP_CHECK_START];
		int diff = sys->old_mem[i] ^ val;
		if (diff) {
			sys->old_mem[i] = val;
			if (sys->disp_mask[i] & diff)
			for (j = 0; j < 4; j++)
			if (diff & 1 << j) {
				char *src = sys->disp_buf + sys->disp_pos[i][j];
				int n;
				if (val & 1 << j) n = src[-2];
				else n = src[-1], src += src[-2];
				fwrite(src, 1, n, stdout);
			}
		}
	}

	for (i = 0; i < 20; i++) {
		int a, x;
		a = mem[217 + i * 2] << 4 | mem[216 + i * 2];
		if ((unsigned)(i - 4) < 8) {
			x = mem[196 + i * 2 - 8] & 3;
			x = (x << 1 | x >> 1) & 3;
		} else {
			const uint8_t tab[20 * 2] = {
				192,3, 192,0, 192,2, 192,1,
				0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
				212,2, 212,0, 212,1, 212,3,
				213,0, 213,1, 213,3, 213,2
			};
			int off = tab[i * 2], bit = tab[i * 2 + 1];
			x = mem[off + 2] >> bit & 1;
			x = x << 1 | (mem[off] >> bit & 1);
		}
		a = a << 2 | x;
		if (sys->old_rows[i] != a) {
			char buf[20], *d = buf;
			sys->old_rows[i] = a;
			for (j = 0; j < 10; j++, a <<= 1, d += 2) {
				if (a & 0x200) d[0] = '[', d[1] = ']';
				else d[0] = ' ', d[1] = ' ';
			}
			printf("\33[%u;2H%.20s", i + 4, buf);
		}
	}

	// update next
	{
		int a = mem[184] | mem[186] << 4 | mem[188] << 8 | mem[190] << 12;
		int diff = a ^ sys->old_next;
		if (diff) {
			sys->old_next = a;
			for (i = 0; i < 4; i++) {
				char buf[8], *d;
				int x, sh = 0x1203 >> (i * 4) & 3;
				if (!(diff >> sh & 0x1111)) continue;
				x = a >> sh; d = buf;
				for (j = 0; j < 4; j++, x <<= 4, d += 2) {
					if (x & 0x1000) d[0] = '[', d[1] = ']';
					else d[0] = ' ', d[1] = ' ';
				}
				printf("\33[%u;24H%.8s", i + 6, buf);
			}
		}
	}
	{
		int a;
		static const uint16_t digit1[] = {
			0x8c8c, 0x0880, 0x84c8, 0x88c8, 0x08c4,
			0x884c, 0x8c4c, 0x0888, 0x8ccc, 0x88cc };
		// update speed
		a = mem[196] | mem[198] << 4 | mem[200] << 8 | mem[202] << 12;
		a &= 0x8ccc;
		if (a != sys->old_speed) {
			sys->old_speed = a;
			for (j = 0; j < 10; j++) if (a == digit1[j]) break;
			printf("\33[11;31H%c", j < 10 ? j + '0' : a ? '?' : ' ');
		}
		// update level
		a = mem[204] | mem[206] << 4 | mem[208] << 8 | mem[210] << 12;
		a &= 0x8ccc;
		if (a != sys->old_level) {
			sys->old_level = a;
			for (j = 0; j < 10; j++) if (a == digit1[j]) break;
			printf("\33[13;31H%c", j < 10 ? j + '0' : a ? '?' : ' ');
		}
	}
	// update score
	{
		char buf[4]; uint32_t a;
		static const uint8_t digit4[] = {
			0xe7, 0xa0, 0xcb, 0xe9, 0xac, 0x6d, 0x6f, 0xe0, 0xef, 0xed };
		a  = (mem[179] | mem[199] << 4) << 24;
		a |= (mem[185] | mem[201] << 4) << 16;
		a |= (mem[189] | mem[187] << 4) << 8;
		a |=  mem[191] | mem[203] << 4;
		a &= 0xefefefef;
		if (a != sys->old_score) {
			sys->old_score = a;
			for (i = 0; i < 4; i++, a >>= 8) {
				int x = a & 0xff;
				for (j = 0; j < 10; j++) if (x == digit4[j]) break;
				buf[i] = j < 10 ? j + '0' : x ? '?' : ' ';
			}
			printf("\33[1;26H%.4s", buf);
		}
	}
	// update memory map
	{
		int i, j;
		int row = 3;
		if (sys_keys(sys) >> 17 & 1) {
			char buf[32];
			if (!(sys->misc & 1)) {
				printf("\33[%u;40H    0 1 2 3 4 5 6 7 8 9 a b c d e f", row);
				printf("\33[%u;40H  /--------------------------------", row + 1);
				for (i = 0; i < 16; i++)
					printf("\33[%u;40H%x |", i + row + 2, i);
				sys->misc |= 1;
#if NO_FLICKER
				memset(sys->memcopy, 0, sizeof(sys->memcopy));
#endif
			}
			for (i = 0; i < 16; i++) {
				for (j = 0; j < 16; j++) {
					int a = mem[i << 4 | j];
#if NO_FLICKER
					int b = sys->memcopy[i << 4 | j];
					int thr = NO_FLICKER * 16;
					if ((a ^ b) & 15) b = a;
					if (b < thr) b += 0x10, a = 0x10;
					sys->memcopy[i << 4 | j] = b;
#endif
					buf[j * 2] = a > 15 ? '#' : a < 10 ? a + '0' : a - 10 + 'a';
					buf[j * 2 + 1] = ' ';
				}
				printf("\33[%u;44H%.31s", i + row + 2, buf);
			}
		} else if (sys->misc & 1) {
			sys->misc &= ~1;
			for (i = 0; i < 18; i++)
				// "\33[K" - clear right
				printf("\33[%u;40H\33[K", i + row);
		}
	}
	printf("\33[H\n"); // refresh screen
}

typedef struct {
	uint8_t mem[256]; uint16_t pc, stack;
	uint8_t a, r[5], cf, tmr, tf, timer_en;
} cpu_state_t;

static int check_state(cpu_state_t *s) {
	unsigned i, x = 0;
	for (i = 0; i < 256; i++) x |= s->mem[i], s->mem[i] &= 15;
	x |= s->pc >> 8; s->pc &= 0xfff;
	x |= s->stack >> 9; s->stack &= 0x1fff;
	x |= s->a; s->a &= 15;
	for (i = 0; i < 5; i++) x |= s->r[i], s->r[i] &= 15;
	x |= (s->cf | s->tf | s->timer_en) << 3;
	s->cf &= 1; s->tf &= 1; s->timer_en &= 1;
	return x >> 4;
}

#ifndef DECOMPILED
static void run_game(uint8_t *rom, sysctx_t *sys, cpu_state_t *s) {
	unsigned pc = s->pc;
	unsigned a = s->a, cf = s->cf;
	uint8_t pa = 0, pm = 0xf, ps = 0xf, pp = 0xf;
	uint32_t tickcount = 0, prev_tick = 0, tmr_frac = 0;
	uint64_t last_time;

#define CPU_TRACE 0

	last_time = get_time_usec();

	for (;;) {
		unsigned x, op;
		op = rom[pc];
#define R1R0 s->r[1] << 4 | s->r[0]
#define R3R2 s->r[3] << 4 | s->r[2]

#if CPU_TRACE
#define TRACE(...) fprintf(stderr, "  " __VA_ARGS__)
		fprintf(stderr, "%03x: o=%02x,r=%x:%02x:%02x:%x,c%u",
				pc, op, a, R1R0, R3R2, s->r[4], cf);
#else
#define TRACE(...) (void)0
#endif

	switch (op) {

	case 0x00: /* RR A */
		cf = a & 1; a = (a << 4 | a) >> 1 & 15; TRACE("a=%x,c=%u", a, cf); break;
	case 0x01: /* RL A */
		cf = a >> 3; a = (a << 4 | a) >> 3 & 15; TRACE("a=%x,c=%u", a, cf); break;
	case 0x02: /* RRC A */
		a = cf << 4 | a; cf = a & 1; a >>= 1; TRACE("a=%x,c=%u", a, cf); break;
	case 0x03: /* RLC A */
		a = a << 1 | cf; cf = a >> 4; a &= 15; TRACE("a=%x,c=%u", a, cf); break;

	case 0x04: // MOV A, [R1R0]
	case 0x06: // MOV A, [R3R2]
		x = op & 2; x = s->r[x + 1] << 4 | s->r[x]; a = s->mem[x]; TRACE("a=%x", a); break;
	case 0x05: // MOV [R1R0], A
	case 0x07: // MOV [R3R2], A
		x = op & 2; x = s->r[x + 1] << 4 | s->r[x]; s->mem[x] = a; TRACE("m[%02x]=%x", x, a); break;

	case 0x08: /* ADC A, [R1R0] */
	case 0x09: /* ADD A, [R1R0] */
		cf &= ~op;
		a += s->mem[R1R0] + cf; cf = a >> 4; a &= 15;
		TRACE("a=%x", a);
		break;

	case 0x0a: /* SBC A, [R1R0] */
	case 0x0b: /* SUB A, [R1R0] */
		cf |= op & 1;
		a += 15 - s->mem[R1R0] + cf; cf = a >> 4; a &= 15;
		TRACE("a=%x", a);
		break;

	case 0x0c: // INC [R1R0]
	case 0x0d: // DEC [R1R0]
	case 0x0e: // INC [R3R2]
	case 0x0f: // DEC [R3R2]
		x = op & 2; x = s->r[x + 1] << 4 | s->r[x];
		s->mem[x] = (s->mem[x] + (op & 1 ? -1 : 1)) & 15;
		TRACE("m[%02x]=%x", x, s->mem[x]);
		break;

	case 0x10: case 0x12: // INC Rn
	case 0x14: case 0x16: case 0x18:
		x = op >> 1 & 7; s->r[x] = (s->r[x] + 1) & 15; TRACE("r%u=%x", x, s->r[x]); break;

	case 0x11: case 0x13: // DEC Rn
	case 0x15: case 0x17: case 0x19:
		x = op >> 1 & 7; s->r[x] = (s->r[x] - 1) & 15; TRACE("r%u=%x", x, s->r[x]); break;

	case 0x1a: /* AND A, [R1R0] */ a &= s->mem[R1R0]; TRACE("a=%x", a); break;
	case 0x1b: /* XOR A, [R1R0] */ a ^= s->mem[R1R0]; TRACE("a=%x", a); break;
	case 0x1c: /* OR A, [R1R0] */ a |= s->mem[R1R0]; TRACE("a=%x", a); break;
	case 0x1d: /* AND [R1R0], A */ s->mem[R1R0] &= a; TRACE("m[%02x]=%x", R1R0, s->mem[R1R0]); break;
	case 0x1e: /* XOR [R1R0], A */ s->mem[R1R0] ^= a; TRACE("m[%02x]=%x", R1R0, s->mem[R1R0]); break;
	case 0x1f: /* OR [R1R0], A */ s->mem[R1R0] |= a; TRACE("m[%02x]=%x", R1R0, s->mem[R1R0]); break;

	case 0x20: case 0x22: // MOV Rn, A
	case 0x24: case 0x26: case 0x28:
		s->r[op >> 1 & 7] = a; TRACE("r%u=%x", op >> 1 & 7, a); break;

	case 0x21: case 0x23: // MOV A, Rn
	case 0x25: case 0x27: case 0x29:
		a = s->r[op >> 1 & 7]; TRACE("a=%x", a); break;

	case 0x2a: /* CLC */ cf = 0; TRACE("c=%x", cf); break;
	case 0x2b: /* STC */ cf = 1; TRACE("c=%x", cf); break;
	case 0x2c: /* EI */ /* TODO */; TRACE("i=%x", 1); break;
	case 0x2d: /* DI */ /* TODO */; TRACE("i=%x", 0); break;
	case 0x2e: /* RET */
		pc = s->stack; TRACE("pc=%03x", pc); pc--; break;
	case 0x2f: /* RETI */
		pc = s->stack; cf = pc >> 12; TRACE("pc=%03x,c=%u", pc, cf); pc--; break;

	case 0x30: /* OUT PA, A */ pa = a; TRACE("pa=%x", pa); break;
	case 0x31: /* INC A */ a = (a + 1) & 15; TRACE("a=%x", a); break;
	case 0x32: /* IN A, PM */ a = pm; TRACE("a=%x", a); break;
	case 0x33: /* IN A, PS */ a = ps; TRACE("a=%x", a); break;
	case 0x34: /* IN A, PP */ a = pp; TRACE("a=%x", a); break;
	case 0x35: /* unknown */ break;
	case 0x36: /* DAA */
		if (a >= 10 || cf) a = (a + 6) & 15, cf = 1, TRACE("a=%x,c=%u", a, cf);
		break;
	case 0x37: /* HALT */
		TRACE("halt"); break;
	case 0x38: /* TIMER ON */
		s->timer_en = 1; TRACE("timer on"); break;
	case 0x39: /* TIMER OFF */
		s->timer_en = 0; TRACE("timer off"); break;
	case 0x3a: /* MOV A, TMRL */
		a = s->tmr & 15; TRACE("a=%x", a); break;
	case 0x3b: /* MOV A, TMRH */
		a = s->tmr >> 4; TRACE("a=%x", a); break;
	case 0x3c: /* MOV TMRL, A */
		s->tmr = (s->tmr & 0xf0) | a; TRACE("tmrl=%x", a); break;
	case 0x3d: /* MOV TMRH, A */
		s->tmr = a << 4 | (s->tmr & 15); TRACE("tmrh=%x", a); break;
	case 0x3e: /* NOP */
		TRACE("nop"); break;
	case 0x3f: /* DEC A */ a = (a - 1) & 15; TRACE("a=%x", a); break;

	case 0x40: // ADD A, imm4
		a += rom[++pc & 0xfff] & 15;
		cf = a >> 4; a &= 15; TRACE("a=%x", a); break;
	case 0x41: // SUB A, imm4
		a += 16 - (rom[++pc & 0xfff] & 15);
		cf = a >> 4; a &= 15; TRACE("a=%x", a); break;
	case 0x42: // AND A, imm4
		a &= rom[++pc & 0xfff]; TRACE("a=%x", a); break;
	case 0x43: // XOR A, imm4
		a ^= rom[++pc & 0xfff] & 15; TRACE("a=%x", a); break;
	case 0x44: // OR A, imm4
		a |= rom[++pc & 0xfff] & 15; TRACE("a=%x", a); break;
	case 0x45: // SOUND imm4
		x = rom[++pc & 0xfff] & 15; TRACE("sound %x", x);
		(void)x; /* TODO */ break;
	case 0x46: // MOV R4, imm4
		s->r[4] = rom[++pc & 0xfff] & 15; TRACE("r4=%x", s->r[4]); break;
	case 0x47: // TIMER imm8
		s->tmr = rom[++pc & 0xfff]; TRACE("tmr=%02x", s->tmr); break;
	case 0x48: /* SOUND ONE */ TRACE("sound one"); /* TODO */ break;
	case 0x49: /* SOUND LOOP */ TRACE("sound loop"); /* TODO */ break;
	case 0x4a: /* SOUND OFF */ TRACE("sound off"); /* TODO */ break;
	case 0x4b: /* SOUND A */ TRACE("sound a"); /* TODO */ break;
	case 0x4c: /* READ R4A */
		a = rom[(pc & 0xf00) | a << 4 | s->mem[R1R0]];
		TRACE("r4:a=%02x", a);
		s->r[4] = a >> 4; a &= 15; break;
	case 0x4d: /* READF R4A */
		a = rom[0xf00 | a << 4 | s->mem[R1R0]];
		TRACE("r4:a=%02x", a);
		s->r[4] = a >> 4; a &= 15; break;
	case 0x4e: /* READ MR0A */
		a = rom[(pc & 0xf00) | a << 4 | s->r[4]];
		TRACE("m[%02x]:a=%02x", R1R0, a);
		s->mem[R1R0] = a >> 4; a &= 15; break;
	case 0x4f: /* READF MR0A */
		a = rom[0xf00 | a << 4 | s->r[4]];
		TRACE("m[%02x]:a=%02x", R1R0, a);
		s->mem[R1R0] = a >> 4; a &= 15; break;

#define CASE8(x) \
	case x:     case x + 1: case x + 2: case x + 3: \
	case x + 4: case x + 5: case x + 6: case x + 7:

	CASE8(0x50) CASE8(0x58) // MOV R1R0, imm8
		s->r[0] = op & 0xf; s->r[1] = rom[++pc & 0xfff] & 15; TRACE("r1r0=%02x", R1R0); break;
	CASE8(0x60) CASE8(0x68) // MOV R3R2, imm8
		s->r[2] = op & 0xf; s->r[3] = rom[++pc & 0xfff] & 15; TRACE("r3r2=%02x", R3R2); break;

	CASE8(0x70) CASE8(0x78) /* MOV A, imm4 */ a = op & 15; TRACE("a=%x", a); break;

#define JMP11 \
	x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff]; pc++;
#define TRACE_JUMP TRACE("pc=%03x", pc + 1); else TRACE("no jump")
#define X(cond) JMP11 if (cond) pc = x - 1, TRACE_JUMP; break;
	CASE8(0x80) CASE8(0x88) // JAn imm11
	CASE8(0x90) CASE8(0x98) X(a >> (op >> 3 & 3) & 1)
	CASE8(0xa0) /* JNZ R0, imm11 */ X(s->r[0])
	CASE8(0xa8) /* JNZ R1, imm11 */ X(s->r[1])
	CASE8(0xb0) /* JZ A, imm11 */ X(!a)
	CASE8(0xb8) /* JNZ A, imm11 */ X(a)
	CASE8(0xc0) /* JC imm11 */ X(cf)
	CASE8(0xc8) /* JNC imm11 */ X(!cf)
	CASE8(0xd0) /* JTMR imm11 */ JMP11 if (s->tf) pc = x - 1, TRACE_JUMP; s->tf = 0; break;
	CASE8(0xd8) /* JNZ R4, imm11 */ X(s->r[4])
#undef X
#undef JMP11

	CASE8(0xe0) CASE8(0xe8) // JMP imm12
		pc = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
		TRACE("pc=%03x", pc);
		pc--; break;
	CASE8(0xf0) CASE8(0xf8) // CALL imm12
		s->stack = (pc + 2) & 0xfff;
		pc = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
		TRACE("pc=%03x,ret=%03x", pc, s->stack);
		pc--; break;

	default:
		ERR_EXIT("unknown opcode\n");
	} // end switch

#if CPU_TRACE
		fprintf(stderr, "\n");
#endif
		pc = (pc + 1) & 0xfff;
		tickcount++;

#if CPU_TRACE
		if (tickcount > 2150) {
			// display_redraw(sys, s->mem);
			break;
		}
#endif

		// 1ms
		if (tickcount - prev_tick >= sys->sleep_ticks) {
			uint64_t new_time, delay;
			uint32_t keys, sleep_delay;
			prev_tick = tickcount;
			sys_redraw(sys, s->mem);
			new_time = get_time_usec();
			delay = new_time - last_time;
			sleep_delay = sys->sleep_delay;
			if (delay > sleep_delay) {
				last_time = new_time;
			} else {
				last_time += sleep_delay;
				usleep(sleep_delay - delay);
			}
			keys = ~sys_events(sys);
			if (!(keys & 0x10000)) break;
			pp = keys & 15;
			ps = keys >> 4 & 15;
		}

		if (s->timer_en) {
			tmr_frac += sys->timer_inc;
			if (tmr_frac >= 0x10000) {
				tmr_frac -= 0x10000;
				if (!++s->tmr) s->tf = 1;
			}
		}

		(void)pa;

	} // end for

	s->pc = pc; s->a = a; s->cf = cf;
}
#else
void run_decomp(sysctx_t *user, cpu_state_t *cpu);
#endif

static void test_keys() {
	char x;
	while ((x | 32) != 'q')
		if (read(0, &x, 1) == 1)
			printf("0x%02x %u\n", x, x);
}

#if USE_GAMEPAD
static void test_gamepad(int js_fd) {
	struct js_event event;
	if (js_fd >= 0)
	for (;;) {
		int n = read(js_fd, &event, sizeof(event));
		if (n != sizeof(event))
			ERR_EXIT("unexpected joystic event\n");
		printf("0x%08x 0x%04x 0x%02x 0x%02x\n",
				event.time, event.value & 0xffff, event.type, event.number);
	}
}
#endif

int main(int argc, char **argv) {
	sysctx_t ctx;
	const char *save_fn = NULL;
	FILE *f; unsigned n;
#if USE_GAMEPAD
	const char* js_fn = "/dev/input/js0";
#endif
#ifndef DECOMPILED
	const char *rom_fn = "brickrom.bin";
	uint8_t rom[0x1000];
#endif
	uint32_t hold_time = 50;
	uint32_t sleep_ticks = 1000, sleep_delay = 1000;
	uint32_t timer_inc = 32;
	const char *progname = argv[0];
	cpu_state_t cpu;

	while (argc > 1) {
		if (!strcmp(argv[1], "--save")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			save_fn = argv[2];
			if (!*save_fn) save_fn = NULL;
			argc -= 2; argv += 2;
#ifndef DECOMPILED
		} else if (!strcmp(argv[1], "--rom")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			rom_fn = argv[2];
			argc -= 2; argv += 2;
#endif
#if USE_GAMEPAD
		} else if (!strcmp(argv[1], "--js")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			js_fn = argv[2];
			if (!*js_fn) js_fn = NULL;
			argc -= 2; argv += 2;
#endif
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			printf(
"Usage: %s [options]\n"
"Options:\n"
"  -h, --help        Display help text and exit\n"
#ifndef DECOMPILED
"  --rom file        To specify the ROM file name\n"
"                      (default is \"%s\")\n"
#endif
#if USE_GAMEPAD
"  --js device       To specify gamepad device\n"
"                      (default is \"%s\")\n"
#endif
"  --save file       To specify the file for cpu state\n"
"  -k n              Holds a key for N ms after pressing (default is %d)\n"
"  -t n              Stops at every N tick to redraw, sleep and check keys\n"
"                      (default is %d)\n"
"  -d n              Max sleep time in microseconds (default is %d)\n"
"  -i n              Increment timer every N ticks (default is %d)\n"
"\n", progname,
#ifndef DECOMPILED
		rom_fn,
#endif
#if USE_GAMEPAD
		js_fn,
#endif
		hold_time,
		sleep_ticks,
		sleep_delay, timer_inc);
			return 1;
		} else if (!strcmp(argv[1], "-k")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			hold_time = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-t")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			sleep_ticks = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-d")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			sleep_delay = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-i")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			timer_inc = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else ERR_EXIT("unknown option\n");
	}

	timer_inc = timer_inc ? 0x10000 / timer_inc : 0x10000;
	if (timer_inc > 0x10000) timer_inc = 0x10000;

#ifndef DECOMPILED
	f = fopen(rom_fn, "rb");
	if (!f) ERR_EXIT("fopen failed\n");
	n = fread(rom, 1, sizeof(rom), f);
	fclose(f);
	if (n != sizeof(rom)) ERR_EXIT("unexpected ROM size\n");
#endif

	memset(&cpu, 0, sizeof(cpu));
	if (save_fn) {
		f = fopen(save_fn, "rb");
		if (f) {
			n = fread(&cpu, 1, sizeof(cpu), f);
			fclose(f);
			if (n != sizeof(cpu)) ERR_EXIT("unexpected save size\n");
		}
		if (check_state(&cpu)) ERR_EXIT("save state is corrupted\n");
	}

	sys_init(&ctx);
	ctx.hold_time = hold_time;
	ctx.sleep_ticks = sleep_ticks;
	ctx.sleep_delay = sleep_delay;
	ctx.timer_inc = timer_inc;

#if USE_GAMEPAD
	ctx.js_fd = -1;
	if (js_fn) {
		ctx.js_fd = open(js_fn, O_RDONLY);
		if (ctx.js_fd >= 0) sys_gamepad_init(&ctx);
		//test_gamepad(ctx.js_fd);
	}
#endif

	//test_keys();
#ifndef DECOMPILED
	run_game(rom, &ctx, &cpu);
#else
	ctx.timer_inc = timer_inc * sleep_ticks >> 8;
	ctx.last_time = get_time_usec();	
	ctx.randseed = ctx.last_time;
	run_decomp(&ctx, &cpu);
#endif

	if (save_fn) {
		f = fopen(save_fn, "wb");
		if (f) {
			n = fwrite(&cpu, 1, sizeof(cpu), f);
			fclose(f);
		}
	}

	sys_close(&ctx);
}

#ifdef DECOMPILED

static void timer_handler(sysctx_t *sys, cpu_state_t *cpu) {
	uint64_t new_time = get_time_usec();
	uint64_t last_time = sys->last_time;
	uint64_t diff = new_time - last_time;
	unsigned sleep_delay = sys->sleep_delay;
	if (diff >= sleep_delay) {
		sys->last_time = last_time + sleep_delay;
		sys->tmr_frac += sys->timer_inc;
		sys_redraw(sys, cpu->mem);
		sys_events(sys);
	}
}

static int cb_in_ps(sysctx_t *sys, cpu_state_t *cpu) {
	timer_handler(sys, cpu); return (~sys_keys(sys) >> 4) & 0xf; }
static int cb_in_pp(sysctx_t *sys, cpu_state_t *cpu) {
	timer_handler(sys, cpu); return ~sys_keys(sys) & 0xf; }

static int cb_get_tf(sysctx_t *sys, cpu_state_t *cpu) {
	int tmr_frac;
	usleep(sys->sleep_delay / 4);
	timer_handler(sys, cpu);
	tmr_frac = sys->tmr_frac;
	if (tmr_frac < 0x10000) return 0;
	sys->tmr_frac = tmr_frac - 0x10000;
	return 1;
}

static int cb_get_tmr(sysctx_t *sys) {
	return (sys->randseed = sys->randseed * 0x08088405 + 1) >> 24;
}

#define RET_OFFSET(x) x,
#define RET_LABEL(x) &&r_##x,
#define RET_OFFSET2(l, o) o,
#define RET_LABEL2(l, o) &&l,
#define START \
	unsigned pc; \
	static uint16_t const ret_offsets[] = { \
		RET_ENUM(RET_OFFSET) JTMR_ENUM(RET_OFFSET2) 0 }; \
	static void* const ret_labels[] = { \
		RET_ENUM(RET_LABEL) JTMR_ENUM(RET_LABEL2) &&l_start }; \
	do { \
		unsigned i, n = sizeof(ret_offsets) / sizeof(uint16_t); \
		pc = cpu->stack; \
		for (i = 0; i < n; i++) \
			if (pc == ret_offsets[i]) { stack = ret_labels[i]; break; } \
		if (i == n) break; \
		pc = cpu->pc; \
		for (i = 0; i < n; i++) \
			if (pc == ret_offsets[i]) goto *ret_labels[i]; \
	} while (0); \
	ERR_EXIT("unable to continue with this save state\n"); \
l_exit: \
	cpu->pc = pc; \
	cpu->a = a; cpu->r[4] = r4; cpu->cf = cf; \
	cpu->r[0] = r1r0 & 15; cpu->r[1] = r1r0 >> 4; \
	cpu->r[2] = r3r2 & 15; cpu->r[3] = r3r2 >> 4; \
	{ \
		unsigned i, n = sizeof(ret_offsets) / sizeof(uint16_t); \
		for (i = 0; i < n; i++) \
			if (stack == ret_labels[i]) { cpu->stack = ret_offsets[i]; break; } \
		if (i == n) ERR_EXIT("can't find return address in the list\n"); \
	} \
	return; \
l_start:

#define TIMER_ON
#define GET_TMR cb_get_tmr(sys)
#define OUT_PA
#define IN_PS a = cb_in_ps(sys, cpu);
#define IN_PP a = cb_in_pp(sys, cpu);
#define SOUND(x)
#define SOUND_OFF
#define HALT
#define JTMR(label, offset) if (cb_get_tf(sys, cpu)) { \
	if (sys_keys(sys) & 0x10000) { pc = offset; goto l_exit; } \
	goto label; \
}

void run_decomp(sysctx_t *sys, cpu_state_t *cpu) {
	uint8_t *m = cpu->mem;
	int a = cpu->a, r4 = cpu->r[4], cf = cpu->cf;
	unsigned r1r0 = cpu->r[1] << 4 | cpu->r[0];
	unsigned r3r2 = cpu->r[3] << 4 | cpu->r[2];
	void *stack;

#define RR cf = a & 1, a = (a << 4 | a) >> 1 & 15;
#define RL cf = a >> 3, a = (a << 4 | a) >> 3 & 15;
#define RRC a = cf << 4 | a, cf = a & 1, a >>= 1;
#define RLC a = a << 1 | cf, cf = a >> 4, a &= 15;
#define DAA if (a >= 10 || cf) a = (a + 6) & 15, cf = 1;

#define ADD(a, b) a = (cf = a + b) & 15, cf >>= 4;
#define ADC(a, b) a = (cf = a + b + cf) & 15, cf >>= 4;
#define SUB(a, b) a = (cf = a + 16 - b) & 15, cf >>= 4;
#define SBC(a, b) a = (cf = a + 15 - b + cf) & 15, cf >>= 4;

#define INC(a) a = (a + 1) & 15;
#define DEC(a) a = (a - 1) & 15;
#define INC_RE(r, op) r = (r & 0xf0) | ((r op 1) & 15);
#define INC_RO(r, op) r = (r op 16) & 0xff;
#define INC_R0 INC_RE(r1r0, +)
#define INC_R1 INC_RO(r1r0, +)
#define INC_R2 INC_RE(r3r2, +)
#define INC_R3 INC_RO(r3r2, +)
#define DEC_R0 INC_RE(r1r0, -)
#define DEC_R1 INC_RO(r1r0, -)
#define DEC_R2 INC_RE(r3r2, -)
#define DEC_R3 INC_RO(r3r2, -)
#define CALL(fn, ret) stack = &&r_##ret; goto fn; r_##ret:;
#define RET goto *stack;

#include "brickgame_dec.c"
}
#endif // DECOMPILED

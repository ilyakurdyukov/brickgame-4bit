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

typedef struct {
	struct termios tcattr;
	unsigned hold_time, sleep_ticks, sleep_delay, timer_inc;
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
			if (status == 1) return 1 << 16; // escape = exit
			break;
		}
	}
#undef SET_KEY
	return sys->keys;
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
		sys->keys = 0;
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
		int i, n = sizeof(sys->disp_buf);
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

static void run_game(uint8_t *rom, sysctx_t *sys, cpu_state_t *s) {
	uint16_t pc = s->pc;
	uint8_t a = s->a, cf = s->cf;
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

	// MOV Rn, A
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

static void test_keys() {
	char x;
	while ((x | 32) != 'q')
		if (read(0, &x, 1) == 1)
			printf("0x%02x %u\n", x, x);
}

int main(int argc, char **argv) {
	sysctx_t ctx;
	const char *rom_fn = "brickrom.bin";
	const char *save_fn = NULL;
	uint8_t rom[0x1000];
	FILE *f; unsigned n;
	uint32_t hold_time = 50;
	uint32_t sleep_ticks = 1000, sleep_delay = 1000;
	uint32_t timer_inc = 0x10000 >> 5;
	const char *progname = argv[0];
	cpu_state_t cpu = { 0 };

	while (argc > 1) {
		if (!strcmp(argv[1], "--rom")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			rom_fn = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--save")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			save_fn = argv[2];
			if (!*save_fn) save_fn = NULL;
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			printf(
"Usage: %s [options]\n"
"Options:\n"
"  -h, --help        Display help text and exit\n"
"  --rom file        To specify the ROM file name (default is \"brickrom.bin\")\n"
"  --save file       To specify the file for cpu state\n"
"  -k n              Holds a key for N ms after pressing (default is 50)\n"
"  -t n              Stops at every N tick to redraw, sleep and check keys\n"
"                      (default is 1000)\n"
"  -d n              Max sleep time in microseconds (default is 1000)\n"
"  -i n              Increment timer every N ticks (default is 32)\n"
"\n", progname);
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
			timer_inc = timer_inc ? 0x10000 / timer_inc : 0x10000;
			if (timer_inc > 0x10000) timer_inc = 0x10000;
			argc -= 2; argv += 2;
		} else ERR_EXIT("unknown option\n");
	}

	f = fopen(rom_fn, "rb");
	if (!f) ERR_EXIT("fopen failed\n");
	n = fread(rom, 1, sizeof(rom), f);
	fclose(f);
	if (n != sizeof(rom)) ERR_EXIT("unexpected ROM size\n");

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

	//test_keys();
	run_game(rom, &ctx, &cpu);

	if (save_fn) {
		f = fopen(save_fn, "wb");
		if (f) {
			n = fwrite(&cpu, 1, sizeof(cpu), f);
			fclose(f);
		}
	}

	sys_close(&ctx);
}


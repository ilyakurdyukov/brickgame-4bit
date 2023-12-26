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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define CASE8(x) \
	case x:     case x + 1: case x + 2: case x + 3: \
	case x + 4: case x + 5: case x + 6: case x + 7:

static unsigned mark_opcodes(uint8_t *rom, unsigned pc, uint8_t *marks) {
	unsigned read_mask = 0;

	for (;;) {
		unsigned x, op;

		pc &= 0xfff;
		x = marks[pc];
		if (x & 1) break;
		marks[pc] = x | 1;

		op = rom[pc];
		switch (op) {
		case 0x2e: /* RET */
		case 0x2f: /* RETI */
			return read_mask;

		case 0x40: // ADD A, imm4
		case 0x41: // SUB A, imm4
		case 0x42: // AND A, imm4
		case 0x43: // XOR A, imm4
		case 0x44: // OR A, imm4
		case 0x45: // SOUND imm4
		case 0x46: // MOV R4, imm4
		case 0x47: // TIMER imm8
		CASE8(0x50) CASE8(0x58) // MOV R1R0, imm8
		CASE8(0x60) CASE8(0x68) // MOV R3R2, imm8
			marks[++pc] |= 2; break;

		case 0x4c: /* READ R4A */
		case 0x4d: /* READF R4A */
		case 0x4e: /* READ MR0A */
		case 0x4f: /* READF MR0A */
			read_mask |= 1 << (op & 1 ? 0xf : pc >> 8); break;

		CASE8(0x80) CASE8(0x88) // JAn imm11
		CASE8(0x90) CASE8(0x98)
		CASE8(0xa0) /* JNZ R0, imm11 */
		CASE8(0xa8) /* JNZ R1, imm11 */
		CASE8(0xb0) /* JZ A, imm11 */
		CASE8(0xb8) /* JNZ A, imm11 */
		CASE8(0xc0) /* JC imm11 */
		CASE8(0xc8) /* JNC imm11 */
		CASE8(0xd0) /* JTMR imm11 */
		CASE8(0xd8) /* JNZ R4, imm11 */
			x = (pc & 0x800) | (op & 7) << 8 | rom[(pc + 1) & 0xfff];
			marks[++pc & 0xfff] |= 2; marks[x] |= 4;
			if ((op & 0xf8) == 0xd0) marks[x & 0xfff] |= 32;
			read_mask |= mark_opcodes(rom, x, marks); break;

		CASE8(0xe0) CASE8(0xe8) // JMP imm12
			x = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
			marks[++pc & 0xfff] |= 2;
			marks[x] |= 4;
			pc = x - 1; break;
		CASE8(0xf0) CASE8(0xf8) // CALL imm12
			x = (op & 15) << 8 | rom[(pc + 1) & 0xfff];
			marks[++pc & 0xfff] |= 2; marks[x] |= 8;
			marks[(pc + 1) & 0xfff] |= 16; // ret
			read_mask |= mark_opcodes(rom, x, marks); break;
		}
		pc++;
	}
	return read_mask;
}

static void decompile(uint8_t *rom, uint8_t *marks, unsigned read_mask, FILE *fo) {
	unsigned pc;

#define OUT(...) fprintf(fo, "\t" __VA_ARGS__)

	{
		int i, j;
		for (i = 0; i < 16; i++) if (read_mask >> i & 1) {
			OUT("static const uint8_t rom_%x[256] = {\n\t\t", i);
			for (j = 0; j < 0x100; j++)
				fprintf(fo, "0x%02x%s", rom[i << 8 | j],
					j == 255 ? "\n\t};\n" : (j & 15) == 15 ? ",\n\t\t" : ",");
		}

		fprintf(fo, "#define RET_ENUM(X) \\\n");
		for (i = 0, pc = 0; pc < 0x1000; pc++) if (marks[pc] & 16) {
			if (i >= 5) i = 0, fprintf(fo, " \\\n");
			fprintf(fo, "%sX(0x%03x)", !i ? "\t" : " ", pc);
			i++;
		}
		fprintf(fo, "\n\n");

		fprintf(fo, "#define JTMR_ENUM(X) \\\n");
		for (i = 0, pc = 0; pc < 0x1000; pc++) if (marks[pc] & 32) {
			if (i >= 3) i = 0, fprintf(fo, " \\\n");
			fprintf(fo, "%sX(l_%03x, 0x%03x)", !i ? "\t" : " ", pc, pc);
			i++;
		}
		fprintf(fo, "\n\n");

		OUT("START\n");
	}

	for (pc = 0; pc < 0x1000; pc++) {
		unsigned x, op;
		x = marks[pc];
		if (x & 2) continue;
		if (x & 4) fprintf(fo, "l_%03x:\n", pc);
		if (x & 8) fprintf(fo, "f_%03x:\n", pc);
		op = rom[pc];
		if (!(x & 1)) { OUT("// 0x%02x\n", op); continue; }

		switch (op) {
		case 0x00: /* RR A */ OUT("RR\n"); break;
		case 0x01: /* RL A */ OUT("RL\n"); break;
		case 0x02: /* RRC A */ OUT("RRC\n"); break;
		case 0x03: /* RLC A */ OUT("RLC\n"); break;

		case 0x04: // MOV A, [R1R0]
		case 0x06: // MOV A, [R3R2]
			OUT("a = m[%s];\n", op & 2 ? "r3r2" : "r1r0"); break;
		case 0x05: // MOV [R1R0], A
		case 0x07: // MOV [R3R2], A
			OUT("m[%s] = a;\n", op & 2 ? "r3r2" : "r1r0"); break;

		case 0x08: /* ADC A, [R1R0] */
			OUT("ADC(a, m[r1r0])\n"); break;
		case 0x09: /* ADD A, [R1R0] */
			OUT("ADD(a, m[r1r0])\n"); break;
		case 0x0a: /* SBC A, [R1R0] */
			OUT("SBC(a, m[r1r0])\n"); break;
		case 0x0b: /* SUB A, [R1R0] */
			OUT("SUB(a, m[r1r0])\n"); break;

		case 0x0c: // INC [R1R0]
		case 0x0d: // DEC [R1R0]
		case 0x0e: // INC [R3R2]
		case 0x0f: // DEC [R3R2]
			OUT("%s(m[%s])\n", op & 1 ? "DEC" : "INC", op & 2 ? "r3r2" : "r1r0"); break;

		case 0x10: case 0x12: // INC Rn
		case 0x14: case 0x16:
		case 0x11: case 0x13: // DEC Rn
		case 0x15: case 0x17: {
			OUT("%s_R%u\n", op & 1 ? "DEC" : "INC", (op >> 1) & 3);
			break;
		}
		case 0x18: OUT("INC(r4)\n"); break;
		case 0x19: OUT("DEC(r4)\n"); break;

		case 0x1a: /* AND A, [R1R0] */ OUT("a &= m[r1r0];\n"); break;
		case 0x1b: /* XOR A, [R1R0] */ OUT("a ^= m[r1r0];\n"); break;
		case 0x1c: /* OR A, [R1R0] */ OUT("a |= m[r1r0];\n"); break;
		case 0x1d: /* AND [R1R0], A */ OUT("m[r1r0] &= a;\n"); break;
		case 0x1e: /* XOR [R1R0], A */ OUT("m[r1r0] ^= a;\n"); break;
		case 0x1f: /* OR [R1R0], A */ OUT("m[r1r0] |= a;\n"); break;

		case 0x20: case 0x22: // MOV Rn, A
		case 0x24: case 0x26: {
			const char *reg = op & 4 ? "r3r2" : "r1r0";
			if (op & 2) OUT("%s = a << 4 | (%s & 15);\n", reg, reg);
			else OUT("%s = (%s & 0xf0) | a;\n", reg, reg);
			break;
		}
		case 0x21: case 0x23: // MOV A, Rn
		case 0x25: case 0x27: {
			const char *reg = op & 4 ? "r3r2" : "r1r0";
			if (op & 2) OUT("a = %s >> 4;\n", reg);
			else OUT("a = %s & 15;\n", reg);
			break;
		}
		case 0x28: OUT("r4 = a;\n"); break;
		case 0x29: OUT("a = r4;\n"); break;

		case 0x2a: /* CLC */ OUT("cf = 0;\n"); break;
		case 0x2b: /* STC */ OUT("cf = 1;\n"); break;
		case 0x2c: /* EI */ OUT("EI\n"); break;
		case 0x2d: /* DI */ OUT("DI\n"); break;
		case 0x2e: /* RET */ OUT("RET\n\n"); break;
		case 0x2f: /* RETI */ OUT("RETI\n\n"); break;

		case 0x30: /* OUT PA, A */ OUT("OUT_PA\n"); break;
		case 0x31: /* INC A */ OUT("INC(a)\n"); break;
		case 0x32: /* IN A, PM */ OUT("IN_PM\n"); break;
		case 0x33: /* IN A, PS */ OUT("IN_PS\n"); break;
		case 0x34: /* IN A, PP */ OUT("IN_PP\n"); break;
		case 0x35: /* unknown */ OUT("OP35\n"); break;
		case 0x36: /* DAA */ OUT("DAA\n"); break;
		case 0x37: /* HALT */ OUT("HALT\n"); break;
		case 0x38: /* TIMER ON */ OUT("TIMER_ON\n"); break;
		case 0x39: /* TIMER OFF */ OUT("TIMER_OFF\n"); break;
		case 0x3a: /* MOV A, TMRL */ OUT("a = GET_TMR & 15;\n"); break;
		case 0x3b: /* MOV A, TMRH */ OUT("a = GET_TMR >> 4;\n"); break;
		case 0x3c: /* MOV TMRL, A */ OUT("SET_TMRL(a);\n"); break;
		case 0x3d: /* MOV TMRH, A */ OUT("SET_TMRH(a);\n"); break;
		case 0x3e: /* NOP */ OUT("// NOP\n"); break;
		case 0x3f: /* DEC A */ OUT("DEC(a)\n"); break;

#define OP2 rom[(pc + 1) & 0xfff]

		case 0x40: /* ADD A, imm4 */ OUT("ADD(a, 0x%x)\n", OP2 & 15); break;
		case 0x41: /* SUB A, imm4 */ OUT("SUB(a, 0x%x)\n", OP2 & 15); break;
		case 0x42: /* AND A, imm4 */ OUT("a &= 0x%x;\n", OP2 & 15); break;
		case 0x43: /* XOR A, imm4 */ OUT("a ^= 0x%x;\n", OP2 & 15); break;
		case 0x44: /* OR A, imm4 */ OUT("a |= 0x%x;\n", OP2 & 15); break;
		case 0x45: /* SOUND imm4 */ OUT("SOUND(0x%x)\n", OP2 & 15); break;
		case 0x46: /* MOV R4, imm4 */ OUT("r4 = 0x%x;\n", OP2 & 15); break;
		case 0x47: /* TIMER imm8 */ OUT("SET_TMR(0x%02x)\n", OP2); break;
		case 0x48: /* SOUND ONE */ OUT("SOUND_ONE\n"); break;
		case 0x49: /* SOUND LOOP */ OUT("SOUND_LOOP\n"); break;
		case 0x4a: /* SOUND OFF */ OUT("SOUND_OFF\n"); break;
		case 0x4b: /* SOUND A */ OUT("SOUND(a)\n"); break;

		case 0x4c: /* READ R4A */
		case 0x4d: /* READF R4A */
		case 0x4e: /* READ MR0A */
		case 0x4f: /* READF MR0A */
			x = op & 1 ? 0xf : pc >> 8;
			OUT("a = rom_%x[a << 4 | %s];\n", x, op & 2 ? "r4" : "m[r1r0]");
			OUT("%s = a >> 4; a &= 15;\n", op & 2 ? "m[r1r0]" : "r4");
			break;

		CASE8(0x50) CASE8(0x58) // MOV R1R0, imm8
		CASE8(0x60) CASE8(0x68) // MOV R3R2, imm8
			OUT("%s = 0x%02x;\n", op & 0x10 ? "r1r0" : "r3r2",
					(OP2 & 15) << 4 | (op & 15)); break;
		CASE8(0x70) CASE8(0x78) /* MOV A, imm4 */
			OUT("a = 0x%x;\n", op & 15); break;

#define JMP11 x = (pc & 0x800) | (op & 7) << 8 | OP2;
#define X(cond) JMP11 OUT("if (%s) goto l_%03x;\n", cond, x); break;
		CASE8(0x80) CASE8(0x88) // JAn imm11
		CASE8(0x90) CASE8(0x98)
			JMP11 OUT("if (a & %u) goto l_%03x;\n", 1 << (op >> 3 & 3), x); break;
		CASE8(0xa0) /* JNZ R0, imm11 */ X("r1r0 & 15")
		CASE8(0xa8) /* JNZ R1, imm11 */ X("r1r0 & 0xf0")
		CASE8(0xb0) /* JZ A, imm11 */ X("!a")
		CASE8(0xb8) /* JNZ A, imm11 */ X("a")
		CASE8(0xc0) /* JC imm11 */ X("cf")
		CASE8(0xc8) /* JNC imm11 */ X("!cf")
		CASE8(0xd0) /* JTMR imm11 */
			JMP11 OUT("JTMR(l_%03x, 0x%03x)\n", x, x); break;
		CASE8(0xd8) /* JNZ R4, imm11 */ X("r4")
#undef X
#undef JMP11

		CASE8(0xe0) CASE8(0xe8) // JMP imm12
			x = (op & 15) << 8 | OP2;
			OUT("goto l_%03x;\n\n", x);
			break;
		CASE8(0xf0) CASE8(0xf8) // CALL imm12
			x = (op & 15) << 8 | OP2;
			OUT("CALL(f_%03x, 0x%03x)\n", x, (pc + 2) & 0xfff);
			break;
		} // end switch
	}
}

int main(int argc, char **argv) {
	const char *rom_fn = "brickrom.bin";
	const char *marks_fn = NULL;
	const char *output_fn = "decomp_out.c";
	uint8_t rom[0x1000], marks[0x1000];
	FILE *f; unsigned n, read_mask;

	while (argc > 1) {
		if (!strcmp(argv[1], "--rom")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			rom_fn = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-m")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			marks_fn = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "-o")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			output_fn = argv[2];
			argc -= 2; argv += 2;
		} else ERR_EXIT("unknown option\n");
	}

	f = fopen(rom_fn, "rb");
	if (!f) ERR_EXIT("fopen failed\n");
	n = fread(rom, 1, sizeof(rom), f);
	fclose(f);
	if (n != sizeof(rom)) ERR_EXIT("unexpected ROM size\n");

	memset(marks, 0, 0x1000);
	read_mask = mark_opcodes(rom, 0, marks);

	if (marks_fn) {
		f = fopen(marks_fn, "wb");
		if (f) {
			n = fwrite(marks, 1, sizeof(marks), f);
			fclose(f);
		}
	}

	if (output_fn) {
		f = fopen(output_fn, "wb");
		if (f) {
			decompile(rom, marks, read_mask, f);
			fclose(f);
		}
	}
}

## Brick Game emulator

The emulator of a portable Brick Game console that uses a 4-bit microcontroller from Holtek.

Works only on Linux.

### How to run the game

1. Download the only known ROM dump [here](https://github.com/azya52/BrickEmuPy/blob/main/assets/E23PlusMarkII96in1.bin).

2. Run `make` to compile.

3. Now you can start the emulator:  
`./brickgame --rom E23PlusMarkII96in1.bin`

* Run `./brickgame --help` to show the configuation options.

* Use `--save <filename>` option to save game state on exit.

* Sound is not supported. At the beginning of the level the game plays a melody (which you won't hear), mute the sound (M key) so you don't have to wait.

* [Here](https://github.com/ilyakurdyukov/ida-holtek-4bit) is a disassembler module for IDA to explore the ROM.

### Experimental decompiler mode

Decompiles the ROM into C code. Not recommended.  
Save states are incompatible with the emulator mode.

```
$ make DECOMPILED=1 ROMNAME=E23PlusMarkII96in1.bin
$ ./brickgame --save bricksave.bin
```

Made for ROM code research.

### Controls

| Key(s)           | Action             |
|------------------|--------------------|
| Up/Space/W       | rotate             |
| Left/A           | left               |
| Down/S           | down               |
| Right/D          | right              |
| R                | reset              |
| M                | mute               |
| Escape           | exit               |
| P/Enter          | start/pause        |
| Tab              | memory map         |

### Gamepad controls

| Key(s)           | Action             |
|------------------|--------------------|
| Left Stick       | L/R/down/rotate    |
| D-Pad            | L/R/down/rotate    |
| A/B/X/Y          | rotate             |
| L1/R1            | memory map         |
| Select           | mute               |
| Start            | start/pause        |
| Mode             | reset              |

Compile with `GAMEPAD=1` option to enable gamepad support.

### "Screenshots"

Game select screen:
```
      HI-SCORE              0

/--------------------\
|                    |
|                    | NEXT
|                    |
|      [][][]        |
|    []      []      |
|    []      []      |
|    []      []      |
|    []      []      | SPEED  0
|    []      []      |
|      [][][]        | LEVEL 12
|                    |
|    [][][][][]      |
|    []              |
|    [][][][]        |  ROTATE
|            []      |
|            []      |    -->
|    []      []      |
|      [][][]        |
|                    |
|                    |
\--------------------/
```

Tetris game:
```
         SCORE            1000

/--------------------\
|                    |
|                    | NEXT
|                    |
|                    | []
|            [][]    | [][]
|            [][]    |   []
|                    |
|                    | SPEED  0
|                    |
|                    | LEVEL  0
|                    |
|[][]                |
|[][]  []            |
|[][][][]            |  ROTATE
|[][][][]  []  [][][]|
|[][][][][][]  [][]  |    -->
|[][][][][]    [][][]|
|[][][][][][]  [][][]|  PAUSE
|[][][][][][][]  [][]|
|[][][][][][]  [][][]|
\--------------------/
```


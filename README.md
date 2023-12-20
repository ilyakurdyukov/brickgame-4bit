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


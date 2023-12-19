## Brick Game emulator

The emulator of a portable Brick Game console that uses a 4-bit microcontroller from Holtek.

Works only on Linux.

### How to run the game

1. Download the only known ROM dump [here](https://github.com/azya52/BrickEmuPy/blob/main/assets/E23PlusMarkII96in1.bin).

2. Run `make` to compile.

3. Now you can start the emulator:  
`./brickgame --rom E23PlusMarkII96in1.bin`

* Run `./brickgame --help` to show the configuation options.

* The game and hi-scores are not saved at exit.

* Sound is not supported. At the beginning of the level the game plays a melody (which you won't hear), mute the sound (M key) so you don't have to wait.

* [Here](https://github.com/ilyakurdyukov/ida-holtek-4bit) is a disassembler module for IDA to explore the ROM.

### Controls

| Key(s)           | Action             |
|------------------|--------------------|
| Left/Right/Down  | left/right/down    |
| Up/Space         | rotate             |
| R/Esc            | reset              |
| M                | mute               |
| Q                | exit               |
| P/Enter          | start/pause        |


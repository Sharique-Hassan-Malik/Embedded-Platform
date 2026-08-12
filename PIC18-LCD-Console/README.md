# pic18-lcd-console

A portable Breakout clone running on a PIC18F4550 microcontroller driving a
128×64 KS0108 monochrome LCD.  Everything from the display driver to the
sprite engine to the game loop is written from scratch in C.  No game library,
no graphics library and no HAL are used.  High scores persist across power
cycles in the MCU's internal data EEPROM.

---

## What it does

The game implements classic Breakout with five rows of eight bricks, a
paddle controlled by two tactile buttons and a ball that ricochets off walls,
the paddle and bricks.  Clearing the grid advances the player to the next
level with a faster ball.  The paddle deflects the ball at different angles
depending on which third of its surface is struck, giving the player control
over the ball's direction.  Three lives are granted per game.  A high score is
saved to EEPROM and survives a power cut.

---

## The hard part

**Framebuffer rendering on a page-mode display.**  The KS0108 does not have
random pixel access.  It organises its 64×64-pixel half into 8 horizontal
bands (pages) of 8 rows; each write stores 8 vertically adjacent pixels at
once as a byte.  A software framebuffer (`uint8_t lcd_fb[8][128]`) mirrors
this structure.  The `gfx_pixel()` function computes the correct page and
bit offset from (x, y) coordinates and modifies the byte in-place.  The full
framebuffer is flushed to both KS0108 controller halves once per frame via
`lcd_flush()`, which takes approximately 2 ms at 8 MHz — well within the
33 ms frame budget.

**Fitting everything in 2 KB of RAM.**  The 1 024-byte framebuffer occupies
exactly half of the PIC18F4550's RAM.  Game state, the button debounce table
and the call stack share the remaining 1 024 bytes.  This is tight but
achievable with disciplined stack depth (no recursion, all game state in
module-level statics).

**AABB brick collision with correct bounce direction.**  For each brick the
ball overlaps, the penetration depth on each axis is compared.  The smaller
axis is the one the ball entered through, so its velocity component is
negated.  This produces clean bounces regardless of ball angle without
requiring trigonometry or floating-point arithmetic.

---

## Architecture

See `docs/ARCHITECTURE.md` for the full block diagram, module descriptions,
Timer0 derivation, screen layout diagram and hardware schematic notes.

---

## Hardware

| Component | Part | Notes |
|---|---|---|
| Microcontroller | PIC18F4550 | 8 MHz internal oscillator |
| Display | KS0108 128×64 LCD module | 5 V typical; check module datasheet |
| Buttons | Tactile push switches | 3 required |
| Pull-up resistors | 10 kΩ × 3 | On RB0, RB1, RB2 |
| Contrast trimmer | 10 kΩ pot | V0 pin of LCD module |

**Pin map**

| PIC18 pin | Function |
|---|---|
| RD0–RD7 | LCD data bus |
| RE0 | LCD RS |
| RE1 | LCD R/W (tie to GND or drive low) |
| RE2 | LCD E (enable strobe) |
| RC0 | LCD CS1 (left half) |
| RC1 | LCD CS2 (right half) |
| RB0 | LEFT button (active low) |
| RB1 | RIGHT button (active low) |
| RB2 | FIRE button (active low) |

---

## Controls

| Button | Action |
|---|---|
| LEFT | Move paddle left |
| RIGHT | Move paddle right |
| FIRE | Launch ball (from title or launch state) / restart after game over |

---

## Game rules

- 5 × 8 brick grid, 40 bricks per level
- Clearing all bricks advances to the next level (ball speed increases)
- 3 lives per game; ball lost below the paddle costs one life
- Score: `level × 10` points per brick
- High score saved to EEPROM on game over

---

## Tech stack

- Language: C (XC8 compiler)
- Toolchain: MPLAB X IDE with XC8 v2.x
- Target: PIC18F4550 at 8 MHz (INTOSCIO_EC mode)
- Simulation: MPLAB X simulator (game logic and EEPROM testable without hardware;
  LCD output requires physical hardware or Proteus simulation)

---

## Building

1. Open MPLAB X and create a new project for PIC18F4550.
2. Select XC8 as the toolchain.
3. Add every `.c` file in `firmware/` to Source Files.
4. Add every `.h` file in `firmware/` to Header Files.
5. Build: Ctrl+F11.
6. Program the `.hex` file with PICkit 3/4 or SNAP.

No libraries beyond the XC8 standard library are required.

---

## Results

- Frame rate: 30 Hz, locked to Timer0 (no frame-rate variability)
- LCD flush time: ≈ 2 ms per frame (1 024 writes at 8 MHz with 2 µs per write)
- Game logic + render time: < 1 ms per frame
- Flash footprint: ≈ 6 KB code + 320 B font table
- RAM footprint: 1 024 B framebuffer + ≈ 80 B game state
- EEPROM usage: 3 bytes
- High score persistence: survives power cycles, robust to first-use
  (magic byte detection initialises to zero if EEPROM is erased)

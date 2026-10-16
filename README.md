# Tetris 3DS

A simple Tetris for the Nintendo 3DS (homebrew) with a **two-console local
wireless versus mode**. Clearing lines sends garbage rows to the other player.

## Features

- 7-bag randomizer, hold, 3-piece next queue, ghost piece, wall kicks, lock delay
- Solo marathon mode (speeds up every 10 lines)
- Local wireless versus between two 3DS consoles (no internet needed)
  - 2 / 3 / 4 lines cleared send 1 / 2 / 4 garbage lines
  - Clearing lines first cancels garbage that is incoming to you (red meter)
  - Your rival's board is shown live in the corner of the top screen
  - Win counter and rematches without reconnecting

## Controls

| Button            | Action               |
|-------------------|----------------------|
| D-Pad / Circle Pad| Move, Down = soft drop |
| Up                | Hard drop            |
| A / X             | Rotate clockwise     |
| B / Y             | Rotate counter-clockwise |
| L / R             | Hold                 |
| START             | Pause (solo) / leave prompt (versus) |

## Playing versus

1. Launch the game on both consoles (wireless must be switched on).
2. On one 3DS press **X** to host.
3. On the other press **Y** to join. The match starts as soon as they connect.
4. After a round, both players press **A** for a rematch.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`3ds-dev` group installed.

```sh
make
```

This produces `tetris3ds.3dsx`. Copy it to `/3ds/` on the SD card and launch it
from the Homebrew Launcher on each console.

Every push is also built by GitHub Actions; the `.3dsx` can be downloaded from
the workflow run's artifacts.

## Code layout

- `source/game.c` – platform-independent Tetris rules
- `source/net.c` – local wireless (UDS) host / join / send / receive
- `source/main.c` – rendering, menus, input and the versus protocol

# jade-empire-coop

Work-in-progress **online co-op mod for Jade Empire: Special Edition (PC, 2007)**.
Not playable yet. This repo holds our own code only: no game files, assets or scripts
from the game.

Research notes, test logs and sources live in the coop-anything hub:
[`games/jade-empire/NOTES.md`](https://github.com/indy2000/coop-anything/blob/game/jade-empire/games/jade-empire/NOTES.md)
(branch `game/jade-empire`), claim issue
[cranebeaker/coop-anything#407](https://github.com/cranebeaker/coop-anything/issues/407).

## Supported game build

Only this build; the DLL checks its patch sites and refuses to patch anything else.

| | |
|---|---|
| Store | Steam |
| `JadeEmpire.exe` | v1.00, build 1510, SHA-256 `0d25f18c028a7dffff9dd110b7be677f7e9b63e1cad86c1eb8e4e8e189c54fe5` |

The exe is PECompact-packed and checks Steam ownership through the legacy `steam.dll`.
This mod does **not** bypass that check: it loads into the game after a normal start.

## What works today

| Piece | Status |
|---|---|
| `jebridge` in-game bridge (proxy `xinput9_1_0.dll`) | runs on the game's main thread; coop-anything bridge protocol v1 over TCP 127.0.0.1 and a file transport |
| Run NWScript inside the game | `je.runScript`, `je.evalScript` (int results), `je.loadScript` (scripts served from memory, hot-replaceable, no restart) |
| Keep the world running when unfocused | `je.keepActive` / marker `bridge\keep_active` |
| Two game copies on one PC | marker `bridge\multi_instance` (per-process single-instance mutex) |
| Remote-player puppet | spawn → resurrect → walk → deactivate; follows a player across two game instances (automated test 5/5, mutation 1/5) |
| Game-to-game sync (`dll/netsync.inc`) | each game sends its player position over UDP and drives the other's puppet itself; two copies on one PC over 127.0.0.1: `client/crossnet.py` 9/9, mutation (`je.netPause` on B) all legs fail |
| Steam transport (`dll/steamnet.inc`, AppID 480 Spacewar relay) | friends-only lobby, session accept limited to lobby members; tested on one PC with a mock `steam_api.dll` (`mock/`): `client/crossnet.py` 13/13, mutation (B paused) all 4 legs fail. **Not yet tested with real Steam between two accounts** |
| Combat sync, story sync, level transitions | not started |

## Layout

- `dll/` — `jebridge.c` (freestanding C, no CRT), `.def` import lists, `build.sh`.
- `tools/ncsasm.py` — tiny NWScript bytecode (NCS V1.0) assembler used for probe scripts.
- `mock/` — `steam_api_mock.c`, a fake `steam_api.dll` (flat API subset, UDP on 127.0.0.1) so
  two game copies on one PC can test the Steam transport; config `steam_mock.txt` in the copy.
- `client/` — Python client (`jeb.py`, file transport), script builders (`ncs.py`), bot
  helpers (`bot.py`) and the automated tests (`*test*.py`, `crossfollow.py`).

## Build

Needs clang and lld (tested with LLVM 18). No Windows SDK.

```sh
sh dll/build.sh     # -> dll/out/xinput9_1_0.dll, prints its SHA-256 (reproducible: /Brepro)
```

## Use (test installs only)

Never put this into your real game folder. Make a copy of the install and, in the copy:

1. Copy Steam's own `steam.dll` from the Steam folder into the copy (the copy cannot find
   Steam otherwise; ownership is still checked).
2. Rename the copy's `xinput9_1_0.dll` to `xinput_orig.dll`, put `dll/out/xinput9_1_0.dll`
   in its place.
3. Optional markers in `<copy>\bridge\`: `keep_active`, `multi_instance`; `net.txt` turns on
   game-to-game sync (see the header of `dll/netsync.inc`); arm it with `je.netStart` once the
   player is in game. For Steam: `transport steam`, `role host` (lobby id goes to
   `bridge\lobby.txt`) or `role join` + `lobby <id>`, and put the 32-bit `steam_api.dll` from
   Steamworks.NET Standalone 2025.164.1 (SHA-256
   `fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da`) next to the exe.
4. Start the copy's `JadeEmpireLauncher.exe` / `JadeEmpire.exe` with Steam running.
   The bridge writes `jebridge.log` and `coopbridge.json` next to the exe.

Tests: point `client/jeb.py` at the copy (`JEB_INSTALL` / `jeb.use()`), load a game, then
for example `python3 client/restest.py` or `python3 client/crossfollow.py`.

## License

MIT, see `LICENSE`. Jade Empire is © its owners; this project is not affiliated with
them.

# Cheats

The overlay (F1, or Back+Start) and the launcher have a **Cheats** tab. It has two halves:

- **The game's own cheats.** Everything the original game has: its nine button codes and the
  costumes and functions of its Extras screen. A switch or button in the tab does what entering the
  code (or selecting the icon) does, through the game's own code. Nothing about them is stored by
  the port: they are saved, reset and forgotten exactly as on the console.
- **Port cheats.** A few that this port adds. Each is off by default, lives in `config.ini`'s
  `[cheats]` section and is never part of a save.

With every port cheat off and no code entered, the game plays exactly as it does without the tab.

## The game's own cheats

### Button codes

On the console: pause, hold L and R (LT and RT on an Xbox controller; R and E on the keyboard by
default), and press eight buttons, each X or Y (F or Q on the keyboard by default), within two
seconds of each other. A wrong button or a pause of two seconds starts over.
That still works in the port, unchanged (`zGameCheatsUpdate`, `zGameExtras.cpp`). The tab's
switches and **Enter** buttons do the same thing without the buttons. The four codes that are saved
with the game and cannot be undone (all health upgrades, all moves, all moves upgraded, all tasks)
show that in yellow and take two presses: the first turns the button into **Enter for good?**, the
second enters the code, and a click or press anywhere else takes the question back.

They work **while a level is running** (in the original: on the pause screen of a level). At the
title screen, and in the launcher, the tab lists them greyed.

| Code | Name in the tab | What it does | How long it lasts | Bit |
|---|---|---|---|---|
| X X Y Y X Y Y X | Double Manliness Points | every Manliness Point picked up counts twice | until you quit the game; entering it a second time clears its bit but not the doubling (the original does the same) | 0x40000 |
| X X X X Y X Y X | All health upgrades | SpongeBob's and Patrick's health goes up to six units, as if every upgrade was bought; it does not fill your health up | saved with the game | 0x100 |
| Y Y X Y X X Y Y | All moves | every move not learned yet is learned | saved with the game | 0x20000 |
| Y Y X Y X X Y X | All moves, upgraded | every move learned and at its Macho level | saved with the game | 0x400000 |
| X Y X X Y X Y Y | All tasks | every task not given yet is given, so the pause menu's task list can go to any of them (from the tab with the task list already showing: leave the pause menu and open it again to see them) | saved with the game | 0x800000 |
| Y Y Y Y X Y Y Y | SpongeBob: Caveman costume | the costume, from the next level load; takes off SpongeBob's other costumes | until the title screen | 0x2000000 |
| Y Y Y Y X Y Y X | SpongeBob: Ripped Shorts costume | as above | until the title screen | 0x4000000 |
| Y Y Y Y X Y X Y | Patrick: Goofy Goober costume | as above, for Patrick | until the title screen | 0x8000000 |
| Y Y Y Y X Y X X | Patrick: Caveman costume | as above, for Patrick | until the title screen | 0x10000000 |

X and Y are the GameCube's (and an Xbox controller's) X and Y; `docs/CONTROLS.md` has the other
controllers and the keyboard. The code reads the PlayStation 2 pad's Circle and Square for them
(`zGameExtras.cpp`, `XPAD_BUTTON_O` and `XPAD_BUTTON_SQUARE`); that is not checked on a PlayStation 2.

How the game stores them:

- **The cheat word** (`g_flg_chEnabled`, read with `zGameExtras_CheatFlags`). A matched code sets its
  bit, or flips it for the five that toggle (double points and the costumes), then runs its
  callback and plays the success sound.
- **In a save:** only the low 16 bits of the word are written (`zGameExtras_Save`, in the save's
  counters block after the task counters), and a load ORs them back in. Of the nine codes only
  *All health upgrades* (0x100) is in those bits. The effects of the other one-shot codes are saved
  as ordinary progress: moves and health upgrades with the player's upgrades, tasks with the task
  counters.
- **At the title screen** the word is set to 0 (`zMenuLoop`, on the way in and out), so the costume
  codes and the double points bit are forgotten there. The points multiplier itself
  (`globals.player.InvManlinessPointsMultiplier`) is only ever set to 1 when the game starts, so
  doubled points stay doubled until the game is quit.
- A switch in the tab shows the bit as the game holds it, read every frame, so a code entered with
  the buttons shows in the tab too.

The tab's table (`iCheats.cpp`) is checked against the game's own (`cheatList`) once per start, when
a level first runs; the log says `tssm: cheats: the tab's 9 button codes are the game's`.
`tssm_cheattest` checks it against the source.

**In the game's data but connected to nothing.** `zGameExtras.cpp` has sixteen more button sequences
that no row of `cheatList` uses (names as in the source): AddShiny, AddSpatulas, BubbleBowl,
CruiseBubble, MonsterGallery, ArtTheatre, TestCheats, Manliness, GoofyGoober, BubbleBash,
MachoBowl, SonicGuitar, BellyFlop, PickupThrow, TenSkillPoints and TenTreasureChests. Entering
one does nothing, so the tab does not list them. The PlayStation 2 demo has the same nine cheat
callbacks and no others. Two more leftovers: the Sonic Wave
Guitar has a second set of steering values for a cheat bit (0x20000000) that no code sets, and the
game works out a set of "holiday" flags from the date that nothing reads.

### Extras screen

The pause menu's Extras screen opens one item for every treasure chest collected (`Extra` lines in
`SB04.INI`). Four of the items are costumes and seven are "functions". Selecting one switches it on
or off. The tab has the same switches; a click is the Extras screen's own select
(`zUICustomExtrasManager::SelectCurrentIcon`).

| Item | Treasure chests | What it does |
|---|---|---|
| Mermaid Man SpongeBob | 2 | costume, from the next level load |
| Long Buckethead Antennas | 3 | the bucket-headed fish have antennas twice as long |
| New Targetting Hand | 7 | a different hand marks what you are aiming at |
| Buckethead Antenna Fountain | 12 | the antennas spray like fountains |
| Fish Hook Hand | 17 | a fish hook, not the hand, brings you back from the level's edge |
| Disco Patrick | 20 | costume, from the next level load |
| Tiny Foggers | 25 | the Fogger enemies are tiny |
| La Cucaracha Horn | 31 | the Patty Wagon's horn |
| Plain Patrick | 32 | costume, from the next level load |
| Magnetic Pickup | 39 | pickups fly to you from four times as far |
| Plain Sponge SpongeBob | 41 | costume, from the next level load |

- An item is locked until you have its treasure chests, in the tab as on the Extras screen (the tab
  says how many it needs). The port cheat *Extras without treasure chests* lifts that for the tab.
  An item that is on can always be switched off in the tab, so one switched on with that cheat is
  not left stuck on when the cheat is turned off again.
- The switches are not saved. They all go off whenever the game goes back to the title screen
  (`zUICustomExtrasManager::ResetExtrasStatus`); the treasure chests are saved.
- Putting a costume on takes other costumes off, including the code costumes, by the game's own
  rule (`ClearAllButThisCostume`), quirks included. It is not always the same character's: Plain
  Sponge SpongeBob takes Patrick's costumes off and leaves Mermaid Man SpongeBob on, and Plain
  Patrick takes SpongeBob's off. The tab does what the Extras screen does.
- The tab does not start or stop the icon's spin on the Extras screen; the screen sets it from the
  switch the next time it is entered.

## Port cheats

```
[cheats]
no_damage = off
fly = off
unlock_extras = off
```

| Key | In the tab | What it does | Applies |
|---|---|---|---|
| `no_damage` | No damage | Hits knock you about but take no health. It is the developers' own switch (`zGlobalSettings::TakeDamage`, read by `zEntPlayer_Damage`). Falling off the level, and the few things that end a try at once, still do. | at once |
| `fly` | Flight | The developers' flight (`CheatFlyToggle`). Hold white and press black (LB and RB on an Xbox controller, Z and C on the keyboard by default) to start and stop. While flying you pass through walls, nothing hurts you, Y rises, the Karate Spin button sinks, and holding a trigger flies faster. While it is on, Select and Z in menus are ignored while white is held, as the original does with its flight switch on; turned off outside a level, that lasts until the next level frame. | at once; turned off in mid-air, the buttons keep working until you have landed with them |
| `unlock_extras` | Extras without treasure chests | The tab switches every Extras item whatever the treasure chest count. The Extras screen itself still asks for the chests, and the count is not changed. | at once; items already on stay on until you switch them off (the tab still can) or go back to the title screen |

- **Refill health** (a button, not a setting): once, the character you are playing gets full health.
- **Turn all port cheats off** sets the three keys to `off`.
- None of them is written into a save, so saves stay the original's. What you do with them is still
  your progress: a task finished while flying is saved as finished.
- **Flight can break a level.** You can fly past what a level expects you to do and into places it
  never meant you to reach: tasks may not start or finish, and you can get stuck. Save first.
- With all three off the port writes nothing into the game: the two developers' switches keep the
  values the game gives them (1 and 0) and are only put back to those when a cheat is turned off.

### What the port does not add

The BfBB PC port's cheats were the starting point. Of those that fit this game:

- **Infinite health** is *No damage* here (the same developers' switch).
- **All moves and upgrades** are the game's own codes (*All moves*, *All moves, upgraded*, *All
  health upgrades*), so the port adds none of its own.
- **All tokens** is left out: the Goofy Goober tokens only buy moves and health upgrades, which the
  codes above already give, and the count is saved with the game.
- **A warp or level select** is left out: the game's own warp is the pause menu's task list, and
  *All tasks* opens every task there. The BfBB port's warp cheat sets a developers' switch
  (`CheatAlwaysPortal`) that nothing in this game reads. `[game] boot` in `config.ini` still starts
  the game in any level, for testing.

## For developers

| File | What |
|---|---|
| `src/SB/Core/pc/iCheats.h` | the interface |
| `src/SB/Core/pc/iCheats.cpp` | the tables, the port cheats' state, the tab's request queue and the co-op state; no game code |
| `src/SB/Core/pc/iCheatsGame.cpp` | the game side (a game unit): `iCheatsGameFrame`, the status the tab shows, the test hooks |
| `src/SB/Core/pc/gui/iGuiCheats.cpp` | the tab |
| `src/SB/Game/zGame.cpp` | under `PLATFORM_PC`: `iCheatsGameFrame()` in `zGameLoop`, right after `zGameExtras_SceneUpdate` (where the original takes its codes) |
| `src/SB/Game/zGameExtras.cpp` | under `PLATFORM_PC`: `zGameExtras_PCCheat`, `zGameExtras_PCEnterCheat` (what `zGameCheatsUpdate` does on a match), `zGameExtras_PCSetCheatFlags` |
| `src/SB/Game/zMenuSB.cpp` | under `PLATFORM_PC`: `zMenuSB_PCExtraCount`, `zMenuSB_PCExtra`, `zMenuSB_PCToggleExtra` (what the Extras screen's select does), `zMenuSB_PCSetExtraStatus` |

The tab never changes the game from the drawing code. A click queues a request (`iCheatsRequest`);
`iCheatsGameFrame` takes it on the next frame of `zGameLoop`. The tab knows a level is running from
those frames (`iCheatsStatus::inGame`, also false once the last such frame is a second old); a
request left over when the game leaves the level is dropped.

### Co-op

The cheats change how the game plays, so in a co-op game the host decides. `iCheatsShared` is all
of it in one struct: the port cheats in force, the game's cheat word, which Extras items are on,
and the points multiplier (24 bytes on the wire, `iCheatsSharedPack`/`Unpack`).

- The host calls `iCheatsSharedGet` and sends the result when it changes (comparing the 24 bytes
  each frame is enough) and when a guest joins.
- The guest calls `iCheatsSharedSet` with what arrives. From then on `iCheatsGameFrame` holds the
  guest's game to it every frame: a code the host has on is entered on the guest too (so its
  callback runs there), then the word, the Extras items and the multiplier are the host's. The
  guest's own `[cheats]` keys wait, its requests are refused and the tab's switches are greyed with
  a line saying so.
- `iCheatsSharedRelease` when the session ends gives the guest its own `[cheats]` settings back.
  It only stops the following: the cheat word and the Extras items stay as the host had them until
  the title screen, and the points multiplier until the game is quit. The co-op merge must decide
  whether to reset them (for example by going through the title screen on leaving a session).
- Not in the struct: *Refill health*. It is a one-off change to a player's health, refused on a
  guest; the co-op merge treats it like any other change to the host's player.

### Tests

- `tssm_cheattest` (ctest): the tables against `zGameExtras.cpp`'s source, the `[cheats]` keys
  through the real config layer, the request queue and the co-op state.
- `tools/cheat_runs.py --exe build-pc/tssm.exe --assets DIR --out DIR`: scripted runs in PT03 that
  enter every code, switch Extras items, take real hits with and without `no_damage`, fly, enter a
  code with the buttons on the pause screen, follow a host's state, and check that nothing is
  touched when nothing is on. It reads the game's state from the log.
- `TSSM_TEST_CHEAT=<frame>:<what>,...` asks for things at given frames, the way the tab does:
  `code:<id>` (`max_health`, `all_moves`, `all_macho_moves`, `all_tasks`, `double_points`,
  `spongebob_caveman`, `spongebob_ripped_shorts`, `patrick_goofy_goober`, `patrick_caveman`),
  `extra:<word>` (`mermaid_spongebob`, `plain_spongebob`, `disco_patrick`, `plain_patrick`,
  `targeting_hand`, `fish_hook_hand`, `cucaracha_horn`, `long_antennas`, `antenna_fountain`,
  `tiny_foggers`, `magnetic_pickup`), `refill`, `port:<key>:on|off`, `port:alloff`, `hit:<n>` (a
  hit of n health units through `zEntPlayer_Damage`), `host:<port>:<word>:<extras low>:<extras
  high>:<multiplier>` (hexadecimal; a host's state arriving) and `release`.
- `TSSM_TEST_CHEATLOG=<n>` logs the state every n frames (`tssm: test: frame 210: cheats word
  0x00000100 multiplier 1 health 3/6 ...`).

### Other platforms

Nothing here is Windows-only: the state and the game side are plain C++ over the config layer, and
the tab is ImGui like the others. For Android, the Switch and the 3DS:

- The tab is a row of `gui/iGuiTabs.cpp`'s list, so a platform that builds the overlay gets it.
- Flight's buttons are white and black. A platform whose controller has no such buttons needs them
  bound (`docs/CONTROLS.md`), or an on-screen pair. The tab's text names LB and RB and the PC
  keyboard's keys (for flight and for entering a code); a port should name its own buttons there
  (`iCheats.cpp`'s table and `gui/iGuiCheats.cpp`).
- Co-op builds: the merge wires `iCheatsShared` into the session as described under "Co-op" above.
- A build without the overlay (`TSSM_GUI=OFF`) still reads `[cheats]` from `config.ini`.

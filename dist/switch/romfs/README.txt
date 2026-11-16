The files inside the .nro (Nintendo Switch homebrew).

This RomFS holds only the port's own files: this note and the licenses of the
libraries the program is built from (licenses/). It never holds any of the
game's data. The game's files are the player's own, copied to the SD card:

  /switch/tssm/tssm.nro      this program
  /switch/tssm/assets/       the game's Xbox files (as the Android port's assets folder)
  /switch/tssm/config.ini    settings, written on the first run
  /switch/tssm/saves/        saves
  /switch/tssm/mods/         mods
  /switch/tssm/last_run.log  the log of the last run; crash.log beside it

docs/SWITCH.md in the source has the rest.

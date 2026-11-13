@echo off
rem Local co-op: two players on this PC (docs\MULTIPLAYER.md 12). Starts the game as
rem Player 1; Player 2's game opens by itself once Player 1 is in a level, and joins it.
cd /d "%~dp0"
start "" "%~dp0tssm.exe" --local-coop

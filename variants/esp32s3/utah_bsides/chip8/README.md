# CHIP-8 games

From John Earnest's CHIP-8 Archive (https://github.com/JohnEarnest/chip8Archive), whose README
places everything in it under Creative Commons 0 (public domain). `games.json` keeps each game's
title, authors, speed (instructions per 1/60 s frame) and the Octo quirk flags it expects.

Add a game: put its `.ch8` here and an entry in `games.json`; `tools/make_chip8.py` builds them
into the firmware.

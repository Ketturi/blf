 ____  _     _____      _    __         ____ 
| __ )| |   |  ___|    / \  / /_    _  |____|
|  _ \| |   | |_      / _ \| '_ \ _| |_ 
| |_) | |___|  _|    / ___ \ (_) |_   _|
|____/|_____|_|     /_/   \_\___/  |_|  
--------------------------------------------
README
--------------------------------------------
This firmware modifies several key items to personalize the
functionality of the attiny13a-based BLF-A6 driver, and compatibles.
This repo will contain up-to-date attiny13 firmware in both
.elf and .hex formats.

--------------------------------------------
NEW FEATURES
--------------------------------------------
- Fixed build-time config, no config mode (broken+not needed)
- Backported biking mode from ToyKeeper
- Beacon mode tweaks
- Powersaving tweaks for lower cutoff current
- Shuffled special modes
- Dimmer blinks for battery check
- Calibrated values for this spesific light
                                      
--------------------------------------------
USAGE
--------------------------------------------

Glossary:
---------
Short Press - A short tap that quickly breaks the circuit for <0.5 seconds
Medium Press - A middling tap that breaks the circuit for 1-1.5 seconds
Long Press - A long press that breaks the circuit for >3 seconds

Functionality:
--------------
Short press to advance to the next mode.

Medium press (if enabled) to reverse to the previous mode.  If you are at 
the first mode, you will enter the "Hidden" modes (see "Hidden Modes").

Long press returns back to last mode used. Flashlight remembers mode when
turned off and back on.

Normal Modes:
-------------
1. Mode group 1 
 - 8 modes, moon to turbo, visually linear

Hidden modes:
-------------
Hidden modes cannot be accessed without medium press enabled.

To access hidden modes, medium press at 'moon', the dimmest normal mode.

Hidden modes can only be cycled through with a medium press, a short press
will bring you back to the first mode of the mode group you are in.

Hidden modes are always in this order:

1. Battery Check (1 flash per 25%, 5 for 100%)
2. Turbo
3. Biking Strobe (High, with turbo strobes for visibility) 
4. Beacon (2 flash every 2.5 seconds, longlife)
5. 10Hz Strobe
6. SOS ( Flash out ...---...)
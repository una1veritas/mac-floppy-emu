To update the Xilinx CPLD firmware:

1. Copy firmware.xvf to the root directory of your SD card, and insert it into Floppy Emu.
2. Hold down the PREV and NEXT buttons.
3. Press and release the RESET button.
4. Follow the on-screen prompts.


To update the microcontroller application software:

If you have the SD bootloader already installed:
1. Copy femu.bin to the root directory of your SD card, and insert it into Floppy Emu.
2. Hold down the PREV and SELECT buttons.
3. Press and release the RESET button.
4. Follow the on-screen prompts.

If you don't have the SD bootloader installed:
1. Use your AVR ISP programmer to flash floppyemu.hex to the microcontroller.

If you want to install the SD bootloader:
1. Use your AVR ISP programmer to flash merged.hex to the microcontroller.
2. Use the ISP programmer to set the BOOTRST fuse to 1 (on), and the BOOTSZ fuse to 2048W_F800. (Fuses should be Extended: 0xFC, High: 0xDA, Low: 0xBF)



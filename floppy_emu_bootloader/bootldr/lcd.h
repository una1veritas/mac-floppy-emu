/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

void LcdCommand(void);
void LcdData(void);
void LcdWrite(uint8_t data);
void LcdGoto(uint8_t x, uint8_t y);
void LcdClear(void);
void LcdString(const char *s);
void LcdInit(void);
/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

#ifndef NOKLCD_H_
#define NOKLCD_H_

#include <inttypes.h>

#define LCD_WIDTH 84
#define LCD_HEIGHT 48

#define LCD_CMD 0
#define LCD_DATA 1

#define TEXT_NORMAL 0
#define TEXT_INVERSE 1

extern volatile uint8_t lcd_vop;
extern volatile uint8_t lcd_bias;
extern volatile uint8_t lcd_tempCoef;

void LcdReset(void);
void LcdClear(void);
void LcdGoto(uint8_t x, uint8_t y);
void LcdWrite(uint8_t dc, uint8_t data);
void LcdTinyString(const char *characters, uint8_t inverse, uint8_t maxWidth = 84);
void LcdTinyStringP(PGM_P characters, uint8_t inverse);
void LcdTinyStringFramed(const char *characters);

#endif /* NOKLCD_H_ */

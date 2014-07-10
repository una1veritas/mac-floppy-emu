/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

void init_spi (void);		/* Initialize SPI port (asmfunc.S) */
void deselect (void);		/* Select MMC (asmfunc.S) */
void select (void);			/* Deselect MMC (asmfunc.S) */
void xmit_spi (uint8_t d);		/* Send a byte to the MMC (asmfunc.S) */
void dly_100us (void);		/* Delay 100 microseconds (asmfunc.S) */

extern const uint8_t tiny_font[][3] PROGMEM;
const uint8_t tiny_font[][3] = {
	{0x1e,0x05,0x1e}, // 41 A
	{0x1f,0x15,0x0a}, // 42 B
	{0x0e,0x11,0x11}, // 43 C
	{0x1f,0x11,0x0e}, // 44 D
	{0x1f,0x15,0x15}, // 45 E
	{0x1f,0x05,0x05}, // 46 F
	{0x0e,0x15,0x1d}, // 47 G
	{0x1f,0x04,0x1f}, // 48 H
	{0x11,0x1f,0x11}, // 49 I
	{0x08,0x10,0x0f}, // 4a J
	{0x1f,0x04,0x1b}, // 4b K
	{0x1f,0x10,0x10}, // 4c L
	{0x1f,0x06,0x1f}, // 4d M
	{0x1f,0x0e,0x1f}, // 4e N		
	{0x0e,0x11,0x0e}, // 4f O	
	{0x1f,0x05,0x02}, // 50 P				
	{0x00,0x10,0x00}, // 51 . (not Q)
	{0x1f,0x0d,0x16}, // 52 R
	{0x12,0x15,0x09}, // 53 S
	{0x01,0x1f,0x01}, // 54 T
	{0x0f,0x10,0x1f}, // 55 U
	{0x07,0x18,0x07}, // 56 V		
	{0x1f,0x0c,0x1f}, // 57 W
	{0x00,0x00,0x00}, // 58 space (not X)
	{0x03,0x1c,0x03}, // 59 Y
	{0x00,0x0a,0x00}  // 5a : (not Z)
};
		
#define LCD_DDR DDRB
#define LCD_PORT PORTB
#define LCD_RESET_PIN_NUMBER 0
#define LCD_CS_PIN_NUMBER 2
#define LCD_DC_DDR DDRD
#define LCD_DC_PORT PORTD
#define LCD_DC_PIN_NUMBER 6

void LcdCommand()
{
	LCD_DC_PORT &= ~(1<<LCD_DC_PIN_NUMBER);
}

void LcdData()
{
	LCD_DC_PORT |= (1<<LCD_DC_PIN_NUMBER);	
}

void LcdWrite(uint8_t data)
{
	deselect(); // make sure the SD card is not selected
	
	LCD_PORT &= ~(1<<LCD_CS_PIN_NUMBER);

	xmit_spi(data);

	LCD_PORT |= (1<<LCD_CS_PIN_NUMBER);
}

void LcdGoto(uint8_t x, uint8_t y)
{
	LcdCommand();
	LcdWrite(0x80 | x);  
	LcdWrite(0x40 | y);
}

void LcdClear(void)
{
	LcdData();
	for (uint16_t index = 0; index < 84 * 6; index++)
	{
		LcdWrite(0x00);
	}
}

void LcdString(const char *s)
{
	LcdData();

	uint8_t c;	
	while ((c = pgm_read_byte_far(s)))
	{
		unsigned short charbase = (c - 0x41) * 3;
		s++;
		
		for (uint8_t index = 0; index < 3; index++)
		{
			// need to use pgm_read_byte_far on Atmegas with more than 64K Flash
			uint8_t pixels = pgm_read_byte_far((unsigned char*)tiny_font + charbase + index);
			pixels = pixels << 1;
			LcdWrite(pixels);
		}
		
		LcdWrite(0x00);		
	}	
}

void LcdInit()
{
	// set LCD pin directions
	LCD_DDR |= (1<<LCD_RESET_PIN_NUMBER);
	LCD_DDR |= (1<<LCD_CS_PIN_NUMBER);
	LCD_DC_DDR |= (1<<LCD_DC_PIN_NUMBER);
  	
	init_spi();
	
	// toggle RST low to reset; CS low so it'll listen to us
	LCD_PORT &= ~(1<<LCD_CS_PIN_NUMBER);
	
	LCD_PORT |= (1<<LCD_RESET_PIN_NUMBER);
    for (uint8_t n = 200; n; n--) dly_100us();
	LCD_PORT &= ~(1<<LCD_RESET_PIN_NUMBER);
    for (uint8_t n = 200; n; n--) dly_100us();
    LCD_PORT |= (1<<LCD_RESET_PIN_NUMBER);
    for (uint8_t n = 200; n; n--) dly_100us();

	LcdCommand();
	LcdWrite(0x21); // LCD Extended Commands.
	LcdWrite(0xBF); // Set LCD Vop (Contrast). 
	LcdWrite(0x14); // LCD bias mode 
	LcdWrite(0x20);
	LcdWrite(0x0C); // LCD in normal mode. 0x0d for inverse	
	
	LcdGoto(0,0);
	LcdClear();
}

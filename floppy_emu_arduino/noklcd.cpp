/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

#include <math.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <string.h>

#include "noklcd.h"
#include "portmacros.h"

#define LCD_RESET_PORT B
#define LCD_RESET_PIN 0

#define LCD_CS_PORT B
#define LCD_CS_PIN 2

#define SPI_CLK_PORT B
#define SPI_CLK_PIN 7

#define SPI_DATA_PORT B
#define SPI_DATA_PIN 5

#define SPI_DC_PORT D
#define SPI_DC_PIN 6

volatile uint8_t lcd_vop;
volatile uint8_t lcd_bias;
volatile uint8_t lcd_tempCoef;

extern const uint8_t tiny_font[][3] PROGMEM;
const uint8_t tiny_font[][3] = {

	{0x00,0x00,0x00}, // 20  
	{0x00,0x17,0x00}, // 21 !
	{0x03,0x00,0x03}, // 22 "
	{0x1f,0x0a,0x1f}, // 23 #
	{0x0a,0x1f,0x05}, // 24 $
	{0x09,0x04,0x12}, // 25 %
	{0x0f,0x17,0x1c}, // 26 &
	{0x00,0x03,0x03}, // 27 ' -> degree
	{0x00,0x0e,0x11}, // 28 (
	{0x11,0x0e,0x00}, // 29 )
	{0x05,0x02,0x05}, // 2a *
	{0x04,0x0e,0x04}, // 2b +
	{0x10,0x08,0x00}, // 2c ,
	{0x04,0x04,0x04}, // 2d -		
	{0x00,0x10,0x00}, // 2e .	
	{0x18,0x04,0x03}, // 2f /				
	{0x1e,0x11,0x0f}, // 30 0
	{0x02,0x1f,0x00}, // 31 1
	{0x19,0x15,0x12}, // 32 2
	{0x15,0x15,0x0a}, // 33 3
	{0x07,0x04,0x1f}, // 34 4
	{0x17,0x15,0x09}, // 35 5		
	{0x1e,0x15,0x1d}, // 36 6
	{0x19,0x05,0x03}, // 37 7
	{0x1a,0x15,0x0b}, // 38 8
	{0x17,0x15,0x0f}, // 39 9	
	{0x00,0x0a,0x00}, // 3a :
	{0x04,0x0c,0x04}, // 3b ;	-> down arrow	
	{0x04,0x0e,0x1f}, // 3c <
	{0x0a,0x0a,0x0a}, // 3d =
	{0x1f,0x0e,0x04}, // 3e >
	{0x01,0x15,0x03}, // 3f ?			
	{0x0e,0x15,0x16}, // 40 @
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
	{0x0e,0x19,0x1e}, // 51 Q
	{0x1f,0x0d,0x16}, // 52 R
	{0x12,0x15,0x09}, // 53 S
	{0x01,0x1f,0x01}, // 54 T
	{0x0f,0x10,0x1f}, // 55 U
	{0x07,0x18,0x07}, // 56 V		
	{0x1f,0x0c,0x1f}, // 57 W
	{0x1b,0x04,0x1b}, // 58 X
	{0x03,0x1c,0x03}, // 59 Y
	{0x19,0x15,0x13}, // 5a Z	
	{0x1f,0x10,0x10}, // 5b [
	{0x02,0x04,0x08}, // 5c backslash
	{0x10,0x10,0x1f}, // 5d ]
	{0x04,0x06,0x04}, // 5e ^
	{0x10,0x10,0x10}, // 5f _
	{0x00,0x03,0x03}, // 60 ` -> degree
	{0x1a,0x16,0x1c}, // 61 a
	{0x1f,0x12,0x0c}, // 62 b
	{0x0c,0x12,0x12}, // 63 c
	{0x0c,0x12,0x1f}, // 64 d
	{0x0c,0x1a,0x16}, // 65 e
	{0x04,0x1e,0x05}, // 66 f
	{0x0c,0x2a,0x1e}, // 67 g
	{0x1f,0x02,0x1c}, // 68 h
	{0x00,0x1d,0x00}, // 69 i
	{0x10,0x20,0x1d}, // 6a j 
	{0x1f,0x0c,0x12}, // 6b k
	{0x11,0x1f,0x10}, // 6c l
	{0x1e,0x0e,0x1e}, // 6d m
	{0x1e,0x02,0x1c}, // 6e n
	{0x0c,0x12,0x0c}, // 6f o
	{0x3e,0x12,0x0c}, // 70 p
	{0x0c,0x12,0x3e}, // 71 q
	{0x1c,0x02,0x02}, // 72 r
	{0x14,0x1e,0x0a}, // 73 s
	{0x02,0x1f,0x12}, // 74 t
	{0x0e,0x10,0x1e}, // 75 u
	{0x0e,0x18,0x0e}, // 76 v
	{0x1e,0x1c,0x1e}, // 77 w
	{0x12,0x0c,0x12}, // 78 x
	{0x06,0x28,0x1e}, // 79 y
	{0x1a,0x1f,0x16}, // 7a z	
	{0x04,0x1b,0x11}, // 7b {
	{0x00,0x1f,0x00}, // 7c |
	{0x11,0x1b,0x04}, // 7d }
	{0x08,0x0c,0x04}, // 7e ~																
};

void LcdWrite(uint8_t dc, uint8_t data)
{
	// set SPI speed to "half speed": clock speed / 4
	SPCR = (1 << SPE) | (1 << MSTR);
	SPSR = 0;
			
	if (dc)
	{
		PORT(SPI_DC_PORT) |= (1<<SPI_DC_PIN);	
	}
	else
	{
		PORT(SPI_DC_PORT) &= ~(1<<SPI_DC_PIN);	
	}		  

	PORT(LCD_CS_PORT) &= ~(1<<LCD_CS_PIN);

	SPDR = data; 
	while(!(SPSR & (1<<SPIF))) 
	{}

	PORT(LCD_CS_PORT) |= (1<<LCD_CS_PIN);
}

void LcdGoto(uint8_t x, uint8_t y)
{
	LcdWrite(LCD_CMD, 0x80 | x);  
	LcdWrite(LCD_CMD, 0x40 | y);
}

void LcdTinyString(const char *characters, uint8_t inverse, uint8_t maxWidth)
{
	uint8_t width = 0;
	
	while (*characters && width < maxWidth)
	{
		if (*characters == 'm')
		{
			// special case 'm'
			characters++;
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x3c ^ 0x7F : 0x3c);
				width++;
			}				
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x04 ^ 0x7F : 0x04);
				width++;
			}
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x18 ^ 0x7F : 0x18);
				width++;
			}
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x04 ^ 0x7F : 0x04);
				width++;
			}
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x38 ^ 0x7F : 0x38);
				width++;
			}
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x7F : 0x00);
				width++;
			}
		}
		else
		{	
			unsigned short charbase = (*characters++ - 0x20) * 3;
		
			for (uint8_t index = 0; index < 3 && width < maxWidth; index++)
			{
				uint8_t pixels = pgm_read_byte((unsigned char*)tiny_font + charbase + index);
				pixels = pixels << 1;
				LcdWrite(LCD_DATA, inverse ? pixels ^ 0x7F : pixels);
				width++;
			}
		
			if (width < maxWidth)
			{
				LcdWrite(LCD_DATA, inverse ? 0x7F : 0x00);
				width++;
			}				
		}		
	}	
}

void LcdTinyStringP(PGM_P characters, uint8_t inverse)
{
	while (pgm_read_byte(characters))
	{
		if (pgm_read_byte(characters) == 'm')
		{
			// special case 'm'
			characters++;
			LcdWrite(LCD_DATA, inverse ? 0x3c ^ 0x7F : 0x3c);
			LcdWrite(LCD_DATA, inverse ? 0x04 ^ 0x7F : 0x04);
			LcdWrite(LCD_DATA, inverse ? 0x18 ^ 0x7F : 0x18);
			LcdWrite(LCD_DATA, inverse ? 0x04 ^ 0x7F : 0x04);
			LcdWrite(LCD_DATA, inverse ? 0x38 ^ 0x7F : 0x38);
			LcdWrite(LCD_DATA, inverse ? 0x7F : 0x00);
		}
		else
		{	
			unsigned short charbase = (pgm_read_byte(characters) - 0x20) * 3;
			characters++;
		
			for (uint8_t index = 0; index < 3; index++)
			{
				uint8_t pixels = pgm_read_byte((unsigned char*)tiny_font + charbase + index);
				pixels = pixels << 1;
				LcdWrite(LCD_DATA, inverse ? pixels ^ 0x7F : pixels);
			}
		
			LcdWrite(LCD_DATA, inverse ? 0x7F : 0x00);
		}		
	}	
}

void LcdTinyStringFramed(const char *characters)
{
	while (*characters)
	{
		if (*characters == 'm')
		{
			// special case 'm'
			characters++;
			LcdWrite(LCD_DATA, (0x3c >> 1) | 0x40);
			LcdWrite(LCD_DATA, (0x04 >> 1) | 0x40);
			LcdWrite(LCD_DATA, (0x18 >> 1) | 0x40);
			LcdWrite(LCD_DATA, (0x04 >> 1) | 0x40);
			LcdWrite(LCD_DATA, (0x38 >> 1) | 0x40);
			LcdWrite(LCD_DATA, (0x00 >> 1) | 0x40);
		}
		else
		{	
			unsigned short charbase = (*characters++ - 0x20) * 3;
		
			for (uint8_t index = 0; index < 3; index++)
			{
				uint8_t pixels = pgm_read_byte((unsigned char*)tiny_font + charbase + index);
				//pixels = pixels << 2;
				pixels |= 0x40;
				LcdWrite(LCD_DATA, pixels);
			}
		
			LcdWrite(LCD_DATA, 0x40);
		}		
	}	
}

void LcdClear(void)
{
	for (int index = 0; index < LCD_WIDTH * LCD_HEIGHT / 8; index++)
	{
		LcdWrite(LCD_DATA, 0x00);
	}
}

void LcdReset(void)
{
	lcd_vop = 0xBF;
	lcd_bias = 0x14;
	lcd_tempCoef = 0x04;
	
	// set pin directions
	DDR(SPI_DC_PORT) |= (1<<SPI_DC_PIN);
	DDR(SPI_CLK_PORT) |= (1<<SPI_CLK_PIN);
	DDR(SPI_DATA_PORT) |= (1<<SPI_DATA_PIN);
  	DDR(LCD_CS_PORT) |= (1<<LCD_CS_PIN);
	DDR(LCD_RESET_PORT) |= (1<<LCD_RESET_PIN);
  
	// toggle RST low to reset; CS low so it'll listen to us
	PORT(LCD_CS_PORT) &= ~(1<<LCD_CS_PIN);
	
    PORT(LCD_RESET_PORT) |= (1<<LCD_RESET_PIN);
    _delay_ms(20);
	PORT(LCD_RESET_PORT) &= ~(1<<LCD_RESET_PIN);
    _delay_ms(20);
    PORT(LCD_RESET_PORT) |= (1<<LCD_RESET_PIN);
    _delay_ms(20);

	// check for the signature byte in EEPROM
	uint8_t signature = eeprom_read_byte((uint8_t*)0);
	
	// get the stored contrast setting
	if (signature == 0xF1)
	{
		lcd_vop = eeprom_read_byte((uint8_t*)1);
	}
	else
	{
		eeprom_update_byte((uint8_t*)0, 0xF1);
		eeprom_update_byte((uint8_t*)1, lcd_vop);
	}
	
	LcdWrite(LCD_CMD, 0x21); // LCD Extended Commands.
	LcdWrite(LCD_CMD, lcd_vop); // Set LCD Vop (Contrast). 
	//LcdWrite(LCD_CMD, lcd_tempCoef); // Set Temp coefficent.
	LcdWrite(LCD_CMD, lcd_bias); // LCD bias mode 
	LcdWrite(LCD_CMD, 0x20);
	LcdWrite(LCD_CMD, 0x0C); // LCD in normal mode. 0x0d for inverse
}

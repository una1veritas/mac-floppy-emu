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
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "portmacros.h"
#include "noklcd.h"
#include "millitimer.h"
#include "SdFat.h"
#include "SdBaseFile.h"
#include "micro.h"
#include "ports.h"
#include "diskmenu.h"

#ifdef ENABLE_ORIGINAL_COMPILER_WORKAROUND

// work-around for compiler bug
#undef PROGMEM 
#define PROGMEM __attribute__(( section(".progmem.data") )) 
#undef PSTR 
#define PSTR(s) (__extension__({static prog_char __c[] PROGMEM = (s); &__c[0];})) 

#endif

// I/O pin assignments
#define CPLD_RESET_PORT B
#define CPLD_RESET_PIN 0

#define CPLD_STEP_DIR_MOTOR_ON_PORT C
#define CPLD_STEP_DIR_MOTOR_ON_PIN 7

#define CPLD_STEP_REQ_PORT D
#define CPLD_STEP_REQ_PIN 0 // PCINT24
#define CPLD_STEP_REQ_INT_MSK PCMSK3
#define CPLD_STEP_REQ_INT_PIN PCINT24
#define CPLD_STEP_REQ_INT_ENABLE PCIE3

#define CPLD_CURRENT_SIDE_PORT C
#define CPLD_CURRENT_SIDE_PIN 1 // PCINT17
#define CPLD_CURRENT_SIDE_INT_MSK PCMSK2
#define CPLD_CURRENT_SIDE_INT_PIN PCINT17
#define CPLD_CURRENT_SIDE_INT_ENABLE PCIE2

#define CPLD_EJECT_REQ_PORT D
#define CPLD_EJECT_REQ_PIN 3

#define CPLD_STEP_ACK_DISK_IN_PORT C
#define CPLD_STEP_ACK_DISK_IN_PIN 2

#define CPLD_WR_REQ_PORT C
#define CPLD_WR_REQ_PIN 0 // PCINT16
#define CPLD_WR_REQ_INT_MSK PCMSK2
#define CPLD_WR_REQ_INT_PIN PCINT16
#define CPLD_WR_REQ_INT_ENABLE PCIE2

#define CPLD_RD_READY_TK0_PORT C
#define CPLD_RD_READY_TK0_PIN 5

#define CPLD_RD_ACK_WR_TICK_PORT A
#define CPLD_RD_ACK_WR_TICK_PIN 7 // PCINT7
#define CPLD_RD_ACK_WR_TICK_INT_MSK PCMSK0
#define CPLD_RD_ACK_WR_TICK_INT_PIN PCINT7
#define CPLD_RD_ACK_WR_TICK_INT_ENABLE PCIE0

#define CPLD_DATA_PORT A

#define CPLD_DATA_HIZ_PORT C
#define CPLD_DATA_HIZ_PIN 6

#define CPLD_TACH_PORT D
#define CPLD_TACH_PIN 5

#define CPLD_TMS_PORT C
#define CPLD_TMS_PIN 3

#define SELECT_BUTTON_PORT D
#define SELECT_BUTTON_PIN 4

#define PREV_BUTTON_PORT D
#define PREV_BUTTON_PIN 1

#define NEXT_BUTTON_PORT D
#define NEXT_BUTTON_PIN 2

#define STATUS_LED_PORT B
#define STATUS_LED_PIN 3

#define CARD_WPROT_PORT D
#define CARD_WPROT_PIN 7

#define SECTOR_DATA_SIZE 512
#define INTER_SECTOR_GAP_SIZE 55
#define ADDRESS_DATA_GAP_SIZE 10
#define SECTOR_DATA_HEADER_SIZE 3
#define SECTOR_DATA_SECTORNUM_START SECTOR_DATA_HEADER_SIZE
#define SECTOR_DATA_SECTORNUM_SIZE 1
#define SECTOR_DATA_ENCODED_TAGS_START (SECTOR_DATA_HEADER_SIZE+SECTOR_DATA_SECTORNUM_SIZE)
#define SECTOR_DATA_ENCODED_TAGS_SIZE 16
#define SECTOR_DATA_ENCODED_DATA_START (SECTOR_DATA_HEADER_SIZE+SECTOR_DATA_SECTORNUM_SIZE+SECTOR_DATA_ENCODED_TAGS_SIZE)
#define SECTOR_DATA_ENCODED_DATA_SIZE 683
#define SECTOR_DATA_CHECKSUM_START (SECTOR_DATA_ENCODED_DATA_START+SECTOR_DATA_ENCODED_DATA_SIZE)

// 8 byte marker placed at the end of the program binary, used by the bootloader.
// Configure the .bootldrinfo address to be 8 bytes below the bootloader start address for the type of Atmega being used.
#define DEVICEID_HIGH 0xDDDD  
#define DEVICEID_LOW 0xDDDD     
#define VERSIONID 0x0100
const uint16_t bootloader_info[] __attribute__(( section(".bootldrinfo") )) = { DEVICEID_HIGH, DEVICEID_LOW, VERSIONID, 0x0000 };
	
const char versionStr[] PROGMEM = "App Version 1.0 L";

volatile uint8_t currentTrack;
volatile uint8_t prevTrack;
volatile uint8_t currentSide;
volatile uint8_t prevSide;
volatile uint8_t writeMode;
volatile bool restartDisk;
volatile bool writeError;

bool diskInserted;
bool readOnly;
bool mfmMode;
uint16_t crc;
uint8_t numberOfDiskSides;
uint8_t currentSector;
uint16_t driveTachHalfPeriod;
uint8_t tachFlutter;

uint8_t writeDisplayTimer;
uint8_t cpldFirmwareVersion;

#define TEXTBUF_SIZE 22
char textBuf[TEXTBUF_SIZE];

#define NUM_BUFFERS 24
uint8_t sectorBuf[NUM_BUFFERS][SECTOR_DATA_SIZE];
uint8_t extraBuf[SECTOR_DATA_SIZE];

bool selectedFileIsDiskCopyFormat;

extern const uint16_t sony_track_start[] PROGMEM;
const uint16_t sony_track_start[80] = {
	0,    12,  24,  36,  48,  60 , 72,  84,
	96,  108, 120, 132, 144, 156, 168, 180,

	192, 203, 214, 225, 236, 247, 258, 269,
	280, 291, 302, 313, 324, 335, 346, 357,

	368, 378, 388, 398, 408, 418, 428, 438,
	448, 458, 468, 478, 488, 498, 508, 518,

	528, 537, 546, 555, 564, 573, 582, 591,
	600, 609, 618, 627, 636, 645, 654, 663,

	672, 680, 688, 696, 704, 712, 720, 728,
	736, 744, 752, 760, 768, 776, 784, 792
};

extern const uint8_t sony_track_len[] PROGMEM;
const uint8_t sony_track_len[80] = {
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8
};
	
extern const uint8_t sony_to_disk_byte[] PROGMEM;
const uint8_t sony_to_disk_byte[] = {
	0x96, 0x97, 0x9A, 0x9B,  0x9D, 0x9E, 0x9F, 0xA6, /* 0x00 */
	0xA7, 0xAB, 0xAC, 0xAD,  0xAE, 0xAF, 0xB2, 0xB3,
	0xB4, 0xB5, 0xB6, 0xB7,  0xB9, 0xBA, 0xBB, 0xBC, /* 0x10 */
	0xBD, 0xBE, 0xBF, 0xCB,  0xCD, 0xCE, 0xCF, 0xD3,
	0xD6, 0xD7, 0xD9, 0xDA,  0xDB, 0xDC, 0xDD, 0xDE, /* 0x20 */
	0xDF, 0xE5, 0xE6, 0xE7,  0xE9, 0xEA, 0xEB, 0xEC,
	0xED, 0xEE, 0xEF, 0xF2,  0xF3, 0xF4, 0xF5, 0xF6, /* 0x30 */
	0xF7, 0xF9, 0xFA, 0xFB,  0xFC, 0xFD, 0xFE, 0xFF
};

extern const uint8_t disk_byte_to_sony[] PROGMEM;
const uint8_t disk_byte_to_sony[] = {
	/* table begins at disk byte 0x96, value of 0xFF is an invalid disk byte */
	/* 0x96 */ 0x00, 0x01, 0xFF, 0xFF, 0x02, 0x03, 0xFF, 0x04,
	/* 0x9E */ 0x05, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	/* 0xA6 */ 0x07, 0x08, 0xFF, 0xFF, 0xFF, 0x09, 0x0A, 0x0B,
	/* 0xAE */ 0x0C, 0x0D, 0xFF, 0xFF, 0x0E, 0x0F, 0x10, 0x11,
	/* 0xB6 */ 0x12, 0x13, 0xFF, 0x14, 0x15, 0x16, 0x17, 0x18,
	/* 0xBE */ 0x19, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	/* 0xC6 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1B, 0xFF, 0x1C,
	/* 0xCE */ 0x1D, 0x1E, 0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0xFF,
	/* 0xD6 */ 0x20, 0x21, 0xFF, 0x22, 0x23, 0x24, 0x25, 0x26,
	/* 0xDE */ 0x27, 0x28, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x29,
	/* 0xE6 */ 0x2A, 0x2B, 0xFF, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
	/* 0xEE */ 0x31, 0x32, 0xFF, 0xFF, 0x33, 0x34, 0x35, 0x36,
	/* 0xF6 */ 0x37, 0x38, 0xFF, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
	/* 0xFE */ 0x3E, 0x3F
};	
	
uint8_t sectorDataHeaderGCR[] = { 0xD5, 0xAA, 0xAD };

extern const uint16_t crc_ccitt[] PROGMEM;
const uint16_t crc_ccitt[] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

void ResetDiskState();

uint16_t writeErrorNumber;

void error(const char* msg)
{
	bool wasWriteError = writeError;
	uint16_t wasWriteErrorNumber = writeErrorNumber;
	
	ResetDiskState(); // clears writeError and writeErrorNumber

	LcdClear();
	LcdGoto(0,0);
	if (wasWriteError)
		LcdTinyStringP(PSTR("WRITE ERROR          "), TEXT_INVERSE);		
	else
		LcdTinyStringP(PSTR("FATAL ERROR          "), TEXT_INVERSE);
	LcdGoto(0,1);
	LcdTinyString(msg, TEXT_NORMAL);
	
	if (wasWriteError)
	{		
		snprintf(textBuf, TEXTBUF_SIZE, "%u", wasWriteErrorNumber);
		LcdGoto(0,5);
		LcdTinyString(textBuf, TEXT_NORMAL);
	}
					
	while (1);	
}	
		
void InitPorts()
{
	// set all data lines as outputs, MSB (RD_ACK/WR_TICK) as input
	DDR(CPLD_DATA_PORT) = 0x7F;
	
	// initialize the other output lines
	DDR(CPLD_TACH_PORT) |= (1<<CPLD_TACH_PIN);
	DDR(CPLD_RESET_PORT) |= (1<<CPLD_RESET_PIN);
	DDR(CPLD_STEP_ACK_DISK_IN_PORT) |= (1<<CPLD_STEP_ACK_DISK_IN_PIN);
	DDR(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);
	DDR(CPLD_DATA_HIZ_PORT) |= (1<<CPLD_DATA_HIZ_PIN);
	DDR(CPLD_TMS_PORT) |= (1<<CPLD_TMS_PIN);
	DDR(STATUS_LED_PORT) |= (1<<STATUS_LED_PIN);
	
	// enable the pull-up resistor for the user buttons and SD card write protect
	PORT(SELECT_BUTTON_PORT) |= (1<<SELECT_BUTTON_PIN);
	PORT(PREV_BUTTON_PORT) |= (1<<PREV_BUTTON_PIN);
	PORT(NEXT_BUTTON_PORT) |= (1<<NEXT_BUTTON_PIN);
	PORT(CARD_WPROT_PORT) |= (1<<CARD_WPROT_PIN);
	
	// enable the pull-up resistors for the CPLD, for when we're midway through a build and it's not there yet
	PORT(CPLD_EJECT_REQ_PORT) |= (1<<CPLD_EJECT_REQ_PIN);
	PORT(CPLD_RD_ACK_WR_TICK_PORT) |= (1<<CPLD_RD_ACK_WR_TICK_PIN);
	PORT(CPLD_STEP_REQ_PORT) |= (1<<CPLD_STEP_REQ_PIN);
	PORT(CPLD_WR_REQ_PORT) |= (1<<CPLD_WR_REQ_PIN);
	PORT(CPLD_CURRENT_SIDE_PORT) |= (1<<CPLD_CURRENT_SIDE_PIN);
							
	// set initial output values
	PORT(CPLD_RESET_PORT) |= (1<<CPLD_RESET_PIN);
	PORT(CPLD_STEP_ACK_DISK_IN_PORT) |= (1<<CPLD_STEP_ACK_DISK_IN_PIN);
	PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);
	PORT(CPLD_DATA_HIZ_PORT) &= ~(1<<CPLD_DATA_HIZ_PIN);
	PORT(CPLD_TMS_PORT) |= (1<<CPLD_TMS_PIN); // TMS=1, stay in JTAG reset
	PORT(STATUS_LED_PORT) |= (1<<STATUS_LED_PIN);
	
	// set the pin change interrupt masks
	CPLD_STEP_REQ_INT_MSK |= (1<<CPLD_STEP_REQ_INT_PIN);
	CPLD_CURRENT_SIDE_INT_MSK |= (1<<CPLD_CURRENT_SIDE_INT_PIN);
	CPLD_WR_REQ_INT_MSK |= (1<<CPLD_WR_REQ_INT_PIN);
	//CPLD_RD_ACK_WR_TICK_INT_MSK |= (1<<CPLD_RD_ACK_WR_TICK_INT_PIN);
}

void SetTach()
{	
	/*
	Produces 60 pulses for each rotation of the drive motor
	Data from Apple/Sony docs:
	   Tracks	RPM   Acceptable Speed Range in ROM
	   00-15:   394   1135-11E9
	   16-31:   429   12C6-138A
	   32-47:   472   14A7-157F
	   48-63:   525   16F2-17E2
	   64-79:   590   19D0-1ADE
			
	Experimentally determined TACH toggle rates for Plus Too, running at 8.125 MHz:
		TACH Half Period Clocks		Resulting Timing Value Computed By Mac
					9996				    $117B (4475)           
					9122  				    $1328 (4904)
					8292  				    $1513 (5395)
					7463  				    $176A (5994)
					6634				    $1A56 (6742)
		
	A real Mac runs slightly slower than Plus Too (7.8336 MHz vs 8.125 MHz), so those
	timing values should be scaled by multiplying by 7.8336/8.125 = 0.96414
	
	Scaling these values and converting to real time units, we get:
	   Tracks	TACH Half Period    Implied RPM   
	   00-15:      1276.04 us         391.84
	   16-31:      1164.47 us         429.38
	   32-47:      1058.52 us         472.36
	   48-63:       952.69 us         524.83
	   64-79:       846.86 us         590.41

	*/
	
	const uint16_t zoneRPM[] = { 394, 429, 472, 525, 590 };	
	uint8_t speedZone = currentTrack >> 4;	
	if (speedZone > 4)
		speedZone = 4;
				
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		driveTachHalfPeriod = F_CPU / (2 * zoneRPM[speedZone]);		
		// OCR1A update will be performed during the next sector 0 read			
	}	
	
	TIFR1 = (1 << OCF1A); // Clear the timer 1 compare A match flag. Not sure this is actually necessary.
}

uint16_t trackStart(uint8_t trackNumber)
{
	return mfmMode ? trackNumber * 18 : pgm_read_word(&sony_track_start[trackNumber]);
}

uint8_t trackLength(uint8_t trackNumber)
{
	return mfmMode ? 18 : pgm_read_byte(&sony_track_len[trackNumber]);
}
	
#define BUFFER_DIRTY 1
#define BUFFER_DATA_VALID 2
#define BUFFER_LOCKED 4

volatile uint8_t bufferState[NUM_BUFFERS];
volatile uint8_t wrTrack;
volatile uint8_t wrSide;
volatile uint8_t wrSector;

// these variables are used only within the interrupt routine, and do not need to be declared volatile
uint8_t wrTick;
uint16_t writeCount;
uint8_t writeTemp;
uint8_t currentWriteBufferNumber;
uint8_t ck5, ck6, ck7;
uint8_t XBit;
uint8_t* pSectorBuf;
	
void WriteError()
{
	writeMode = false;
	restartDisk = true;
	writeError = true;
	writeCount = 0;
}

// pin state change interrupt: STEP
ISR(PCINT3_vect) 
{ 		
	// step to a new track?
	if (bit_is_set(PIN(CPLD_STEP_REQ_PORT), CPLD_STEP_REQ_PIN))
	{	
		// determine the range of dirty sector buffers
		uint8_t trackLen = trackLength(currentTrack);
		uint8_t firstDirtyBuffer = NUM_BUFFERS, lastDirtyBuffer=0;
	
		for (uint8_t i=0; i<trackLen*2 && i<NUM_BUFFERS; i++)
		{
			if (bufferState[i] & BUFFER_DIRTY)
			{
				if (firstDirtyBuffer == NUM_BUFFERS)
					firstDirtyBuffer = i;
				
				lastDirtyBuffer = i;
			}
		}
	
		// Lock all the buffers in the dirty range, preventing writes to them. This prevents a problem:
		// A write for the new track could fill a buffer before the old track's data was completely flushed back to SD, 
		// causing some of track N+1's data to get written to track N.
		// Also mark all buffers in the dirty range as dirty if they have valid data, even if they weren't actually 
		// dirty. The dirty range will have to be written out as a contiguous block anyway. The flush method
		// can then detect when buffers in the range don't have valid data, because they won't have the dirty flag.
		for (uint8_t i=firstDirtyBuffer; i<=lastDirtyBuffer; i++)					
		{
			if (bufferState[i] & BUFFER_DATA_VALID)
				bufferState[i] |= BUFFER_DIRTY;
				
			bufferState[i] |= BUFFER_LOCKED;
		}
				
		// step to the next track
		if (bit_is_set(PIN(CPLD_STEP_DIR_MOTOR_ON_PORT), CPLD_STEP_DIR_MOTOR_ON_PIN))
		{
			// bounds check at 0
			if (currentTrack != 0)
				currentTrack--;			
		}
		else
		{
			currentTrack++;
		}
		
		// assert ack and send new TK0 indicator
		if (currentTrack == 0)
			PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);
		else
			PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
		PORT(CPLD_STEP_ACK_DISK_IN_PORT) |= (1<<CPLD_STEP_ACK_DISK_IN_PIN);
				
		// wait for de-assertion of step request
		while (bit_is_set(PIN(CPLD_STEP_REQ_PORT), CPLD_STEP_REQ_PIN))
		{
		}
			
		// de-assert ack and RD_READY
		PORT(CPLD_STEP_ACK_DISK_IN_PORT) &= ~(1<<CPLD_STEP_ACK_DISK_IN_PIN);
		PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);
		
		SetTach();
		
		// premature end of a write?
		if (writeCount >= SECTOR_DATA_SECTORNUM_START)
		{
			strncpy(textBuf, "incomplete write", TEXTBUF_SIZE);
			writeErrorNumber = 1000 + writeCount;
			WriteError();		
		}
										
		restartDisk = true;
	}
}
		
// pin state change interrupt: SIDE, WR_REQ
ISR(PCINT2_vect) 
{ 				
	// did the current side change?
	uint8_t newSide = ((PIN(CPLD_CURRENT_SIDE_PORT) >> CPLD_CURRENT_SIDE_PIN) & 0x01);
	if (newSide != currentSide)
	{
		if (numberOfDiskSides == 2)
			currentSide = newSide;
		else
			currentSide = 0;
		restartDisk = true;
		
		// premature end of a write?
		if (writeCount >= SECTOR_DATA_SECTORNUM_START)
		{
			strncpy(textBuf, "incomplete write", TEXTBUF_SIZE);
			writeErrorNumber = 2000 + writeCount;
			WriteError();		
		}		
	}		

	// did the read/write mode change? RD_RW: 0 = write, 1 = read
	uint8_t wreqBit = bit_is_clear(PIN(CPLD_WR_REQ_PORT), CPLD_WR_REQ_PIN);
	if (wreqBit != writeMode)
	{
		writeMode = wreqBit;	
		
		if (writeMode)
		{	
			// switch the DATA pins to inputs
			DDR(CPLD_DATA_PORT) = 0;
		
			// indicate that the data bus has been released
			PORT(CPLD_DATA_HIZ_PORT) |= (1<<CPLD_DATA_HIZ_PIN);
		
			// enable the WR_TICK interrupt
			CPLD_RD_ACK_WR_TICK_INT_MSK |= (1<<CPLD_RD_ACK_WR_TICK_INT_PIN);	
						
			wrTick = bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN);
			
			writeCount = 0;
		}
		else
		{
			// switch the DATA pins to outputs
			DDR(CPLD_DATA_PORT) = 0x7F;
		
			// indicate that the data bus has been reacquired
			PORT(CPLD_DATA_HIZ_PORT) &= ~(1<<CPLD_DATA_HIZ_PIN);
		
			// disable the WR_TICK interrupt
			CPLD_RD_ACK_WR_TICK_INT_MSK &= ~(1<<CPLD_RD_ACK_WR_TICK_INT_PIN);
			
			// premature end of a write?
			if (writeCount >= SECTOR_DATA_SECTORNUM_START)
			{
				strncpy(textBuf, "incomplete write", TEXTBUF_SIZE);
				writeErrorNumber = 3000 + writeCount;
				WriteError();		
			}
		}		
		
		restartDisk = true;	
	}	
}

void HandleGCRWrite()
{			
	uint8_t diskByte = 0x80 | PIN(CPLD_DATA_PORT);
			
	if (writeCount < SECTOR_DATA_ENCODED_TAGS_START)
	{
		// look for the sector header
		// the final header byte is the sector number
		if (writeCount == SECTOR_DATA_SECTORNUM_START)
		{
			uint8_t sector = pgm_read_byte(&disk_byte_to_sony[diskByte - 0x96]);
		
			if (sector >= trackLength(currentTrack))
			{
				snprintf(textBuf, TEXTBUF_SIZE, "bad sector %d for t%d", sector, currentTrack);
				writeErrorNumber = 60;
				WriteError();	
			}		
		
			uint8_t trackLen = trackLength(currentTrack);
			currentWriteBufferNumber = trackLen * currentSide + sector;	
					
			if (bufferState[currentWriteBufferNumber] & BUFFER_LOCKED)
			{
				snprintf(textBuf, TEXTBUF_SIZE, "buf locked %d/%d:%d", currentTrack, currentSide, sector);
				writeErrorNumber = 61;
				WriteError();			
			}
										
			pSectorBuf = sectorBuf[currentWriteBufferNumber];
			bufferState[currentWriteBufferNumber] |= BUFFER_LOCKED;
			bufferState[currentWriteBufferNumber] &= ~BUFFER_DATA_VALID;
			wrTrack = currentTrack;
			wrSide = currentSide;
			wrSector = sector; 
					
			// turn on the LED when receiving a sector write
			PORT(STATUS_LED_PORT) &= ~(1<<STATUS_LED_PIN);
			
			ck5 = ck6 = ck7 = 0;	
		}
		else if (diskByte != sectorDataHeaderGCR[writeCount] || readOnly)
		{
			// header doesn't match: start over
			writeCount = 0;
			return;
		}			
	}
	else
	{
		uint8_t dataIn = pgm_read_byte(&disk_byte_to_sony[diskByte - 0x96]);
		uint8_t b;
				
		// read the sector data
		if (writeCount < SECTOR_DATA_CHECKSUM_START)
		{		
			uint16_t addResult;
					
			switch (writeCount & 0x03)
			{
				case 0:
					writeTemp = dataIn << 2; // get 0 0 A7 A6 B7 B6 C7 C6, store it as A7 A6 B7 B6 C7 C6 0 0
							
					//ROL(ck7); 
					XBit = ck7 >> 7;
					ck7 = (ck7 << 1) | XBit;
					break;
							
				case 1:			
					b = (writeTemp & 0xC0) | dataIn; // A7 A6 0 0 0 0 0 0 | 0 0 A5 A4 A3 A2 A1 A0
					b ^= ck7;

					if (writeCount >= SECTOR_DATA_ENCODED_DATA_START)
						*pSectorBuf++ = b;
							
					//ADDX(ck5, b);	
					addResult = (uint16_t)ck5 + b + XBit; 
					ck5 = addResult & 0xFF;
					XBit = addResult >> 8;
					writeTemp <<= 2; // B7 B6 C7 C6 0 0 0 0 
					break;
							
				case 2:
					b = (writeTemp & 0xC0) | dataIn; // B7 B6 0 0 0 0 0 0 | 0 0 B5 B4 B3 B2 B1 B0
					b ^= ck5;
							
					if (writeCount >= SECTOR_DATA_ENCODED_DATA_START)
						*pSectorBuf++ = b; 
							
					//ADDX(ck6, b);	
					addResult = (uint16_t)ck6 + b + XBit; 
					ck6 = addResult & 0xFF;
					XBit = addResult >> 8;
					writeTemp <<= 2; // C7 C6 0 0 0 0 0 0
					break;
							
				case 3:
					b = writeTemp | dataIn; // C7 C6 0 0 0 0 0 0 | 0 0 C5 C4 C3 C2 C1 C0
					b ^= ck6;
							
					if (writeCount >= SECTOR_DATA_ENCODED_DATA_START)
						*pSectorBuf++ = b;
								
					//ADDX(ck7, b);
					addResult = (uint16_t)ck7 + b + XBit; 
					ck7 = addResult & 0xFF;
					XBit = addResult >> 8;
					break;
			}
		}
		else
		{						
			// verify the checksum
			if (writeCount == SECTOR_DATA_CHECKSUM_START)
			{
				writeTemp = dataIn; 
				writeTemp <<= 2;
			}
			else if (writeCount == SECTOR_DATA_CHECKSUM_START+1)
			{
				b = (writeTemp & 0xC0) | dataIn;
				writeTemp <<= 2;
				if (b != ck5)
				{
					strncpy(textBuf, "checksum failure 0", TEXTBUF_SIZE);
					writeErrorNumber = 62;
					WriteError();				
				}
			}
			else if (writeCount == SECTOR_DATA_CHECKSUM_START+2)
			{
				b = (writeTemp & 0xC0) | dataIn;
				writeTemp <<= 2;
				if (b != ck6)
				{
					strncpy(textBuf, "checksum failure 1", TEXTBUF_SIZE);
					writeErrorNumber = 63;
					WriteError();
				}
			}
			else if (writeCount == SECTOR_DATA_CHECKSUM_START+3)
			{
				b = writeTemp | dataIn;
				writeTemp <<= 2;
				if (b != ck7)
				{
					strncpy(textBuf, "checksum failure 2", TEXTBUF_SIZE);
					writeErrorNumber = 64;
					WriteError();
				}
						
				// success! 
				bufferState[currentWriteBufferNumber] |= BUFFER_DATA_VALID;
				bufferState[currentWriteBufferNumber] |= BUFFER_DIRTY;
				bufferState[currentWriteBufferNumber] &= ~BUFFER_LOCKED;
						
				// turn off the LED at the end of a sector write
				PORT(STATUS_LED_PORT) |= (1<<STATUS_LED_PIN);

				// prepare for the next sector write
				writeCount = 0;
				return;
			}	
			else
			{
				// how'd we get here?
				writeCount = 0;
				return;
			}									
		}
	}	
			
	writeCount++;				
}

void CheckMFMCRC(uint8_t bufferNumber)
{
	uint16_t receivedCRC = crc;
	
	crc = 0xFFFF;
	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ 0xA1]);		
	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ 0xA1]);		
	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ 0xA1]);		
	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ 0xFB]);	
	
	for (uint16_t i=0; i<SECTOR_DATA_SIZE; i++)
	{
		crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ sectorBuf[bufferNumber][i]]);	
	}			
	
	if (crc != receivedCRC)
	{
		strncpy(textBuf, "checksum fail", TEXTBUF_SIZE);
		writeErrorNumber = 70;
		WriteError();
	}	
}

// pin state change interrupt: WR_TICK
ISR(PCINT0_vect) 
{ 
	uint8_t wrTickBit = bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN);
	
	// was a new byte written?
	if (writeMode && wrTickBit != wrTick)
	{
		wrTick = wrTickBit;
		
		if (!mfmMode)
		{
			HandleGCRWrite();
		}	
		else
		{
			if (wrTickBit == 0)
			{
				// high nibble arrives first
				writeTemp = (PIN(CPLD_DATA_PORT) << 4) & 0xF0;
				return;
			}					
			else
			{
				writeTemp |= (PIN(CPLD_DATA_PORT) & 0x0F);
				
				if (writeCount == 2)
				{
					if (writeTemp == 0xFB)
					{
						writeCount++;
						
						// header received OK!
						currentWriteBufferNumber = currentSector; // assume the buffer to write was the last one read	
					
						if (bufferState[currentWriteBufferNumber] & BUFFER_LOCKED)
						{
							snprintf(textBuf, TEXTBUF_SIZE, "buf locked %d/%d:%d", currentTrack, currentSide, currentSector);
							writeErrorNumber = 71;
							WriteError();
							return;		
						}
										
						pSectorBuf = sectorBuf[currentWriteBufferNumber];
						bufferState[currentWriteBufferNumber] |= BUFFER_LOCKED;
						bufferState[currentWriteBufferNumber] &= ~BUFFER_DATA_VALID;
						wrTrack = currentTrack;
						wrSide = currentSide;
						wrSector = currentSector; // assume the buffer to write was the last one read		
						
						// turn on the LED when receiving a sector write
						PORT(STATUS_LED_PORT) &= ~(1<<STATUS_LED_PIN);
					}						
					else if (writeTemp != 0xA1)
					{
						writeCount = 0;
					}						
				}
				else if (writeCount == 0)
				{
					if (writeTemp == 0xA1)
					writeCount++;
				}
				else if (writeCount == 1)
				{
					if (writeTemp == 0xA1)
						writeCount++;
					else
						writeCount = 0;
				}						
				else if (writeCount < SECTOR_DATA_SIZE+3)
				{
					sectorBuf[currentWriteBufferNumber][writeCount-3] = writeTemp;
					writeCount++;
				} 
				else if (writeCount == SECTOR_DATA_SIZE+3)
				{
					crc = writeTemp << 8;
					
					writeCount++; 
				} 
				else if (writeCount == SECTOR_DATA_SIZE+4)
				{
					crc |= writeTemp;
					
					CheckMFMCRC(currentWriteBufferNumber);
					
					// success! 
					bufferState[currentWriteBufferNumber] |= BUFFER_DATA_VALID;
					bufferState[currentWriteBufferNumber] |= BUFFER_DIRTY;
					bufferState[currentWriteBufferNumber] &= ~BUFFER_LOCKED;
						
					// turn off the LED at the end of a sector write
					PORT(STATUS_LED_PORT) |= (1<<STATUS_LED_PIN);
					
					// prepare for the next sector write
					writeCount = 0;
				} 
			}												
		}		
	}
}
	
void SendByte(uint8_t b)
{								
	cli();
	
	if (!writeMode)
	{		
		PORT(CPLD_DATA_PORT) = b & 0x7F;	
		PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
		
		// wait for ack to go high, then low
		// TODO: find a way to avoid two busy waits here-- can it be done with just one?
		while (bit_is_clear(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN)) 
		{}
	
		// is this really needed? Probably - when sending literal data like the GCR address header
		// the AVR is probably fast enough to call SendByte twice within the space of a single bit (320 clock cycles)
		// which would then do the wrong thing without this second busy wait.
		while (bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN)) 
		{}
						
		// clear the read byte ready flag	
		PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);	
	}
		
	sei();	
}

// make a 6-bit result from the top 2 bits of three input bytes
#define nib4(__c0, __c1, __c2) \
	(((__c0 & 0xC0) >> 2) | ((__c1 & 0xC0) >> 4) | ((__c2 & 0xC0) >> 6))

// rotate left
#define rot_ck0(__ck0)						\
	do {									\
		__ck0 &= 0xFF;						\
		__ck0 = (__ck0 << 1) | (__ck0 >> 7);\
	} while(0)

// ADC __ckr, __in; __out = __in ^ __ckl
#define enc_byte(__in, __out, __ckl, __ckr)	\
	do {									\
		uint8_t __d = __in;					\
		__ckr += __d;						\
		__ckr += (__ckl & 0x100) >> 8;		\
		__ckl &= 0xFF;						\
		__out = __d ^ __ckl;				\
	} while(0)

#define SendByteAndCheckRestart(b)			\
	do {									\
		if (restartDisk)					\
			goto restart;					\
		SendByte(b);						\
	} while(0)
		
void SendMFMSync()
{
	// send A1 sync
	
	// SendByte
	// TODO: what if an interrupt has switched the data port to an input? This will turn on pull-ups
	PORT(CPLD_DATA_PORT) = 0x0A; // data in bits 3-0, sync flag in bit 4	
	PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
	// wait for ack to go high, then low
	while (bit_is_clear(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	while (bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);

	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ 0xA1]);
	
	// SendByte
	// TODO: what if an interrupt has switched the data port to an input? This will turn on pull-ups
	PORT(CPLD_DATA_PORT) = 0x11; // data in bits 3-0, sync flag in bit 4	
	PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
	// wait for ack to go high, then low
	while (bit_is_clear(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	while (bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);		
}	
	
void SendMFMByte(uint8_t data)
{	
	// send X 0 0 0 D7 D6 D5 D4
	uint8_t out = (data >> 4) & 0x0F;
	
	//if (restartDisk)
	//	return;
		 
	// SendByte
	// TODO: what if an interrupt has switched the data port to an input? This will turn on pull-ups
	PORT(CPLD_DATA_PORT) = out;	
	PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
	// wait for ack to go high, then low
	while (bit_is_clear(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	while (bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);

	// send X 0 0 0 D3 D2 D1 d0
	out = (data & 0x0F);
		
	crc = (crc << 8) ^ pgm_read_word(&crc_ccitt[(uint8_t)(crc >> 8) ^ data]);		
	
	//if (restartDisk)
	//	return;
			
	// SendByte
	// TODO: what if an interrupt has switched the data port to an input? This will turn on pull-ups
	PORT(CPLD_DATA_PORT) = out;	
	PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);		
	// wait for ack to go high, then low
	while (bit_is_clear(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	while (bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN));
	PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);
}
	
#define SendMFMAndCheckRestart(d)			\
	do {									\
		if (restartDisk)					\
			goto restart;					\
		SendMFMByte(d);						\
	} while(0)
			
void SendGCRSectorData(const uint8_t* data)
{
	uint16_t ck0, ck1, ck2;
	uint8_t b0, b1, b2;
	uint8_t i;
	static const uint8_t tags[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const uint8_t* p = tags;

	ck0 = ck1 = ck2 = 0;

	// Do 12 bytes of tags plus 510 bytes of data
	for (i = 0; i < 174; i++) 
	{
		if (i == 4)
			p = data;
	
		rot_ck0(ck0); // ROL byte by 1 bit
		enc_byte(*(p++), b0, ck0, ck2);
		enc_byte(*(p++), b1, ck2, ck1);
		enc_byte(*(p++), b2, ck1, ck0);
		SendByte(pgm_read_byte(&sony_to_disk_byte[nib4(b0, b1, b2)]));
		SendByte(pgm_read_byte(&sony_to_disk_byte[b0 & 0x3F]));
		
		// was there a disk restart?
		if (restartDisk)
			return;
			
		SendByte(pgm_read_byte(&sony_to_disk_byte[b1 & 0x3F]));	
		SendByte(pgm_read_byte(&sony_to_disk_byte[b2 & 0x3F]));	
	}

	// Then do remaining 2 bytes of data
	rot_ck0(ck0);
	enc_byte(*(p++), b0, ck0, ck2);
	enc_byte(*(p++), b1, ck2, ck1);

	SendByte(pgm_read_byte(&sony_to_disk_byte[nib4(b0, b1, 0)]));
	SendByte(pgm_read_byte(&sony_to_disk_byte[b0 & 0x3F]));
	SendByte(pgm_read_byte(&sony_to_disk_byte[b1 & 0x3F]));

	// And write out checksum
	SendByte(pgm_read_byte(&sony_to_disk_byte[nib4(ck2, ck1, ck0)]));
	SendByte(pgm_read_byte(&sony_to_disk_byte[ck2 & 0x3F]));		
	SendByte(pgm_read_byte(&sony_to_disk_byte[ck1 & 0x3F]));		
	SendByte(pgm_read_byte(&sony_to_disk_byte[ck0 & 0x3F]));
}

SdBaseFile f;
unsigned int sectorOffset;
unsigned char GetNextFirmwareByte()
{			
	if (sectorOffset == SECTOR_DATA_SIZE)
	{
		// read the next sector of the XSVF file
		if (f.read(&sectorBuf[0], SECTOR_DATA_SIZE) < 0)
		{
			error("SD read error R");
		}
		sectorOffset = 0;
		
		PORT(STATUS_LED_PORT) ^= (1<<STATUS_LED_PIN);
	}

	unsigned char result = sectorBuf[0][sectorOffset];
	
	sectorOffset++;
		
	return result;
}

void UpdateFirmware()
{
	LcdClear();
	LcdGoto(0,0);
	LcdTinyStringP(PSTR("Updating firmware..."), TEXT_NORMAL);	
	
	// wait for the NEXT button to be released
	while (bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN))
	{}
	
	_delay_ms(400);
			
	// open the XSVF file on the SD card			
	if (!f.open("firmware.xvf", O_RDONLY)) 
	{
		LcdGoto(0,2);
		LcdTinyStringP(PSTR("Could not open"), TEXT_NORMAL);
		LcdGoto(0,3);
		LcdTinyStringP(PSTR("firmware.xvf"), TEXT_NORMAL);
	}	
	else
	{	
		// read the first sector of the XSVF file
		if (f.read(&sectorBuf[0], SECTOR_DATA_SIZE) < 0)
		{
			error("SD read error R");
		}
		sectorOffset = 0;
		
		setReadCallback(GetNextFirmwareByte);
		
		// disable interrupts and execute the XSVF
		int result;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			result = xsvfExecute();
		}
		
		f.close();
		
		LcdGoto(0,2);
		
		if (result == 0)
			strncpy(textBuf, "Result: success", TEXTBUF_SIZE);
		else
			snprintf(textBuf, TEXTBUF_SIZE, "Result: error %d", result);
			
		LcdTinyString(textBuf, TEXT_NORMAL);	
	}
		
	// display the firmware update result until a button is pressed
	LcdGoto(0,5);
	LcdTinyStringP(PSTR("Press any button"), TEXT_NORMAL);
	
	while (bit_is_set(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) && 
		   bit_is_set(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) && 
		   bit_is_set(PIN(SELECT_BUTTON_PORT),SELECT_BUTTON_PIN))
	{}
		
	LcdClear();
}

void ShowVersion()
{	
	LcdGoto(0,0);
	LcdTinyStringP(PSTR("     FLOPPY EMU      "), TEXT_INVERSE);
	LcdGoto(2*(21-strlen_P(versionStr)),2);
	LcdTinyStringP(versionStr, TEXT_NORMAL);
	snprintf(textBuf, TEXTBUF_SIZE, "CPLD Firmware %d", cpldFirmwareVersion);
	LcdGoto(2*(21-strlen(textBuf)),3);
	LcdTinyString(textBuf, TEXT_NORMAL);
	
	// LED off
	PORT(STATUS_LED_PORT) &= ~(1<<STATUS_LED_PIN);
	
	millitimerOn();
							
	for (uint8_t i=0; i<5; i++)
	{
		_delay_ms(800);
		// toggle LED
		PORT(STATUS_LED_PORT) ^= (1<<STATUS_LED_PIN);
	}
	
	millitimerOff();
}

void AdjustContrast(void)
{
	LcdGoto(0,0);
	LcdClear();
	LcdGoto(0,0);
	LcdTinyStringP(PSTR("CONTRAST ADJUSTMENT"), TEXT_NORMAL);
	LcdGoto(0,2);
	LcdTinyStringP(PSTR("Release buttons to"), TEXT_NORMAL);
	LcdGoto(0,3);
	LcdTinyStringP(PSTR("begin"), TEXT_NORMAL);
		
	// wait for the buttons to be released
	while (bit_is_clear(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) || 
			bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) ||
			bit_is_clear(PIN(SELECT_BUTTON_PORT), SELECT_BUTTON_PIN))
	{}
	
	LcdGoto(0,0);
	LcdClear();
		
	LcdGoto(0,2);
	LcdTinyStringP(PSTR("PREV: Lighter"), TEXT_NORMAL);
	LcdGoto(0,3);
	LcdTinyStringP(PSTR("NEXT: Darker"), TEXT_NORMAL);
	LcdGoto(0,4);
	LcdTinyStringP(PSTR("SELECT: Save"), TEXT_NORMAL);
		
	while (true)
	{
		LcdGoto(0,0);
		LcdTinyStringP(PSTR("Contrast: "), TEXT_NORMAL);
		char contrastStr[4];
		snprintf(contrastStr, 4, "%d", lcd_vop);
		LcdTinyString(contrastStr, TEXT_NORMAL);
			
		_delay_ms(400);
	
		// wait for a button press
		while (bit_is_set(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) && 
				bit_is_set(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) && 
				bit_is_set(PIN(SELECT_BUTTON_PORT),SELECT_BUTTON_PIN))
		{}
			
		if (bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN))
		{				
			if (lcd_vop < 255)
				lcd_vop++;
		}
		else if (bit_is_clear(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN))
		{				
			if (lcd_vop > 128)
				lcd_vop--;
		}
		else if (bit_is_clear(PIN(SELECT_BUTTON_PORT), SELECT_BUTTON_PIN))
		{				
			eeprom_update_byte((uint8_t*)1, lcd_vop);
			break;
		}
		
		LcdWrite(LCD_CMD, 0x21); // LCD Extended Commands.
		LcdWrite(LCD_CMD, lcd_vop); // Set LCD Vop (Contrast). 
		LcdWrite(LCD_CMD, 0x20);
	}	
}

void PromptForFirmwareUpdate()
{
	LcdGoto(0,0);
	LcdClear();
	LcdGoto(0,0);
	LcdTinyStringP(PSTR("CPLD FIRMWARE UPDATE"), TEXT_NORMAL);
	LcdGoto(0,2);
	LcdTinyStringP(PSTR("Release buttons to"), TEXT_NORMAL);
	LcdGoto(0,3);
	LcdTinyStringP(PSTR("begin"), TEXT_NORMAL);
		
	// wait for the buttons to be released
	while (bit_is_clear(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) || 
			bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) ||
			bit_is_clear(PIN(SELECT_BUTTON_PORT), SELECT_BUTTON_PIN))
	{}
		
	LcdGoto(0,2);
	LcdTinyStringP(PSTR("NEXT: Load firmware"), TEXT_NORMAL);
	LcdGoto(0,3);
	LcdTinyStringP(PSTR("PREV: Cancel"), TEXT_NORMAL);
	_delay_ms(400);
		
	// wait for a button press
	while (bit_is_set(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) && 
			bit_is_set(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) && 
			bit_is_set(PIN(SELECT_BUTTON_PORT),SELECT_BUTTON_PIN))
	{}
			
	if (bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN))
	{				
		UpdateFirmware();
	}
	else
	{
		LcdClear();
	}
}

uint32_t imageFirstBlock, imageLastBlock;
	
bool OpenImageFile()
{	
	LcdClear();
	LcdGoto(0,0);
	LcdTinyString(selectedLongFile, TEXT_NORMAL);
	
	LcdGoto(0,1);
	
	// open the disk image file
	// to-do: check if the file is read-only on the card	
	bool openOK = true;
	if (!f.open(selectedFile, O_RDWR)) 
	{
		if (f.open(selectedFile, O_RDONLY))
		{
			// TODO: How do we tell the CPLD the disk is read-only?
			readOnly = true;
		}
		else			
		{	
			LcdTinyStringP(PSTR("error opening image"), TEXT_NORMAL);
			openOK = false;
		}			
	}	
	else
	{	
		if (selectedFileType == DISK_IMAGE_400K || selectedFileType == DISK_IMAGE_DISKCOPY_400K)	
		{
			numberOfDiskSides = 1;
		}
		else 	
		{
			numberOfDiskSides = 2;
		}	
	}	
		
	// get address of file on SD
	if (openOK && !f.contiguousRange(&imageFirstBlock, &imageLastBlock)) 
	{
		LcdTinyStringP(PSTR("image not contiguous"), TEXT_NORMAL);
		openOK = false;
	}
	
	if (!openOK)
	{
		_delay_ms(4000); // wait 4 seconds
	}
	else
	{
		LcdGoto(0,1);
		// show disk image type
		switch (selectedFileType)
		{
			case DISK_IMAGE_400K:
				LcdTinyStringP(PSTR("400K raw image"), TEXT_NORMAL);
				break;
				
			case DISK_IMAGE_800K:
				LcdTinyStringP(PSTR("800K raw image"), TEXT_NORMAL);
				break;	

			case DISK_IMAGE_1440K:
				LcdTinyStringP(PSTR("1440K raw image"), TEXT_NORMAL);
				mfmMode = true;
				break;
					
			case DISK_IMAGE_DISKCOPY_400K:
				LcdTinyStringP(PSTR("400K DiskCopy image"), TEXT_NORMAL);
				break;
				
			case DISK_IMAGE_DISKCOPY_800K:
				LcdTinyStringP(PSTR("800K DiskCopy image"), TEXT_NORMAL);
				break;	

			case DISK_IMAGE_DISKCOPY_1440K:
				LcdTinyStringP(PSTR("1440K DiskCopy image"), TEXT_NORMAL);
				mfmMode = true;
				break;	
											
			default:
				break;	
		}
		
		selectedFileIsDiskCopyFormat = (selectedFileType >= DISK_IMAGE_DISKCOPY_400K);
	
		if (bit_is_set(PIN(CARD_WPROT_PORT), CARD_WPROT_PIN))
			readOnly = true;

		// mount DiskCopy images read-only
		if (selectedFileIsDiskCopyFormat)
			readOnly = true;
											
		uint16_t volumeNameOffset = selectedFileIsDiskCopyFormat ? 0x424 + 0x54 : 0x424;
		f.seekSet(volumeNameOffset); // offset of the Macintosh disk name in the image file
		f.read(&sectorBuf[0][0], SECTOR_DATA_SIZE);
		int nameLen = sectorBuf[0][0];
		uint8_t* name = &sectorBuf[0][1];
		name[nameLen] = 0;
		name[21] = 0; // in case nameLen was bogus, terminate the string after 21 chars, which is the longest displayable name on the LCD
		LcdGoto(0,2);
		LcdTinyString((char*)name, TEXT_NORMAL);
		LcdGoto(0,4);
		LcdTinyStringP(PSTR("Track    Side"), TEXT_NORMAL);
		
		// show a lock icon if the disk image is mounted as read-only
		if (readOnly)
		{
			LcdGoto(77,0);
			LcdWrite(LCD_DATA, 0x00);
			LcdWrite(LCD_DATA, 0x78);
			LcdWrite(LCD_DATA, 0x7E);
			LcdWrite(LCD_DATA, 0x79);
			LcdWrite(LCD_DATA, 0x79);
			LcdWrite(LCD_DATA, 0x7E);
			LcdWrite(LCD_DATA, 0x78);
		}
	}
	
	f.close();
	return openOK;
}

void ResetDiskState()
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		// useless code to prevent the "unused" bootloader_info array from being optimized away when optimizations are turned on
		// there's probably a nicer way to accomplish this.
		currentTrack = bootloader_info[currentTrack];
	
		InitPorts();
	
		currentTrack = 0;
		prevTrack = 0;
		restartDisk = false;
		currentSide = 0;
		prevSide = 0;
		writeMode = 0;
		diskInserted = false;	
		numberOfDiskSides = 2; 
		currentSector = 0; 
		readOnly = false;
		mfmMode = false;
		writeError = false;
		writeDisplayTimer = 0;
		tachFlutter = 0;
		writeErrorNumber = 0;
		writeCount = 0;
		
		for (uint8_t i=0; i<NUM_BUFFERS; i++)
			bufferState[i] = 0;
		
		// Reset the CPLD, and get its 7-bit firmware version number.
		// switch the DATA pins to inputs
		DDR(CPLD_DATA_PORT) = 0;	
		// indicate that the data bus has been released
		PORT(CPLD_DATA_HIZ_PORT) |= (1<<CPLD_DATA_HIZ_PIN);
		// put the CPLD into reset
		PORT(CPLD_RESET_PORT) &= ~(1<<CPLD_RESET_PIN);
		_delay_ms(20);
		cpldFirmwareVersion = PIN(CPLD_DATA_PORT) & 0x7F;
		// indicate that the data bus has been reacquired
		PORT(CPLD_DATA_HIZ_PORT) &= ~(1<<CPLD_DATA_HIZ_PIN);	
		// switch the DATA pins to outputs
		DDR(CPLD_DATA_PORT) = 0x7F;		
		// exit the CPLD from reset
		PORT(CPLD_RESET_PORT) |= (1<<CPLD_RESET_PIN);
	
		// enable power to 16-bit Timer/Counter 1
		PRR0 &= ~(1<<PRTIM1); 
		// toggle OC1A when a counter compare match with OCR1A occurs. 
		// use Fast-PWM OCRnA mode - this provides double-buffering of OCR1A.
		// no clock prescale: increment counter every on clock cycle.		
		TCCR1A = (1<<COM1A0) | (1<<WGM11) | (1<<WGM10); 
		TCCR1B = (1<<WGM13) | (1<<WGM12) | (1<<CS10); 
		SetTach();
	
		// initialize the display
		LcdReset();
		LcdClear();	
	}
}

uint8_t NextInterleavedSector(uint8_t trackNumber, uint8_t prevSectorNumber)
{
	uint8_t trackLen = trackLength(trackNumber);
			
	if (mfmMode)
	{
		// 1:1 interleave
		if (prevSectorNumber >= trackLen - 1)
			return 0;
		else
			return prevSectorNumber + 1;
	}
	else
	{
		uint8_t halfTrackLen = (trackLen + 1) >> 1;
					
		// process sectors in interleaved order: 
		// 12 sector tracks: 0 6 1 7 2 8 3 9 4 10 5 11 
		// 11 sector tracks: 0 6 1 7 2 8 3 9 4 10 5
		// 10 sector tracks: 0 5 1 6 2 7 3 8 4 9
		//  9 sector tracks: 0 5 1 6 2 7 3 8 4
		//  8 sector tracks: 0 4 1 5 2 6 3 7
		//       This is how real floppies are formatted, and should improve read performance if the Mac
		//       can't completely process sector N before sector N+1 begins. It should also improve sector-by-sector
		//       write performance, because the Mac alternately reads (address header) and writes (data section) in
		//       this mode, and proper interleaving means it will read the desired address header sooner if the Mac
		//       isn't fast enough to process the sectors linearly (which it likely isn't).
		if (prevSectorNumber > trackLen - 1 ||
			((trackLen & 1) == 0 && prevSectorNumber == trackLen - 1) || 
			((trackLen & 1) == 1 && prevSectorNumber == halfTrackLen - 1))
			return 0;
		else if (prevSectorNumber < halfTrackLen)
			return prevSectorNumber + halfTrackLen;
		else
			return prevSectorNumber + 1 - halfTrackLen;	
	}			
}

void ReadDiskCopy42Block(SdFat& sd, uint32_t blockToRead, uint8_t bufferNumber)
{
	// for a DiskCopy 4.2 image, read two blocks into a temp buffer, then copy the unaligned data into the sector buffer.
	uint16_t i;
								
	if (!sd.card()->readStart(blockToRead))
		error("SD read start error");
								
	// read part 1
	if (!sd.card()->readData(extraBuf))
		error("SD read error D");
	for (i=0; i<512-0x54; i++)
		sectorBuf[bufferNumber][i] = extraBuf[0x54 + i];
									
	// read part 2	
	if (!sd.card()->readData(extraBuf))
		error("SD read error D");
	for (i=512-0x54; i<512; i++)
		sectorBuf[bufferNumber][i] = extraBuf[0x54 + i - 512];
		
	sd.card()->readStop();
}

void FlushDirtySectors(SdFat& sd, uint8_t trackNumber)
{					
	uint8_t trackLen = trackLength(trackNumber);
	uint8_t firstDirtyBuffer = NUM_BUFFERS, lastDirtyBuffer=0;
	
	// determine the dirty range
	for (uint8_t i=0; i<trackLen*2 && i<NUM_BUFFERS; i++)
	{
		if (bufferState[i] & BUFFER_DIRTY)
		{
			if (wrTrack != trackNumber)
			{
				snprintf(textBuf, TEXTBUF_SIZE, "wr wrong track %d/%d", trackNumber, wrTrack);
				error(textBuf);
			}					
			
			if (firstDirtyBuffer == NUM_BUFFERS)
				firstDirtyBuffer = i;
				
			lastDirtyBuffer = i;
		}
	}
											
	if (firstDirtyBuffer != NUM_BUFFERS) 
	{						
		if (readOnly)
		{
			for (uint8_t i=firstDirtyBuffer; i<=lastDirtyBuffer; i++)
			{					
				bufferState[i] &= ~BUFFER_DIRTY;
				bufferState[i] &= ~BUFFER_LOCKED;
			}
			
			snprintf(textBuf, TEXTBUF_SIZE, "Reverted trk %02d   ", trackNumber);
			LcdGoto(0,5);
			LcdTinyString(textBuf, TEXT_NORMAL);
		}
		else
		{
			millitimerOn();
			uint32_t t0 = millis();	
				
			// Are there any buffers in the dirty range that aren't actually dirty?
			// If so, they must be read back from the card, so that the whole range can be written back
			// as a single block.
			// (Alternatively, the range can be written out as several non-contiguous blocks, skipping 
			// the non-dirty buffers, but I think that would be slower.)
			for (uint8_t i=firstDirtyBuffer; i<=lastDirtyBuffer; i++)					
			{	
				if (!(bufferState[i] & BUFFER_DIRTY))
				{	
					uint32_t blockToRead = imageFirstBlock + ((uint32_t)trackStart(trackNumber) * numberOfDiskSides + i);
					
					if (mfmMode)
						blockToRead += trackLen * wrSide;
				
					if (!sd.card()->readBlock(blockToRead, sectorBuf[i]))
						error("SD read error W");
				}									
			}
									
			uint32_t firstBlockToWrite = imageFirstBlock + ((uint32_t)trackStart(trackNumber) * numberOfDiskSides + firstDirtyBuffer);
			
			if (mfmMode)
				firstBlockToWrite += trackLen * wrSide;
				
			uint32_t numBuffersToWrite = lastDirtyBuffer + 1 - firstDirtyBuffer;					
						
			if (!sd.card()->writeStart(firstBlockToWrite, numBuffersToWrite))
				error("SD writeStart fail");
			
			for (uint8_t i=firstDirtyBuffer; i<=lastDirtyBuffer; i++)
			{				
				if (!sd.card()->writeData(sectorBuf[i]))
					error("SD write error");
					
				bufferState[i] &= ~BUFFER_DIRTY;
				bufferState[i] &= ~BUFFER_LOCKED;
			}
													
			if (!sd.card()->writeStop())
				error("SD writeStop fail");
						
			writeDisplayTimer = 25;	
			millitimerOff();
						
			uint32_t writeTime = millis() - t0;
					
			snprintf(textBuf, TEXTBUF_SIZE, "Saved trk %02d in %lu  ", trackNumber, writeTime);							
			LcdGoto(0,5);
			LcdTinyString(textBuf, TEXT_NORMAL);
		}		
	}	
}

int main(void)
{	
	millitimerInit();
	ResetDiskState();
	
	ShowVersion();
	
	LcdClear();
						  
	sei();
	_delay_ms(100); // wait for pending interrupts??
	
	millitimerOn();
	_delay_ms(100); // wait for pending interrupts??
	
	// if select and next are both held down, enter contrast adjust mode
	if (bit_is_set(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) && 
		bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) && 
		bit_is_clear(PIN(SELECT_BUTTON_PORT),SELECT_BUTTON_PIN))
	{				
		AdjustContrast();
	}
	
	SdFat sd;
	if (!sd.init(SPI_FULL_SPEED))
	{
		snprintf(textBuf, TEXTBUF_SIZE, "SD card error %d:%d", sd.card()->errorCode(), sd.card()->errorData());
		error(textBuf);
	}
		
	millitimerOff();
	
	// if prev and next are both held down, enter firmware update mode
	if (bit_is_clear(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN) && 
		bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN) && 
		bit_is_set(PIN(SELECT_BUTTON_PORT),SELECT_BUTTON_PIN))
	{				
		PromptForFirmwareUpdate();
	}					
	
	InitDiskMenu(sd);
	DrawDiskMenu(sd);
	
	// main loop				
	while (true)
	{	
		if (writeError)
		{
			// report error encounted in the interrupt routine
			error(textBuf);
		}	
		
		cli();
		uint8_t trackNumber = currentTrack;	// save track in a local var, since currentTrack is volatile
		uint8_t sideNumber = currentSide; // save side in a local var, since currentSide is volatile
		restartDisk = false;
		sei();
								
		if (diskInserted)
		{											
			// show the current track and side
			snprintf(textBuf, TEXTBUF_SIZE, "%02d", trackNumber);
			LcdGoto(24,4);
			LcdTinyString(textBuf, TEXT_NORMAL);
			snprintf(textBuf, TEXTBUF_SIZE, "%d ", sideNumber);
			LcdGoto(56,4);
			LcdTinyString(textBuf, TEXT_NORMAL);
									 
			// sync RAM buffer with SD card when switching tracks, or also when switching sides for mfmMode
			if (prevTrack != trackNumber || (mfmMode && (prevSide != sideNumber)))
			{		
				// write any dirty sectors from the previous track/side back to the SD card	
				FlushDirtySectors(sd, prevTrack);	
				prevTrack = trackNumber;
				prevSide = sideNumber;
										
				// Also mark all the buffers on this track as invalid, since they don't contain valid data for the new track.
				for (uint8_t i=0; i<NUM_BUFFERS; i++)
					bufferState[i] &= ~BUFFER_DATA_VALID;
			}
			// continuously replay sectors from this track/side until interrupted 		
			while (!restartDisk)
			{																															
				if (writeMode)
				{
					// do nothing: incoming data is processed by the interrupt handler
					
					// show write state
					LcdGoto(64,4);
					LcdTinyStringP(PSTR("Write"), TEXT_NORMAL);
					
					while (!restartDisk)
					{}					
				}
				else if (trackNumber <= 79)
				{								
					uint8_t trackLen = trackLength(trackNumber);
					
					// when stepping from a track with more sectors to one with fewer, current sector number could potentially
					// end up out of range
					if (currentSector >= trackLen)
						currentSector = 0;
						
					bool prevMotorOn = !bit_is_clear(PIN(CPLD_STEP_DIR_MOTOR_ON_PORT), CPLD_STEP_DIR_MOTOR_ON_PIN);
					
					while (true)
					{									
						// check for disk eject
						if (bit_is_set(PIN(CPLD_EJECT_REQ_PORT), CPLD_EJECT_REQ_PIN))
						{
							PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);
							PORT(CPLD_STEP_ACK_DISK_IN_PORT) |= (1<<CPLD_STEP_ACK_DISK_IN_PIN);
							diskInserted = false;
							
							// write any dirty sectors from the current track
							FlushDirtySectors(sd, trackNumber);
					
							_delay_ms(100);
							ResetDiskState();
							InitDiskMenu(sd);
							DrawDiskMenu(sd);
					
							goto restart;
						}		
	
						// show read/idle state
						bool motorOn = bit_is_clear(PIN(CPLD_STEP_DIR_MOTOR_ON_PORT), CPLD_STEP_DIR_MOTOR_ON_PIN);
						
						if (!motorOn)
						{
							// write any dirty sectors from the current track, when idle
							FlushDirtySectors(sd, trackNumber);
						}
						
						if (prevMotorOn != motorOn)
						{
							prevMotorOn = motorOn;
							LcdGoto(64,4);
							if (motorOn)
								LcdTinyStringP(PSTR(" Read"), TEXT_NORMAL);
							else
							{
								LcdTinyStringP(PSTR(" Idle"), TEXT_NORMAL);
								// turn LED off when idle
								PORT(STATUS_LED_PORT) |= (1<<STATUS_LED_PIN);
								
								writeDisplayTimer = 1; // clear old write alerts when going idle
							}								
						}
							
						// remove old write alerts when writeDisplayTimer reaches 1
						if (writeDisplayTimer == 1)
						{
							writeDisplayTimer = 0;
							LcdGoto(0,5);
							LcdTinyStringP(PSTR("                     "), TEXT_NORMAL);							
						}			
													
						uint8_t bufferNumber = mfmMode ? currentSector : (sideNumber * trackLen + currentSector);
						
						bool shouldReadSector = false;			
						
						// atomic check and acquire of buffer lock
						cli();	
						if ((bufferState[bufferNumber] & BUFFER_DATA_VALID) == 0 &&
							(bufferState[bufferNumber] & BUFFER_LOCKED) == 0)
						{
							// lock the buffer, so the Mac won't write to it while we're reading it from SD
							bufferState[bufferNumber] |= BUFFER_LOCKED;	
							shouldReadSector = true;
						}			
						sei();
						
						// read the sector from the SD card, if necessary
						if (shouldReadSector)
						{		
							uint32_t blockToRead = imageFirstBlock + ((uint32_t)trackStart(trackNumber) * numberOfDiskSides + sideNumber * trackLen + currentSector);
								
							millitimerOn();
											
							if (selectedFileIsDiskCopyFormat)
							{
								ReadDiskCopy42Block(sd, blockToRead, bufferNumber);
							}	
							else
							{
								if (!sd.card()->readBlock(blockToRead, sectorBuf[bufferNumber]))
									error("SD read error R");
							}
							
							millitimerOff();
															
							bufferState[bufferNumber] |= BUFFER_DATA_VALID;
							bufferState[bufferNumber] &= ~BUFFER_LOCKED;	
						}				
						
						if (currentSector == 0)
						{
							if (motorOn)
							{
								// toggle LED during drive activity
								PORT(STATUS_LED_PORT) ^= (1<<STATUS_LED_PIN);	
							
								if (writeDisplayTimer > 1)
									writeDisplayTimer--;
							}
															
							// "Flutter" the drive's TACH speed slightly, every time we pass sector 0 (about every 100-150ms). This avoids a bug
							// in P_Sony_MakeSpdTbl in the 64K ROM (used in the Mac 128K and Mac 512K) where 
							// the Mac will crash if two successive TACH measurements see the exact same speed.
							tachFlutter += 25;
							if (tachFlutter >= 125)
								tachFlutter = 0;		
														
							// Set the timeout. OC1A will toggle after this many counts. New timeout threshold won't take effect until the next timeout.	
							OCR1A = driveTachHalfPeriod - tachFlutter; 
						}	
																			
						if (mfmMode)
						{																							
							// insert sector-to-sector gap bytes
							for (uint8_t i=0; i<50; i++)
							{
								SendMFMAndCheckRestart(0x4E);
							}
							
							// insert sync bytes
							for (uint8_t i=0; i<12; i++)
							{
								SendMFMAndCheckRestart(0x00);
							}
							
							// send the address block
							crc = 0xFFFF; // reset CRC							
							SendMFMSync();
							SendMFMSync();
							SendMFMSync();
							SendMFMAndCheckRestart(0xFE);									
							SendMFMAndCheckRestart(trackNumber);
							SendMFMAndCheckRestart(sideNumber);
							SendMFMAndCheckRestart(currentSector+1); // MFM sector numbers are 1-based
							SendMFMAndCheckRestart(2); // size = 128 * 2^N bytes, so 2 means 512
							uint8_t crc0 = (crc >> 8) & 0xFF;
							uint8_t crc1 = crc & 0xFF;
							SendMFMAndCheckRestart(crc0);
							SendMFMAndCheckRestart(crc1);
							
							// insert Address to Data gap bytes
							for (uint8_t i=0; i<22; i++)
							{
								SendMFMAndCheckRestart(0x4E);
							}	
							
							// insert sync bytes
							for (uint8_t i=0; i<12; i++)
							{
								SendMFMAndCheckRestart(0x00);
							}
							
							// send the data block
							crc = 0xFFFF; // reset CRC
							SendMFMSync();
							SendMFMSync();
							SendMFMSync();
							SendMFMAndCheckRestart(0xFB);
							
							for (uint16_t i=0; i<SECTOR_DATA_SIZE; i++)
							{
								uint8_t d = sectorBuf[bufferNumber][i];
								SendMFMAndCheckRestart(d);
							}
							
							crc0 = (crc >> 8) & 0xFF;
							crc1 = crc & 0xFF;
							SendMFMAndCheckRestart(crc0);
							SendMFMAndCheckRestart(crc1);
						}	
						else
						{		
							// ensure a short gap between sectors - otherwise once they're all cached, one sector will appear
							// to immediately follow another on disk, which may cause problems for the Mac.
							// Bad voodoo here:
							// 1. In the Finder StuffIt copy test that sometimes dies after the first 18 tracks, the length of delay here
							// seems to affect what track it will freeze on.
							// 2. With a longer delay here, the first ~10 sectors of copying seem to have fewer or no "long writes".
							// 3. Depending on the delay here, the Transcend 2GB SD card sometimes gets "writeStop fail" when saving tracks.
							for (uint16_t i=0; i<INTER_SECTOR_GAP_SIZE; i++)
							{
								SendByteAndCheckRestart(0xFF);
							}
																								
							// send the address block
							uint8_t format = (numberOfDiskSides == 2) ? 0x22 : 0x02; // 0x22 = MacOS double-sided, 0x02 = single sided
							uint8_t trackLow = (uint8_t)(trackNumber & 0x3F);
							uint8_t trackHigh = (uint8_t)((sideNumber << 5) | (trackNumber >> 6));
							uint8_t checksum = (uint8_t)((trackLow ^ currentSector ^ trackHigh ^ format) & 0x3F);                  

							SendByteAndCheckRestart(0xD5);	
							SendByteAndCheckRestart(0xAA);
							SendByteAndCheckRestart(0x96);
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[trackLow]));
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[currentSector]));
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[trackHigh]));
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[format]));
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[checksum]));
							SendByteAndCheckRestart(0xDE);
							SendByteAndCheckRestart(0xAA);
						
							// insert sync bytes between the address and data blocks
							for (uint8_t i=0; i<ADDRESS_DATA_GAP_SIZE; i++)
							{
								SendByteAndCheckRestart(0xFF);
							}	
									  
							// send the data block
							SendByteAndCheckRestart(0xD5);			
							SendByteAndCheckRestart(0xAA);
							SendByteAndCheckRestart(0xAD);
							SendByteAndCheckRestart(pgm_read_byte(&sony_to_disk_byte[currentSector]));
					
							SendGCRSectorData((const uint8_t*)sectorBuf[bufferNumber]);
							
							SendByteAndCheckRestart(0xDE);
							SendByteAndCheckRestart(0xAA);
							SendByteAndCheckRestart(0xFF);			
						}		
										
						currentSector = NextInterleavedSector(trackNumber, currentSector);
					}
				}											
			}		
restart: ;					
		}
		else
		{					
			while (!restartDisk)
			{
				// check for disk eject. This shouldn't normally happen, but if the CPLD and AVR get out of sync
				// and the CPLD thinks there's a disk while the AVR doesn't, it could.
				// But don't do this is ALL the CPLD inputs are asserted - that means you're probably midway 
				// through building the hardware, and the CPLD isn't there yet.
				if (bit_is_set(PIN(CPLD_EJECT_REQ_PORT), CPLD_EJECT_REQ_PIN) &&
				    !(bit_is_set(PIN(CPLD_RD_ACK_WR_TICK_PORT), CPLD_RD_ACK_WR_TICK_PIN) &&
					  bit_is_set(PIN(CPLD_STEP_REQ_PORT), CPLD_STEP_REQ_PIN) &&
					  bit_is_set(PIN(CPLD_WR_REQ_PORT), CPLD_WR_REQ_PIN) &&
					  bit_is_set(PIN(CPLD_CURRENT_SIDE_PORT), CPLD_CURRENT_SIDE_PIN)))
				{			
					// reboot
					((void(*)(void))0)();					
 				}	
						
				if (bit_is_clear(PIN(PREV_BUTTON_PORT), PREV_BUTTON_PIN))
				{
					if (diskMenuSelection > 0)
					{
						diskMenuSelection--;
						DrawDiskMenu(sd);
						_delay_ms(200);
					}					
				}
				else if (bit_is_clear(PIN(NEXT_BUTTON_PORT), NEXT_BUTTON_PIN))
				{
					diskMenuSelection++;
					DrawDiskMenu(sd);
					_delay_ms(200);
				}
				else if (bit_is_clear(PIN(SELECT_BUTTON_PORT), SELECT_BUTTON_PIN))
				{
					if (selectedFileType == DISK_IMAGE_DIRECTORY)
					{						
						// remember where we came from, so we can get back later
						char* pSubdirName = ((char*)sectorBuf[23]) + ((SHORTFILENAME_LEN+1) * subdirDepth);
						strncpy(pSubdirName, selectedFile, SHORTFILENAME_LEN+1);
						subdirDepth++;
					
						sd.chdir(selectedFile, true);
													
						diskMenuSelection = 0;
						LcdClear();
						InitDiskMenu(sd);
						DrawDiskMenu(sd);
						_delay_ms(400);
					}
					else if (selectedFileType == DISK_IMAGE_UP_DIRECTORY)
					{
						subdirDepth--;
						sd.chdir(true); // go to root directory
						
						for (uint8_t i=0; i<subdirDepth; i++)
						{
							char* pSubdirName = ((char*)sectorBuf[23]) + ((SHORTFILENAME_LEN+1) * i);
							sd.chdir(pSubdirName, true);	
						}
						
						diskMenuSelection = 0;
						LcdClear();
						InitDiskMenu(sd);
						DrawDiskMenu(sd);
						_delay_ms(400);
					}
					else
					{			
						// enable the interrupts
						PCICR |= (1<<CPLD_STEP_REQ_INT_ENABLE);	
						PCICR |= (1<<CPLD_CURRENT_SIDE_INT_ENABLE);
						PCICR |= (1<<CPLD_WR_REQ_INT_ENABLE);
						PCICR |= (1<<CPLD_RD_ACK_WR_TICK_INT_ENABLE);
	
						currentTrack = 0;	
						LcdReset(); // also resets the CPLD, ensures it knows we're now at track 0
						LcdClear();
						
						// "insert" the disk
						if (OpenImageFile())
						{			
							// tell the CPLD whether the disk is read-only (bit 0, active low)
							uint8_t configByte = 0;
						
							if (!readOnly)
								configByte |= 0x01;

							if (!mfmMode)
								configByte |= 0x02;			
							
							PORT(CPLD_DATA_PORT) = configByte;
							// asserting RD_READY without DISK_IN causes the CPLD to load config options from the data bus
							PORT(CPLD_RD_READY_TK0_PORT) |= (1<<CPLD_RD_READY_TK0_PIN);	
							_delay_ms(1);
							PORT(CPLD_RD_READY_TK0_PORT) &= ~(1<<CPLD_RD_READY_TK0_PIN);
							_delay_us(10);
						
							// tell the CPLD there is a disk	
							PORT(CPLD_STEP_ACK_DISK_IN_PORT) &= ~(1<<CPLD_STEP_ACK_DISK_IN_PIN);			
							diskInserted = true;
						}
					}						
					break;			
				}
			}
		}	
	}			
}

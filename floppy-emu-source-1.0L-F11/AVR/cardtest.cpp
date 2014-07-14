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

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
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
#include "cardtest.h"

#ifdef PROGMEM_WORKAROUND
// work-around for compiler bug
#undef PROGMEM 
#define PROGMEM __attribute__(( section(".progmem.data") )) 
#undef PSTR 
#define PSTR(s) (__extension__({static prog_char __c[] PROGMEM = (s); &__c[0];})) 
#endif

#define STATUS_LED_PORT B
#define STATUS_LED_PIN 3

#define TEXTBUF_SIZE 22
extern char textBuf[];
extern uint8_t sectorBuf[512];

void CardTest()
{
	LcdClear();
	
	SdFat sd;
	if (!sd.init(SPI_HALF_SPEED))
	{
		snprintf(textBuf, TEXTBUF_SIZE, "SD card error %d:%d", sd.card()->errorCode(), sd.card()->errorData());
		LcdGoto(0,0);
		LcdTinyString(textBuf, TEXT_NORMAL);
		while(1);
	}
	
	cid_t cid;
	
	uint32_t cardSize = sd.card()->cardSize();
	cardSize /= (2L*1024L);
	
	sd.card()->readCID(&cid);
  
	snprintf(textBuf, TEXTBUF_SIZE, "CID %d %c%c%c%c%c %lu MB", cid.mid, cid.pnm[0], cid.pnm[1], cid.pnm[2], cid.pnm[3], cid.pnm[4], cardSize);
	LcdGoto(0,0);
	LcdTinyString(textBuf, TEXT_NORMAL);	
		
	csd_t csd;
	sd.card()->readCSD(&csd);
			
	uint8_t writeBlockPow; // write block length, log2
	uint8_t sectorSizeCnt; // minimum erasable size, in write blocks
			
	if (csd.v1.csd_ver == 1)
	{
		csd1_t* c = &csd.v1;

		writeBlockPow = 4*c->write_bl_len_high + c->write_bl_len_low;
		sectorSizeCnt = 2*c->sector_size_high + c->sector_size_low;		
	}
	else
	{
		csd2_t* c = &csd.v2;
		
		writeBlockPow = 4*c->write_bl_len_high + c->write_bl_len_low;
		sectorSizeCnt = 2*c->sector_size_high + c->sector_size_low;		
	}
	
	sectorSizeCnt += 1; // these all seem to be 2**n - 1?
	
	uint32_t writeBlock=1;
	for (uint8_t i=0; i<writeBlockPow; i++)
	{
		writeBlock *= 2;
	}
	uint32_t sectorSize = writeBlock * sectorSizeCnt;
	sectorSize /= 1024;
	
	snprintf(textBuf, TEXTBUF_SIZE, "BLK %luB ERASE %luK", writeBlock, sectorSize);
	LcdGoto(0,1);
	LcdTinyString(textBuf, TEXT_NORMAL);		
	
	LcdGoto(0,2);
	
	SdBaseFile f;
	if (!f.open("TESTFILE.DAT", O_RDWR)) 
	{
		LcdTinyStringP(PSTR("TESTFILE.DAT missing"), TEXT_NORMAL);
		while(1);		
	}	
		
	// get address of file on SD
	uint32_t imageFirstBlock, imageLastBlock;
	if (!f.contiguousRange(&imageFirstBlock, &imageLastBlock)) 
	{
		LcdTinyStringP(PSTR("file not contiguous"), TEXT_NORMAL);
		while(1);	
	}
	
	if (f.fileSize() < (unsigned long)1024 * 1024)	
	{
		LcdTinyStringP(PSTR("file too small"), TEXT_NORMAL);
		while(1);	
	}
	
	f.close();
	
	LcdTinyStringP(PSTR("Testing..."), TEXT_NORMAL);
	
	uint32_t writeCount = 0;
	uint32_t writeTotalTime = 0;
	uint32_t worstTime = 0;
	uint32_t above20Count = 0;
	
	for (int trial=0; trial<3; trial++)
	{
		uint32_t b = 0;
		for (uint32_t cnt=0; cnt<1600 && b<1600; cnt++)
		{	
			if (!sd.card()->readBlock(imageFirstBlock + b, sectorBuf))
			{
				LcdGoto(0,2);
				LcdTinyStringP(PSTR("SD read error"), TEXT_NORMAL);
				while(1);
			}
										
			// alter the data, to prevent any kind of compression/optimization on the card
			for (uint16_t i=0; i<512; i++)
			{
				sectorBuf[i] ^= sectorBuf[i+1];
			}
			
			// blink the LED
			if ((writeCount & 0x7) == 0)
				PORT(STATUS_LED_PORT) ^= (1<<STATUS_LED_PIN);
			
			uint32_t t0;		
			
			_delay_ms(3);
			
			// write it	
			t0 = millis();
				
			if (!sd.card()->writeBlock(imageFirstBlock + b, sectorBuf))
			{
				LcdGoto(0,2);
				LcdTinyStringP(PSTR("SD write error"), TEXT_NORMAL);
				while(1);
			}		
			
			uint32_t writeTime = millis() - t0;
			
			// update stats
			writeCount++;
			writeTotalTime += writeTime;
			if (writeTime > 20)
				above20Count++;
			if (writeTime > worstTime)
				worstTime = writeTime;
			
			// pseudo-interleave
			if ((cnt & 1) == 0)
				b += 6;
			else
				b -= 5;
				
			_delay_ms(3);	
		}
	}
		
	uint32_t avg = (writeTotalTime + (writeCount >> 1))/ writeCount;
		
	LcdGoto(0,2);
	LcdTinyStringP(PSTR("AVG.ms/MAX.ms/LONG.%"), TEXT_NORMAL);	
	
	snprintf(textBuf, TEXTBUF_SIZE, "512B RRWI %lu/%lu/%lu", avg, worstTime, above20Count*100/writeCount);
	LcdGoto(0,3);
	LcdTinyString(textBuf, TEXT_NORMAL);
	
	// Card: actual capacity, block size, erase size, best test result (random read-write interleaved) average/max/percent "long" over 20ms
	// ----------------------------------------------------------	
	// PNY class 10 8GB: 7708 MB, 512B block, 64K erase. 512B RRWI 10/189/1 (random read-write interleave)
	// SanDisk unrated Ultra II 2GB: 1938 MB, 1024B block, 32K erase. 512B RRWI 7/79/0
	// Transcend unrated 2GB: 1875 MB, 1024B block, 128K erase. 512B RRWI 3/103/0
	// SanDisk unrated 128MB: 120 MB, 512B block, 16K erase. 512B RRWI 5/94/1
	
	// enabling the TACH stuff seems to cause some cards to get errors during this test. Coupling between traces? Why doesn't it happen during normal operation?
	
	// Transcend unrated 2GB: Finder consistently "goes to sleep" after copying first 19 tracks, but worked twice after 3 tries. Maybe it's too fast?
	
	
	
	while(1);
}

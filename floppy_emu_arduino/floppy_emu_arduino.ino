/*
 * floppy_emu_arduino.ino
 *
 *  Created on: 2014/07/12
 *      Author: sin
 */

#include <util/delay.h>

#include <SPI.h>
#include <SdFat.h>

#include "portmacros.h"

#include "millitimer.h"
#include "diskmenu.h"
#include "noklcd.h"

#include "floppyemu.h"

  SdFat sd;

void setup() {
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

  //SdFat sd;
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

}


// main loop
void loop() {
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
        {
        }					
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
restart: 
    ;					
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





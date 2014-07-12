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
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "diskmenu.h"
#include "SdFat.h"
#include "SdBaseFile.h"
#include "noklcd.h"

#define SECTORBUF_SIZE (23 * 512) // use the 24th buffer for directory breadcrumbs
extern uint8_t sectorBuf[24][512];
extern uint8_t extraBuf[512];

typedef struct FileEntry
{
	char longName[FILENAME_LEN+1];
	char shortName[SHORTFILENAME_LEN+1];
	eImageType imageFileType;
} FileEntry;

bool dirLfnNext(SdFat& sd, dir_t& dir, char* lfn)
{
  uint8_t offset[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
  uint8_t lfnIn = 130;
  uint8_t i;
  uint8_t ndir=0;
  uint8_t sum;
  uint8_t test=0;
  bool haveLong = false;

  while( sd.vwd()->read( &dir, 32 ) == 32 )
  {
    if( DIR_IS_LONG_NAME( &dir ) )
    {
      if( ! haveLong )
      {
        if(( dir.name[0] & 0XE0 ) != 0X40 )
          continue;
        ndir = dir.name[0] & 0X1F;
        test = dir.creationTimeTenths;
        haveLong = true;
        lfnIn = 130;
        lfn[ lfnIn ] = 0;
      }
      else if( dir.name[0] != --ndir || test != dir.creationTimeTenths )
      {
        haveLong = false;
        continue;
      }
      char *p = (char*) & dir;
      if( lfnIn > 0 )
      {
        lfnIn -= 13;
        for( i = 0; i < 13; i++ )
          lfn[lfnIn + i] = p[offset[i]];
      }
    }
    else if( DIR_IS_FILE_OR_SUBDIR( &dir ) 
             && dir.name[0] != DIR_NAME_DELETED 
             && dir.name[0] != DIR_NAME_FREE
             && dir.name[0] != '.')
    {
      if( haveLong )
      {
        for( sum = i = 0; i < 11; i++ )
           sum = (((sum & 1) << 7) | ((sum & 0xfe) >> 1)) + dir.name[i];
        if( sum != test || ndir != 1 )
        haveLong = false;
      }
      if( haveLong )
      {
        for( i = 0; lfnIn + i <= 130 ; i++ )
          lfn[i] = lfn[lfnIn + i];
        return true;
      }
      // else if( dir.reservedNT )
      //  return "Reserved NT";
      else
      {
        SdBaseFile::dirName( dir, lfn );
        return true;  
      }
    }
    else
    {
      if( dir.name[0] == DIR_NAME_FREE )
        break;
      haveLong = false;
    }
  }
  lfn[ 0 ] = 0;
  return false;
}

eImageType DiskImageFileType(dir_t& dir, const char *filename)
{
	if (filename[0] == '.')
		return DISK_IMAGE_NONE;
		
	if (DIR_IS_SUBDIR(&dir))
		return DISK_IMAGE_DIRECTORY;
		
	if (!DIR_IS_FILE(&dir))
		return DISK_IMAGE_NONE;
		
	uint32_t size = dir.fileSize;
		
	if (size == (unsigned long)1024 * 400)
		return DISK_IMAGE_400K;
	else if (size == (unsigned long)1024 * 800)
		return DISK_IMAGE_800K;
	else if (size == (unsigned long)1024 * 1440)
		return DISK_IMAGE_1440K;			
	else if (size > (unsigned long)1024 * 400 && 
			 size < (unsigned long)1024 * 1500)
	{
		// get the 8.3 filename
		char shortName[SHORTFILENAME_LEN+1];
		SdBaseFile::dirName(dir, shortName);
		
		// read the first sector of the file
		SdBaseFile f;
		if (f.open(shortName, O_RDONLY))
		{	
			f.read(extraBuf, 512);
			f.close();
			
			// is it a DiskCopy 4.2 image?
			if (extraBuf[0x52] == 0x01 &&
				extraBuf[0x53] == 0x00)
			{
				size = ((unsigned long)extraBuf[0x41] * 65536 + (unsigned long)extraBuf[0x42] * 256 + (unsigned long)extraBuf[0x43]) / 1024;
			
				if (size == 400)
					return DISK_IMAGE_DISKCOPY_400K;
				else if (size == 800)
					return DISK_IMAGE_DISKCOPY_800K;
				else if (size == 1440)
					return DISK_IMAGE_DISKCOPY_1440K;
			}	
		}					
	}		
	
	return DISK_IMAGE_NONE;
}

uint16_t diskMenuEntryCount;
uint16_t diskMenuOffset = 0;
uint16_t diskMenuSelection = 0;
char selectedFile[FILENAME_LEN+1];
char selectedLongFile[FILENAME_LEN+1];
eImageType selectedFileType;
uint8_t subdirDepth = 0;

#define LONGFILENAME_LEN 130

void InitDiskMenu(SdFat& sd)
{	
	dir_t dir;
	char name[LONGFILENAME_LEN+1];
	
	diskMenuEntryCount = 0;
	
	// use the sector buffers to hold the filenames
	uint16_t maxEntries = SECTORBUF_SIZE / sizeof(FileEntry);
	FileEntry* pFileEntries = (FileEntry*)sectorBuf;
	
	sd.vwd()->rewind();	
	while (dirLfnNext(sd, dir, name) && diskMenuEntryCount < maxEntries)
	{		
		eImageType imageType;
		
		if ((imageType = DiskImageFileType(dir, name)) != DISK_IMAGE_NONE)
		{
			strncpy(pFileEntries[diskMenuEntryCount].longName, name, FILENAME_LEN+1);
			SdBaseFile::dirName(dir, pFileEntries[diskMenuEntryCount].shortName);
			pFileEntries[diskMenuEntryCount].imageFileType = imageType;
			diskMenuEntryCount++;	
		}									
	} 
	
	// add up directory, if not at the root
	if (!sd.vwd()->isRoot())
	{
		strncpy(pFileEntries[diskMenuEntryCount].longName, "..", FILENAME_LEN+1);
		strncpy(pFileEntries[diskMenuEntryCount].shortName, "..", SHORTFILENAME_LEN+1);
		pFileEntries[diskMenuEntryCount].imageFileType = DISK_IMAGE_UP_DIRECTORY;
		diskMenuEntryCount++;	
	}
				
	char file1[FILENAME_LEN+1], file2[FILENAME_LEN+1], temp[FILENAME_LEN+1];
	eImageType tempType;
	
	// sort the names by longname
	for (uint16_t i=0; i<diskMenuEntryCount; i++)
	{
		for (uint16_t j=i+1; j<diskMenuEntryCount; j++)
		{
			// convert filenames to upper case, for comparison purposes
			strncpy(file1, pFileEntries[i].longName, FILENAME_LEN+1);
			for (uint8_t x=0; x<strlen(file1); x++)
				file1[x] = toupper(file1[x]);
				
			strncpy(file2, pFileEntries[j].longName, FILENAME_LEN+1);
			for (uint8_t x=0; x<strlen(file2); x++)
				file2[x] = toupper(file2[x]);
			
			// sort directories before regular files	
			int diff = strncmp(file1, file2, FILENAME_LEN+1);
			if (pFileEntries[i].imageFileType == DISK_IMAGE_DIRECTORY ||
				pFileEntries[i].imageFileType == DISK_IMAGE_UP_DIRECTORY)
				diff -= 1000;
			if (pFileEntries[j].imageFileType == DISK_IMAGE_DIRECTORY ||
				pFileEntries[j].imageFileType == DISK_IMAGE_UP_DIRECTORY)	
				diff += 1000;
			
			// if file1 > file2, swap them	
			if (diff > 0)
			{
				strncpy(temp, pFileEntries[i].longName, FILENAME_LEN+1);
				strncpy(pFileEntries[i].longName, pFileEntries[j].longName, FILENAME_LEN+1);
				strncpy(pFileEntries[j].longName, temp, FILENAME_LEN+1);

				strncpy(temp, pFileEntries[i].shortName, SHORTFILENAME_LEN+1);
				strncpy(pFileEntries[i].shortName, pFileEntries[j].shortName, SHORTFILENAME_LEN+1);
				strncpy(pFileEntries[j].shortName, temp, SHORTFILENAME_LEN+1);		
				
				tempType = 	pFileEntries[i].imageFileType;
				pFileEntries[i].imageFileType = pFileEntries[j].imageFileType;
				pFileEntries[j].imageFileType = tempType;	
			}
		}
	}
}

void DrawDiskMenu(SdFat& sd)
{
	// scroll menu if necessary
	if (diskMenuSelection < diskMenuOffset)
		diskMenuOffset = diskMenuSelection;
	if (diskMenuSelection > diskMenuOffset+4)
		diskMenuOffset = diskMenuSelection-4;
			
	LcdGoto(0,0);
	LcdWrite(LCD_DATA, 0x7F);
	for (int i=0; i<19; i++)
		LcdWrite(LCD_DATA, 0x40);
	
	LcdTinyStringFramed("Select Disk");
	
	for (int i=0; i<19; i++)
		LcdWrite(LCD_DATA, 0x40);
	LcdWrite(LCD_DATA, 0x7F);
	
	if (diskMenuEntryCount == 0)
	{
		LcdGoto(0, 1);
		LcdTinyString("no image files found", TEXT_NORMAL);	
	}	
	else
	{
		FileEntry* pFileEntries = (FileEntry*)sectorBuf;
			
		int row = 0;
		for (uint16_t i=diskMenuOffset; i<diskMenuOffset+5 && i<diskMenuEntryCount; i++)
		{					
			bool selected = (i == diskMenuSelection);
				
			LcdGoto(0, row+1);
			for (int j=0; j<LCD_WIDTH; j++)
			{
				LcdWrite(LCD_DATA, selected ? 0x7F : 0x00);
			}
		
			// show the image name
			LcdGoto(1, row+1);
			LcdTinyString(pFileEntries[i].longName, selected ? TEXT_INVERSE : TEXT_NORMAL, LCD_WIDTH-1);	
			
			// draw a folder icon for subdirectories
			if (pFileEntries[i].imageFileType == DISK_IMAGE_DIRECTORY ||
				pFileEntries[i].imageFileType == DISK_IMAGE_UP_DIRECTORY)
			{
				LcdGoto(73, row+1);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x00): 0x00);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x3C): 0x3C);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x22): 0x22);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x22): 0x22);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x22): 0x22);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x24): 0x24);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x24): 0x24);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x3C): 0x3C);
				LcdWrite(LCD_DATA, selected ? (0x7F ^ 0x00): 0x00);
			}
			
			if (selected)
			{
				strncpy(selectedLongFile, pFileEntries[i].longName, FILENAME_LEN+1);
				strncpy(selectedFile, pFileEntries[i].shortName, SHORTFILENAME_LEN+1);
				selectedFileType = pFileEntries[i].imageFileType;
			}
												
			row++;	
		}
		
		// draw the scrollbar	
		if (diskMenuEntryCount <= 5)
		{
			// no scrollbar
			for (uint8_t r=1; r<6; r++)
			{
				LcdGoto(82, r);
				LcdWrite(LCD_DATA, 0x00);
				LcdWrite(LCD_DATA, 0x00);
			}
		}
		else
		{
			uint8_t barEnd = 8 + (uint16_t)39 * (diskMenuOffset + 5) / diskMenuEntryCount;	
			uint8_t barSize = (uint16_t)39 * 5 / diskMenuEntryCount; 
			uint8_t barStart = barEnd - barSize;
					
			for (uint8_t r=1; r<6; r++)
			{
				LcdGoto(82, r);
				LcdWrite(LCD_DATA, 0x00);
			
				uint8_t b = 0;
				for (uint8_t y=r*8; y<=r*8+7; y++)
				{
					if (y >= barStart && y <= barEnd)
						b = 0x80 | (b >> 1);
					else
						b = (b >> 1);
				}

				LcdWrite(LCD_DATA, b);		
			}
		}
	
		// prevent moving selection past end of list
		if (diskMenuSelection >= diskMenuOffset + row)
		{
			diskMenuSelection = diskMenuOffset + row - 1;
			if (row == 4 && diskMenuOffset > 0)
			{
				diskMenuOffset--; // scroll backwards
			}
			DrawDiskMenu(sd); // draw again
		}
	}	
}

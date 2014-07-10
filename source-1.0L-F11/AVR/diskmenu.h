/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

#ifndef DISKMENU_H_
#define DISKMENU_H_

#include <inttypes.h>

typedef enum {
	DISK_IMAGE_NONE = 0,
	DISK_IMAGE_DIRECTORY,
	DISK_IMAGE_UP_DIRECTORY,
	DISK_IMAGE_400K,
	DISK_IMAGE_800K,
	DISK_IMAGE_1440K,
	DISK_IMAGE_DISKCOPY_400K,
	DISK_IMAGE_DISKCOPY_800K,
	DISK_IMAGE_DISKCOPY_1440K
} eImageType;

#define FILENAME_LEN 21
#define SHORTFILENAME_LEN 12 // 8.3

extern uint16_t diskMenuSelection;
extern char selectedFile[];
extern char selectedLongFile[];
extern eImageType selectedFileType;
extern uint8_t subdirDepth;

class SdFat;

void InitDiskMenu(SdFat& sd);
void DrawDiskMenu(SdFat& sd);

#endif /* DISKMENU_H_ */

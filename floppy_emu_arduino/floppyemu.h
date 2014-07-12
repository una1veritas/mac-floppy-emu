/*
 * floppyemu.h
 *
 *  Created on: 2014/07/12
 *      Author: sin
 */

#ifndef FLOPPYEMU_H_
#define FLOPPYEMU_H_

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

// global variables predefinitions

#define TEXTBUF_SIZE 22
char textBuf[TEXTBUF_SIZE];

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

// public functions predefinitions

void ResetDiskState();
void ShowVersion();
void AdjustContrast(void);
void error(const char* msg);
void PromptForFirmwareUpdate();

#endif /* FLOPPYEMU_H_ */

/* 
    Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
    Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
	license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
	http://creativecommons.org/licenses/by-nc/3.0/
	
	Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
    Permissions beyond the scope of this license may be available at www.bigmessowires.com
	or from mailto:steve@bigmessowires.com.
*/

#include <avr/interrupt.h>
#include <util/atomic.h>
#include "millitimer.h"

static uint32_t milliCount;

void millitimerInit()
{ 	
	TCCR0A = 0; // normal counter mode
	TCCR0B = (1<<CS01) | (1<<CS00); // use oscillator/64: 4 microseconds at 16 MHz
    TIFR0 = (1 << TOV0); // clear the timer 0 interrupt flags 
	
	// don't enable the interrupt yet
    //TIMSK0 = (1 << TOIE0); // enable timer 0 overflow interrupt
}

void millitimerOn()
{
	TIMSK0 = (1 << TOIE0); // enable timer 0 overflow interrupt
}

void millitimerOff()
{
	TIMSK0 = 0;
}

ISR(TIMER0_OVF_vect)
{
	// timer overflows every 256 * 4 microseconds = 1.024 milliseconds
	milliCount++;
}
	
uint32_t millis()
{
	// What happens if the millitimer interrupt occurs in the middle of this function? A bad count could be returned
	uint32_t returnValue;
	
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		returnValue = milliCount;
	}
	
	return returnValue;
}
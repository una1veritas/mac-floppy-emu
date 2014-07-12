#include <math.h>
#include <avr/io.h>
#include <util/delay.h>
#include "ports.h"

#define TMS_PORT PORTC
#define TMS_PIN 3

#define TCK_PORT PORTC
#define TCK_PIN 2

#define TDO_PORT PINC
#define TDO_PIN 4

#define TDI_PORT PORTC
#define TDI_PIN 5

/* setPort:  Implement to set the named JTAG signal (p) to the new value (v).*/
void setPort(short p,short val)
{
    if (p==TMS)
	{
		if (val)
			TMS_PORT |= (1<<TMS_PIN);	
		else
			TMS_PORT &= ~(1<<TMS_PIN);	
	}	
    else if (p==TDI)
	{
		if (val)
			TDI_PORT |= (1<<TDI_PIN);	
		else
			TDI_PORT &= ~(1<<TDI_PIN);	
	}
    else if (p==TCK)
	{
		if (val)
			TCK_PORT |= (1<<TCK_PIN);	
		else
			TCK_PORT &= ~(1<<TCK_PIN);	
	}
}


/* toggle tck LH.  No need to modify this code.  It is output via setPort. */
void pulseClock()
{
    setPort(TCK,0);  /* set the TCK port to low  */
    setPort(TCK,1);  /* set the TCK port to high */
}

ReadFuncPtr pReadFunc = 0;
void setReadCallback(ReadFuncPtr p)
{
	pReadFunc = p;
}

/* readByte:  Implement to source the next byte from your XSVF file location */
/* read in a byte of data from the prom */
void readByte(unsigned char *data)
{
    if (pReadFunc)
		*data = pReadFunc();
    else
		*data = 0;
}

/* readTDOBit:  Implement to return the current value of the JTAG TDO signal.*/
/* read the TDO bit from port */
unsigned char readTDOBit()
{
	return bit_is_set(TDO_PORT, TDO_PIN) ? 1 : 0;
}

/* waitTime:  Implement as follows: */
/* REQUIRED:  This function must consume/wait at least the specified number  */
/*            of microsec, interpreting microsec as a number of microseconds.*/
/* REQUIRED FOR SPARTAN/VIRTEX FPGAs and indirect flash programming:         */
/*            This function must pulse TCK for at least microsec times,      */
/*            interpreting microsec as an integer value.                     */
/* RECOMMENDED IMPLEMENTATION:  Pulse TCK at least microsec times AND        */
/*                              continue pulsing TCK until the microsec wait */
/*                              requirement is also satisfied.               */
void waitTime(long microsec)
{
    static long tckCyclesPerMicrosec    = 1; /* must be at least 1 */
    long        tckCycles   = microsec * tckCyclesPerMicrosec;
    long        i;

	// For Floppy Emu, tckCyclesPerMicrosec = 1 results in wait times about 3x longer than necessary.
	
    /* This implementation is highly recommended!!! */
    /* This implementation requires you to tune the tckCyclesPerMicrosec 
       variable (above) to match the performance of your embedded system
       in order to satisfy the microsec wait time requirement. */
    for ( i = 0; i < tckCycles; ++i )
    {
        pulseClock();		
    }

#if 0
    /* Alternate implementation */
    /* For systems with TCK rates << 1 MHz;  Consider this implementation. */
    /* This implementation does not work with Spartan-3AN or indirect flash
       programming. */
    if ( microsec >= 50L )
    {
        /* Make sure TCK is low during wait for XC18V00/XCFxxS */
        /* Or, a running TCK implementation as shown above is an OK alternate */
        setPort( TCK, 0 );

        /* Use Windows Sleep().  Round up to the nearest millisec */
        _sleep( ( microsec + 999L ) / 1000L );
    }
    else    /* Satisfy FPGA JTAG configuration, startup TCK cycles */
    {
        for ( i = 0; i < microsec;  ++i )
        {
            pulseClock();
        }
    }
#endif

#if 0
    /* Alternate implementation */
    /* This implementation is valid for only XC9500/XL/XV, CoolRunner/II CPLDs, 
       XC18V00 PROMs, or Platform Flash XCFxxS/XCFxxP PROMs.  
       This implementation does not work with FPGAs JTAG configuration. */
    /* Make sure TCK is low during wait for XC18V00/XCFxxS PROMs */
    /* Or, a running TCK implementation as shown above is an OK alternate */
    setPort( TCK, 0 );
    /* Use Windows Sleep().  Round up to the nearest millisec */
    _sleep( ( microsec + 999L ) / 1000L );
#endif
}

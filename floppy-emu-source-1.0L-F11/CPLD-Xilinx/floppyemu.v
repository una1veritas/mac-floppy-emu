/* 
  Floppy Emu, copyright 2013 Steve Chamberlin, "Big Mess o' Wires". All rights reserved.
	
  Floppy Emu is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported 
  license. (CC BY-NC 3.0) The terms of the license may be viewed at 	
  http://creativecommons.org/licenses/by-nc/3.0/
	
  Based on a work at http://www.bigmessowires.com/macintosh-floppy-emu/
	
  Permissions beyond the scope of this license may be available at www.bigmessowires.com
  or from mailto:steve@bigmessowires.com.

  --------------------------------------------------------------------------------------
  
  Disk registers (read):	
	    State-control lines    Register
  CA2    CA1    CA0    SEL    addressed    Information in register

  0      0      0      0      DIRTN        Head step direction (0 = toward track 79, 1 = toward track 0)
  0      0      0      1      CSTIN        Disk in place (0 = disk is inserted)
  0      0      1      0      STEP         Drive head stepping (setting to 0 performs a step, returns to 1 when step is complete)
  0      0      1      1      WRTPRT       Disk locked (0 = locked)
  0      1      0      0      MOTORON      Drive motor running (0 = on, 1 = off)
  0      1      0      1      TK0          Head at track 0 (0 = at track 0)
  0		1		 1		  0		SWITCHED   	 Disk switched (1 = yes?) SWIM3: relax, also eject in progress
  0      1      1      1      TACH         GCR: Tachometer (produces 60 pulses for each rotation of the drive motor), MFM: Index pulse
  1      0      0      0      RDDATA0      Read data, lower head, side 0
  1      0      0      1      RDDATA1      Read data, upper head, side 1 
  1      0      1      0      SUPERDR      Drive is a Superdrive (0 = no, 1 = yes) SWIM3: two meg drive. 
  1      0      1      1      MFM_MODE		 SWIM3: MFM_MODE, 1 = yes (opposite of writing?)
  1      1      0      0      SIDES        Single- or double-sided drive (0 = single side, 1 = double side), SWIM: 0 = 4MB, 1 = not 4MB
  1      1      0      1      READY        0 = yes, SWIM3: SEEK_COMPLETE
  1      1      1      0      INSTALLED	 0 = yes, only used by SWIM, not IWM? SWIM3: drive present. 
  1      1      1      1      HSHK_HD      400K/800K: implements ready handshake if 1, Superdrive: Inserted disk capacity (0 = HD, 1 = DD), SWIM3: 1 = ONE_MEG_MEDIA 


  Disk registers (write):
    Control lines      Register
  CA1    CA0    SEL    addressed    Register function

  0      0      0      DIRTN        Set stepping direction (0 = toward track 79, 1 = toward track 0), SWIM3: SEEK_POSITIVE
  0      0      1      SWITCHED		Reset disk switched flag (writing 1 sets switch flag to 0)
  0      1      0      STEP         Step the drive head one track (setting to 0 performs a step, returns to 1 when step is complete)
  1      0      0      MOTORON      Turn drive motor on/off (0 = on, 1 = off)
  1      0      0      TWOMEGMEDIA_CHECK The first time zero is written, changes the behavior when reading SIDES
  0      1      1      MFM_MODE     0 = MFM, 1 = GCR
  1      1      0      EJECT        Eject the disk (writing 1 ejects the disk)
  1      1      0		  INDEX			if writing 0
*/

`define DRIVE_REG_DIRTN		0  
`define DRIVE_REG_CSTIN		1  	                           
`define DRIVE_REG_STEP		2                          
`define DRIVE_REG_WRTPRT	3  
`define DRIVE_REG_MOTORON	4  
`define DRIVE_REG_TK0		5  
`define DRIVE_REG_EJECT		6  	                           
`define DRIVE_REG_TACH		7  
`define DRIVE_REG_RDDATA0	8  
`define DRIVE_REG_RDDATA1	9  
`define DRIVE_REG_SUPERDR	10 
`define DRIVE_REG_UNUSED   11
`define DRIVE_REG_SIDES		12 
`define DRIVE_REG_READY		13 
`define DRIVE_REG_INSTALLED	14 
`define DRIVE_REG_HSHK_HD		15 

`define FIRMWARE_VERSION_NUMBER 		11

module floppyemu(
	input clk,
	
   // Macintosh interface	
	input ca0,				// PH0
	input ca1,				// PH1
	input ca2,				// PH2
	input lstrb,			// PH3
	input SEL, 				// HDSEL from VIA
	input _enable, 			
	input wr,
	input _wreq,
	input pwm, 				// unused
	output rd,
	 
	// microcontroller interface
	input _rst,
	
	output stepDirectionMotorOn,
	output reg stepRequest,
	input stepAck_diskInserted,
	
	output _wreqMCU,
	output reg rdAckWrTick,
	output reg driveCurrentSide,
	output reg ejectRequest,
	
	input driveTach,
	input byteReady_tk0,
	
	input outputEnable,
	inout [6:0] data,
	
	output zero,
	
	// status display 
	output led,
	
	// debugging
	output test
);

	/********** drive state data **********/
	reg _driveRegTK0;
	reg _driveRegMotorOn;
	reg driveRegStepDirection;
	reg _driveRegWriteProtect;
	reg _driveRegDiskInserted;
	reg _driveRegMFMMode;
	
	/********** serial to parallel interface **********/	
	// GCR: One bit every 2 microseconds
	// The exact rate on the Macintosh is actually 16 clocks @ 7.8336 MHz = 2.04 microseconds.
	// MFM: One bit every 1 microsecond
	reg [7:0] shifter;
	reg [5:0] bitTimer;
	reg [3:0] bitCounter;
	reg [6:0] wrData;
	reg rdHead;
	reg [1:0] wrHistory;
	reg mfmWriteSynced;
	reg wrClear;
	
	always @(posedge clk) begin
		wrHistory <= { wrHistory[0], wr };
	end
	
	always @(posedge clk or negedge _rst) begin
		if (_rst == 0) begin
			rdAckWrTick <= 0;
			_driveRegWriteProtect <= 1;
			_driveRegDiskInserted <= 1;
			_driveRegMFMMode <= 1;
			wrData <= `FIRMWARE_VERSION_NUMBER;
			mfmWriteSynced <= 0;
			wrClear <= 0;
		end
		else begin
			// one-way switch for disk inserted register - until next reset, stepAck_diskInserted will act only as stepAck
			if (_driveRegDiskInserted == 1 && stepAck_diskInserted == 0) begin
				_driveRegDiskInserted <= 0;
			end
			// is the Macintosh currently writing to the disk?
			if (_wreq == 0) begin
				// was there a transition on the wr line?
				// GCR: any transition
				// MFM: falling edge
				if (((wrHistory[1] != wrHistory[0]) && (_driveRegMFMMode == 1)) ||
					 ((wrHistory[1] && ~wrHistory[0]) && (_driveRegMFMMode == 0))) begin
					// has at least half a bit cell time elpased since the last cell boundary?
					if ((bitTimer >= 20 && _driveRegMFMMode == 1) ||
						 (bitTimer >= 10 && _driveRegMFMMode == 0)) begin 
						shifter <= { shifter[6:0], 1'b1 }; 
						bitCounter <= bitCounter - 1'b1;					
					end
					// do nothing if the clock count was less than half a cell
					
					// reset the bit timer
					bitTimer <= 0;
				end
				else begin
					// have one and a half bit cell times elapsed?
					if ((bitTimer >= 60 && _driveRegMFMMode == 1) ||
						 (bitTimer >= 30 && _driveRegMFMMode == 0)) begin
						shifter <= { shifter[6:0], 1'b0 };
						bitCounter <= bitCounter - 1'b1;
						
						if (_driveRegMFMMode == 1)
							bitTimer <= 20;
						else
							bitTimer <= 10;
					end
					else begin
						// init shifter at the beginning of a write, so we can recognize the framing bits later
						if (wrClear == 0) begin
							shifter <= 0;
							wrClear <= 1;
						end
						// has a complete byte been shifted in?
						else if (_driveRegMFMMode == 1) begin
							// GCR
							if (shifter[7] == 1) begin
								// GCR: The complete byte is shifter[7:0], but only 7 bits are stored in wrData, since the MSB is always 1.
								wrData <= shifter[6:0]; // store the byte for the mcu
								shifter <= 0; // clear the byte from the shifter
								rdAckWrTick <= ~rdAckWrTick; // signal the mcu that a new byte is ready
							end
						end	
						else begin
							// MFM
							// If we're in write mode, but haven't yet synched (framed the bytes in the bit stream),
							// and we see 01000100 in the shifter, assume that it's the first half of an A1 sync
							if ((bitCounter == 0) ||
								 (shifter == 8'h44 && mfmWriteSynced == 0)) begin		
								// MFM: send the mcu the data nibble in the low 4 bits, and clock bit C2 in bit 4
								wrData[0] <= shifter[0];
								wrData[1] <= shifter[2];
								wrData[2] <= shifter[4];
								wrData[3] <= shifter[6];
								wrData[4] <= shifter[5]; // clock bit
								bitCounter <= 8;
								
								rdAckWrTick <= (shifter == 8'h44 && mfmWriteSynced == 0) ? 0 : ~rdAckWrTick; // signal the mcu that a new nibble is ready
								mfmWriteSynced <= mfmWriteSynced | (shifter == 8'h44);
							end
						end
						
						bitTimer <= bitTimer + 1'b1;	
					end	
				end
			end
			else begin
				mfmWriteSynced <= 0;
				wrClear <= 0;
				// is it time for a new bit?
				if ((bitTimer == 40 && _driveRegMFMMode == 1) ||
					 (bitTimer == 20 && _driveRegMFMMode == 0))
				begin
					// are all the bits done?
					if (bitCounter == 0) begin
						// is there a new byte ready to read?
						if (byteReady_tk0 == 1) begin
							// if there's a byte ready, but no disk inserted, then load config options from the MCU
							if (_driveRegDiskInserted == 1) begin
								_driveRegWriteProtect <= data[0];
								_driveRegMFMMode <= data[1];
							end
							else begin
								// load the new byte. 
								if (_driveRegMFMMode == 1) begin
									// Only 7 bits are transferred, since the MSB is always 1.
									shifter <= { 1'b1, data };
								end
								else begin
									// For MFM, the 7 bits received from the MCU are:
									// 	0 0 m d3 d2 d1 d0
									// From this we can constuct the MFM-encoded byte with clock and data bits:
									// 	c3 d3 c2 d2 c1 d1 c0 d0
									// where cN = dN+1 NOR dN.
									// If m is 1, then this is part of a mark byte, and c2 should be forced to 0.
									shifter[7] <= ~(shifter[7] | data[3]); 
									shifter[6] <= data[3];
									shifter[5] <= ~(data[3] | data[2]) & ~data[4];
									shifter[4] <= data[2];
									shifter[3] <= ~(data[2] | data[1]); 
									shifter[2] <= data[1];
									shifter[1] <= ~(data[1] | data[0]);
									shifter[0] <= data[0];
								end
								bitCounter <= 7;
								rdAckWrTick <= 1;							
							end
						end
						else begin
							// insert a sync byte
							if (_driveRegMFMMode == 1)
							begin
								shifter <= { 8'b11111111 }; 
								bitCounter <= 9; // sync "byte" sends 10 bits rather than 8
							end
							else begin
								shifter <= { 8'b10101010 }; // logical 0x0, encoded 0xA. Should sync byte be 0x4E instead?
								bitCounter <= 7;
							end							
						end		
					end
					else begin
						if (bitCounter == 7) begin
							// Clear rdAck after the first bit is done. This gives the microcontroller 2 microseconds
							// to react to rdAck before it's deasserted.
							rdAckWrTick <= 0;
						end

						// there are still more bits remaining, so shift the next bit
						shifter <= { shifter[6:0], 1'b0 }; // left shift
						bitCounter <= bitCounter - 1'b1;
					end
				end
				/* GCR: After the bit shift is completed, update the read head state, using the MSB of the shift register.
					A logical 1 is sent as a falling (high to low) transition on the read head at a bit cell boundary time,
					a logical 0 is sent as no falling transition. */
				else if (bitTimer == 2 && shifter[7] == 1 && _driveRegMFMMode == 1) begin
					rdHead <= 1'b0;
				end
				/* GCR: Half-way through the bit cell time, set the read head to 1
					to prepare for a possible falling transition for the next bit. */
				else if (bitTimer == 20 && _driveRegMFMMode == 1)
				begin
					rdHead <= 1'b1;
				end
				/* MFM: At the start of the bit cell time, set the read head to 0 if the logical value is 1. */
				else if (bitTimer == 2 && shifter[7] == 1 && _driveRegMFMMode == 0)
				begin
					rdHead <= 1'b0;
				end
				/* MFM: About a quarter-way through the bit cell time, always reset read head to 1. */
				else if (bitTimer == 7 && _driveRegMFMMode == 0)
				begin
					rdHead <= 1'b1;
				end
				
				// increment bit timer modulo 40 (20 MFM)
				if ((bitTimer == 40 && _driveRegMFMMode == 1) ||
					 (bitTimer == 20 && _driveRegMFMMode == 0))
					bitTimer <= 0;
				else
					bitTimer <= bitTimer + 1'b1;
			end
		end
	end
	
	// enable the data output only if the MCU says its data lines are Hi Z
	assign data = (outputEnable == 1) ? wrData : 7'hZZ;
	
	/********** register read **********/
	wire [3:0] driveReadRegisterSelect = {ca2,ca1,ca0,SEL};
	
	reg registerContents;
	always @* begin
		case (driveReadRegisterSelect)
			`DRIVE_REG_DIRTN:
				registerContents = driveRegStepDirection; // step direction
			`DRIVE_REG_CSTIN:
				registerContents = _driveRegDiskInserted; // disk in drive, 0 = yes
			`DRIVE_REG_STEP:
				registerContents = ~stepRequest; // STEP, 1 = complete
			`DRIVE_REG_WRTPRT:
				registerContents = _driveRegWriteProtect; // write protect, 0 = on, 1 = off
			`DRIVE_REG_MOTORON:
				registerContents = _driveRegMotorOn; // 0 = motor on
			`DRIVE_REG_TK0:
				registerContents = _driveRegTK0; // TK0: track 0 indicator
			`DRIVE_REG_EJECT:
				registerContents = 1'b0; // disk switched?
			`DRIVE_REG_TACH:
				registerContents = driveTach; // TACH: 60 pulses for each rotation of the drive motor
			`DRIVE_REG_RDDATA0:
				registerContents = rdHead; // RDDATA0
			`DRIVE_REG_RDDATA1:
				registerContents = rdHead; // RDDATA1
			`DRIVE_REG_SUPERDR:
				registerContents = 1'b1; // SUPERDR, 1 = yes
			`DRIVE_REG_UNUSED:
				registerContents = 1'b0; // UNUSED
			`DRIVE_REG_SIDES:
				registerContents = 1'b1; // SIDES = double-sided drive
			`DRIVE_REG_READY:
				registerContents = 1'b0; // READY = yes
			`DRIVE_REG_INSTALLED:
				registerContents = 1'b0; // INSTALLED = yes
			`DRIVE_REG_HSHK_HD:
				registerContents = _driveRegMFMMode; // HSHK_HD = implements ready handshake, or DD/HD media

		endcase
	end
	assign rd = _enable == 1'b1 ? 1'bZ : registerContents;
	
	always @(posedge clk or negedge _rst) begin
		if (_rst == 0) begin
			driveCurrentSide = 0;
		end
		else if (_enable == 1'b0 && lstrb == 1'b0) begin
			if (driveReadRegisterSelect == `DRIVE_REG_RDDATA0)
				driveCurrentSide = 0;
			else if (driveReadRegisterSelect == `DRIVE_REG_RDDATA1)
				driveCurrentSide = 1;
		end
	end
	
	// compute the effective _wreq state for the microcontroller
	assign _wreqMCU = ~(_wreq == 0 && _enable == 0 && _driveRegMotorOn == 0);
	
	// cheesy: during a step request, stepDirectionMotorOn is the step direction. Otherwise it's motorOn.
	assign stepDirectionMotorOn = stepRequest ? driveRegStepDirection : _driveRegMotorOn;
	
	/********** register write **********/
	wire [2:0] driveWriteRegisterSelect = {ca1,ca0,SEL};
	
	reg [4:0] lstrbHistory;
	always @(posedge clk) begin
		lstrbHistory <= { lstrbHistory[3:0], lstrb };
	end
	
	always @(posedge clk or negedge _rst) begin
		if (_rst == 1'b0) begin
			_driveRegTK0 <= 0;
			_driveRegMotorOn <= 1;
			driveRegStepDirection <= 0;
			stepRequest <= 0;
			ejectRequest <= 0;
		end
		// was there a rising edge on lstrb?
		else if (_enable == 1'b0 && lstrbHistory == 5'b01111) begin
			case (driveWriteRegisterSelect)
				`DRIVE_REG_DIRTN:
					driveRegStepDirection <= ca2;
				//`DRIVE_REG_SWITCHED: // unused
				`DRIVE_REG_STEP:
					begin
						stepRequest <= 1; // tell the microcontroller that a step was performed
					end
				`DRIVE_REG_MOTORON:
					_driveRegMotorOn <= ca2;
				`DRIVE_REG_EJECT:
					if (ca2 == 1'b1) begin
						ejectRequest <= 1; // tell the microcontroller that the disk was ejected. This stays on forever (until next reset)
					end
			endcase
		end
		else begin
			// clear step request after mcu acknowledges it, and get the new track 0 state
			if (stepRequest == 1 && stepAck_diskInserted == 1'b1) begin
				stepRequest <= 0;
				_driveRegTK0 <= byteReady_tk0;
			end
		end
	end
	
	/********** Revision 1.0 board: status LEDs and fake SD writeProtect **********/
	assign led = _driveRegMotorOn;
	assign zero = 0;
	
	
endmodule
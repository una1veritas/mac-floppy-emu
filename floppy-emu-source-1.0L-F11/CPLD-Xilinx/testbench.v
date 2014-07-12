`timescale 1ns / 1ps

////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer:
//
// Create Date:   15:13:07 11/30/2011
// Design Name:   floppyemu
// Module Name:   C:/Users/steve/Documents/floppyemu/CPLD-Xilinx/testbench.v
// Project Name:  floppyemu
// Target Device:  
// Tool versions:  
// Description: 
//
// Verilog Test Fixture created by ISE for module: floppyemu
//
// Dependencies:
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
////////////////////////////////////////////////////////////////////////////////

module testbench;

	// Inputs
	reg clk;
	reg wr;

	// Outputs
	wire [7:0] wrData;
	wire rdAckWrByte;

	// Instantiate the Unit Under Test (UUT)
	floppyemu uut (
		.clk(clk), 
		.wr(wr), 
		.wrData(wrData), 
		.rdAckWrByte(rdAckWrByte)
	);

	initial begin
		clk = 0;		
	end
	
	always begin
		#70 clk = 1;
		#70 clk = 0;
	end
	
	initial begin
		// Initialize Inputs
		wr = 0;

		// Wait 100 ns for global reset to finish
		#4000;
        
		// send 10-bit sync byte
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;
		
		// send 10-bit sync byte
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;

		// send 10-bit sync byte
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;

		// send 10-bit sync byte
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;

		// send 10-bit sync byte
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000	 ;

		// D5 = 1101 0101
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		
		// AA = 1010 1010
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		
		// 96 = 1001 0110
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000	;
		
		// 96 = 1001 0110
		#2000 wr = ~wr;
		#2000 ;
		#2000 ;
		#2000 wr = ~wr;
		#2000 ;
		#2000 wr = ~wr;
		#2000 wr = ~wr;
		#2000	;
		
		$stop;
	end
      
endmodule


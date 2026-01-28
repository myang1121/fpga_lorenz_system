`timescale 1ns/1ns

module testbench();
	
	reg clk_50, clk_25, reset;
	
	reg [31:0] index;
	wire signed [26:0]  testbench_out;
	
	//Initialize clocks and index
	initial begin
		clk_50 = 1'b0;
		clk_25 = 1'b0;
		index  = 32'd0;
		//testbench_out = 15'd0 ;
	end
	
	//Toggle the clocks
	always begin
		#10
		clk_50  = !clk_50;
	end
	
	always begin
		#20
		clk_25  = !clk_25;
	end
	
	//Intialize and drive signals
	initial begin
		reset  = 1'b0;
		#10 
		reset  = 1'b1;
		#30
		reset  = 1'b0;
	end
	
	//Increment index
	always @ (posedge clk_50) begin
		index  <= index + 32'd1;
	end

	//Instantiation of Integrator module
	// Function integrated is a constant (ramp, overflow, ramp, overflow, repeat)
	integrator DUT   (		.out(testbench_out), 
        					.funct((7'd_1, 20'd_0)),
							.clk(clk_50), 
							.reset(reset),
							.InitialOut(27'd_0));

	
endmodule

/////////////////////////////////////////////////
//// integrator /////////////////////////////////
/////////////////////////////////////////////////

module integrator(out,funct,InitialOut,clk,reset);
	output signed [26:0] out; 		//the state variable V
	input signed [26:0] funct;      //the dV/dt function
	input clk, reset;
	input signed [26:0] InitialOut;  //the initial state variable V
	
	wire signed	[26:0] out, v1new ;
	reg signed	[26:0] v1 ;
	
	always @ (posedge clk) 
	begin
		if (reset==1) //reset	
			v1 <= InitialOut ; // 
		else 
			v1 <= v1new ;	
	end
	assign v1new = v1 + funct ;
	assign out = v1 ;
endmodule

//////////////////////////////////////////////////
//// signed mult of 7.20 format 2'comp////////////
//////////////////////////////////////////////////

module signed_mult (out, a, b);
	output 	signed  [26:0]	out;
	input 	signed	[26:0] 	a;
	input 	signed	[26:0] 	b;
	// intermediate full bit length
	wire 	signed	[53:0]	mult_out;
	assign mult_out = a * b;
	// select bits for 7.20 fixed point
	assign out = {mult_out[53], mult_out[45:20]};
endmodule
//////////////////////////////////////////////////
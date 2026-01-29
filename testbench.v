`timescale 1ns/1ns

module testbench();
	
	reg clk_50, clk_25, reset;
	
	reg [31:0] index;
	wire signed [26:0] x_output;
	wire signed [26:0] xdot_output;
	
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
	// Function integrated is a simple harmonic oscillator w/ damping
	integrator DUT   (		.x(x_output), 
        					.xdot(xdot_output),
							.clk(clk_50), // emulated clock 
							.reset(reset),
							.InitialX((7'd_32, 20'd_0)), // lets say initial at 32 (drop from x = 32)
							.InitialXdot(27'd_0)); // let's say initial at rest (holding oscillator still)

endmodule

/////////////////////////////////////////////////
//// integrator /////////////////////////////////
/////////////////////////////////////////////////

// for simple harmonic oscillator w/ damping, F = -kx - b(x_dot) = m(x_double_dot)
module integrator(
	output wire signed [26:0] x,
	output wire signed [26:0] xdot,
	input clk,
	input reset,
	input wire signed [26:0] InitialX,
	input wire signed [26:0] InitialXdot
);

	wire signed	[26:0] xnew,  xdotnew ; // these two wires always holding the next value for x and xdot
	reg signed	[26:0] xreg, xdotreg ;
	
	always @ (posedge clk) 
	begin
		if (reset==1) begin //reset	to initial condition
			xreg <= InitialX ;
			xdotreg <= InitialXdot ;
		end 
		else begin // otherwise, on every clock edge, update values in xreg, xdotreg
			xreg <= xnew ;	
			xdotreg <= xdotnew ;
		end
	end
	// the new values x and xdot can be computed purely as a function of: previous values (xreg, xdotreg) and function of time step --> Euler integration
	assign xnew = xreg + (xdotreg>>>10) ; // dt chosen to be a power of 2 --> 2^10 = dt = 1024
	assign xdotnew = xdotreg - (xreg>>>10) - (xdotreg>>>12);// (restoring + damping)*dt --> - (xreg>>>10) - (xdotreg>>>12), let's say b (damping constant) * dt = 2^12

	// finally, assign the outputs
	assign x = xreg ; // x will have value in x register
	assign xdot = xdotreg ;
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
`timescale 1ns/1ns

module testbench();
	
	reg clk_50, clk_25, reset;
	
	reg [31:0] index;
	wire signed [26:0] x_output;
	wire signed [26:0] y_output;
	wire signed [26:0] z_output;
	
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
	// Function integrated is a Lorenz System
	integrator DUT   (		.x(x_output), 
							.y(y_output),
							.z(z_output),
							.clk(clk_50), // emulated clock 
							.reset(reset),
							.InitialX(27'h7FF_FFFF), // default initial conditions, x(0) = -1 (27'h7FF_FFFF)
							.InitialY(27'h001999A), // y(0) = 0.1 (27'h001999A)
							.InitialZ(27'h1900000), // z(0) = 25 (27'h1900000)
							.sigma(27'h0A00000), // default parameters, sigma = 10 (27'h0A00000)
							.rho(27'h01C00000), // rho = 28 (27'h01C00000)
							.beta(27'h02AABAA)); // beta = 8/3 (27'h02AABAA)

endmodule

/////////////////////////////////////////////////
//// integrator /////////////////////////////////
/////////////////////////////////////////////////

// for Lorenz System
module integrator(
	// state variables
	output wire signed [26:0] x,
	output wire signed [26:0] y,
	output wire signed [26:0] z,
	// emulated clock
	input clk,
	input reset,
	// initial conditions
	input wire signed [26:0] InitialX,
	input wire signed [26:0] InitialY,
	input wire signed [26:0] InitialZ,
	// parameters
	input wire signed [26:0] sigma,
	input wire signed [26:0] rho,
	input wire signed [26:0] beta
	
);

	wire signed	[26:0] xnew,  ynew,  znew ; // these wires always holding the next value for state variables x, y, z
	reg signed	[26:0] xreg, yreg, zreg ;
	// signed mult output
	wired signed [26:0] sigma_y_x, x_rho_z, x_y, beta_z ;
	
	always @ (posedge clk) 
	begin
		if (reset==1) begin //reset	to initial condition
			xreg <= InitialX ; 
			yreg <= InitialY ; 
			zreg <= InitialZ ; 
		end 
		else begin // otherwise, on every clock edge, update values in xreg, yreg, zreg
			xreg <= xnew ;	
			yreg <= ynew ;
			zreg <= znew ;
		end
	end
	// multiply
	signed_mult SIGMA_Y_X(sigma_y_x, sigma, yreg - xreg);
	signed_mult X_RHO_Z(x_rho_z, xreg, rho - zreg); 
	signed_mult X_Y(x_y, xreg, yreg);
	signed_mult BETA_Z(beta_z, beta, zreg); 

	// let dt = 1/256 for default value, >>>8 is the same as diving by 256
	// x(k+1) = x(k) + dt*xdot(k)
	assign xnew = xreg + (sigma_y_x>>>8) ; 
	assign ynew = yreg + ((x_rho_z - yreg)>>>8) ; 
	assign znew = zreg + ((x_y - beta_z)>>>8) ; 

	// finally, assign the outputs
	assign x = xreg ; // x will have value in x register
	assign y = yreg ;
	assign z = zreg ;
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
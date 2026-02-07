///////////////////////////////////////
/// 640x480 version! 16-bit color
/// This code will segfault the original
/// DE1 computer
/// compile with
/// gcc graphics_video_16bit.c -o gr -O2 -lm
///
///////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/time.h> 
#include <math.h>
//#include "address_map_arm_brl4.h"


// lab 1 week 3 (additional libraries) --> add "-lpthread" to "gcc graphics..."
#include <pthread.h>
#include <semaphore.h>

// lab 1 week 3 (fix2float conversions)
#define int2fix(a)	(((int)(a)) << 20)
#define fix2int(a)	((signed char)((a) >> 20))
#define float2fix(a) (int)(a*1048576) // 2^20 = 1048576
#define	fix2float(a)  (((float)(a))/1048576.0)


// lab 1 week 3 (macros)
// default initial conditions
#define DEFAULT_X0				-1.0
#define DEFAULT_Y0				0.1
#define DEFAULT_Z0				25.0
// default parameters
#define DEFAULT_SIGMA			10.0
#define DEFAULT_RHO				28.0
#define DEFAULT_BETA            2.6666666 // 8/3
// define three projection's plot origin on 640x480 pixel VGA screen
#define XZ_ORIGIN_H				160
#define XZ_ORIGIN_V				120
#define YZ_ORIGIN_H				480
#define YZ_ORIGIN_V				120
#define XY_ORIGIN_H				320
#define XY_ORIGIN_V				360
// define where initial condition and parameter text should start writing on 640x480 pixel VGA screen
// let's do 5 pixel vertical distance between each line --> REMINDER TO MYSELF
#define INITIAL_PARAM_TEXT_START_H				20
#define INITIAL_PARAM_TEXT_START_V				360
// for go flag
#define PAUSE				0
#define RESUME				1



// lab 1 week 3 (global variables, modified in code)
// for plotting along horizontal and vertical direction of the three 2D projections (XZ, YZ, XY)
volatile signed int *horizontal_coord_xz = NULL ; // can probably just use the xyz pio read pointers
volatile signed int *vertical_coord_xz = NULL ;
volatile signed int *horizontal_coord_yz = NULL ;
volatile signed int *vertical_coord_yz = NULL ;
volatile signed int *horizontal_coord_xy = NULL ;
volatile signed int *vertical_coord_xy = NULL ;
// xyz output values from FPGA to ARM are fix-point --> fix2float --> some values very small (e.g 0.6, 0.1, 0.045) --> scale by some factor to be visible on 640x480 pixel VGA screen
float scale_factor = 5.0;
// control pace of drawing (stall for a bit after each clock pulse in integrator_thread)
unsigned int delay_time = 10000;
// if 1 --> integrator resumes, if 0 --> integrator pauses (ARM stop sending clock pulses to FPGA's integrator)
int goFlag = PAUSE;
// character array (max 63 character, one null terminator), accept keyboard inputs
char input_buffer[64];
// accept floating point value
float value_buffer;
// return value from scanf --> make sure successfully read all user input  
int j;
// for VGA_line, to draw a line from previous position (horiz_prev) to current position (horizontal_coord)
int signed horiz_prev_xz;
int signed vert_prev_xz;
int signed horiz_prev_yz;
int signed vert_prev_yz;
int signed horiz_prev_xy;
int signed vert_prev_xy;
// variables to be display later on vga text
float text_x0 = DEFAULT_X0;
float text_y0 = DEFAULT_Y0;
float text_z0 = DEFAULT_Z0;
float text_sigma = DEFAULT_SIGMA;
float text_rho = DEFAULT_RHO;
float text_beta = DEFAULT_BETA;


// lab 1 week 3 (global objects of type relative to pthreads --> mutex objects, semaphores)
// access to text buffer
pthread_mutex_t input_buffer_lock= PTHREAD_MUTEX_INITIALIZER;
// semaphores
sem_t reset_semaphore, user_input_semaphore ; // tell user_input_thread that reset (and initial condition, parameters) is set, ready for user input

// video display
#define SDRAM_BASE            0xC0000000 // hardware base address of axi bus
#define SDRAM_END             0xC3FFFFFF
#define SDRAM_SPAN			  0x04000000
// characters
#define FPGA_CHAR_BASE        0xC9000000 
#define FPGA_CHAR_END         0xC9001FFF
#define FPGA_CHAR_SPAN        0x00002000
/* Cyclone V FPGA devices */
#define HW_REGS_BASE          0xff200000 // hardware base address of lw axi bus
//#define HW_REGS_SPAN        0x00200000 
#define HW_REGS_SPAN          0x00005000 

// lab 1 week 3 (pio base address)
// lw axi bus marcos for pio offset 
#define CLOCK_PIO 			  0x10
#define RESET_PIO 			  0x20
#define SIGMA_PIO 			  0x30
#define RHO_PIO 			  0x40
#define BETA_PIO 			  0x50
#define X0_PIO 			      0x60
#define Y0_PIO 			      0x70
#define Z0_PIO 			      0x80
#define X_PIO_READ 			  0x90 // offsets
#define Y_PIO_READ 			  0x100
#define Z_PIO_READ 			  0x110


// graphics primitives
void VGA_text (int, int, char *);
void VGA_text_clear();
void VGA_box (int, int, int, int, short);
void VGA_rect (int, int, int, int, short);
void VGA_line(int, int, int, int, short) ;
void VGA_Vline(int, int, int, short) ;
void VGA_Hline(int, int, int, short) ;
void VGA_disc (int, int, int, short);
void VGA_circle (int, int, int, int);
// 16-bit primary colors 
// 16 bit RGB color value computed according to consecutive addressing (5 bit R, 6 bit G, 5 bit B --> 16 bit RGB color space according to Altera video IP cores)
#define red  (0+(0<<5)+(31<<11))
#define dark_red (0+(0<<5)+(15<<11))
#define green (0+(63<<5)+(0<<11))
#define dark_green (0+(31<<5)+(0<<11))
#define blue (31+(0<<5)+(0<<11))
#define dark_blue (15+(0<<5)+(0<<11))
#define yellow (0+(63<<5)+(31<<11))
#define cyan (31+(63<<5)+(0<<11))
#define magenta (31+(0<<5)+(31<<11))
#define black (0x0000)
#define gray (15+(31<<5)+(51<<11))
#define white (0xffff)
int colors[] = {red, dark_red, green, dark_green, blue, dark_blue, 
		yellow, cyan, magenta, gray, black, white};

// pixel macro --> draw pixel routine that associate pixel w/ address according to consecutive addressing mode
#define VGA_PIXEL(x,y,color) do{\
	int  *pixel_ptr ;\
	pixel_ptr = (int*)((char *)vga_pixel_ptr + (((y)*640+(x))<<1));\
	*(short *)pixel_ptr = (color);\
} while(0)

// the light weight buss base
void *h2p_lw_virtual_base;

// lab 1 week 3 (pio pointers)
// axi bus pio pointer
volatile signed int * x_pio_read_ptr = NULL ; // signed int, i should use 32 bit fix pt?
volatile signed int * y_pio_read_ptr = NULL ;
volatile signed int * z_pio_read_ptr = NULL ;
// lw axi bus pio pointer
volatile unsigned int * clock_pio_ptr = NULL ;
volatile unsigned int * reset_pio_ptr = NULL ;
volatile unsigned int * sigma_pio_ptr = NULL ; // from wikipedia, one normally assumes the parameters sigma, rho, beta positive
volatile unsigned int * rho_pio_ptr = NULL ;
volatile unsigned int * beta_pio_ptr = NULL ;
volatile signed int * x0_pio_ptr = NULL ; // signed int
volatile signed int * y0_pio_ptr = NULL ;
volatile signed int * z0_pio_ptr = NULL ;

// pixel buffer
volatile unsigned int * vga_pixel_ptr = NULL ;
void *vga_pixel_virtual_base;

// character buffer
volatile unsigned int * vga_char_ptr = NULL ;
void *vga_char_virtual_base;

// /dev/mem file id
int fd;

// measure time
struct timeval t1, t2;
double elapsedTime;

// lab 1 week 3 (pthreads)
// Thread 1: reset thread
void * reset_thread() {
	while(1) {
		// waiting to be signaled by user_input_thread (user type in "reset" to command line interface)
		sem_wait(&reset_semaphore) ;
		
		// clear the text ??? hmmmmm
		VGA_text_clear();

		// stop the animation (plotting pause)
		goFlag = PAUSE;

		// use default values?
		// default x0, y0, z0, sigma, rho, beta
		printf("Default values? (y/n):") ;
		j = scanf("%s", input_buffer) ; // read a string from command line interface and store in input buffer --> j is 1 if user input something, 0 if nothing is read
		

		if (strcmp(input_buffer, "y")==0) { // if y to default value
			// set default initial conditions and parameters
			// send to FPGA through PIOs
			*(x0_pio_ptr) = float2fix(DEFAULT_X0); 
			*(y0_pio_ptr) = float2fix(DEFAULT_Y0);
			*(z0_pio_ptr) = float2fix(DEFAULT_Z0);
			*(sigma_pio_ptr) = float2fix(DEFAULT_SIGMA);
			*(rho_pio_ptr) = float2fix(DEFAULT_RHO);
			*(beta_pio_ptr) = float2fix(DEFAULT_BETA);

			text_x0 = DEFAULT_X0;
			text_y0 = DEFAULT_Y0;
			text_z0 = DEFAULT_Z0;
			text_sigma = DEFAULT_SIGMA;
			text_rho = DEFAULT_RHO;
			text_beta = DEFAULT_BETA;

		} else if (strcmp(input_buffer, "n")==0) { // if n to default value
			// ask user for new initial condition values and new parameters, then set new initial condition and new parameter
			printf("Input new initial conditions and parameters in floating point.\n") ;
			printf("X0: ") ;
			j = scanf("%f", &value_buffer) ;
			text_x0 = value_buffer ; // store value to initial condition variable to be display later on vga text
			*(x0_pio_ptr) = float2fix(value_buffer);// set initial condition --> send to FPGA through PIOs
			printf("Y0: ") ;
			j = scanf("%f", &value_buffer) ;
			text_y0 = value_buffer ;
			*(y0_pio_ptr) = float2fix(value_buffer);
			printf("Z0: ") ;
			j = scanf("%f", &value_buffer) ;
			text_z0 = value_buffer ;
			*(z0_pio_ptr) = float2fix(value_buffer);
			printf("SIGMA: ") ;
			j = scanf("%f", &value_buffer) ;
			text_sigma = value_buffer ;
			*(sigma_pio_ptr) = float2fix(value_buffer);
			printf("RHO: ") ;
			j = scanf("%f", &value_buffer) ;
			text_rho = value_buffer ;
			*(rho_pio_ptr) = float2fix(value_buffer);
			printf("BETA: ") ;
			j = scanf("%f", &value_buffer) ;
			text_beta = value_buffer ;
			*(beta_pio_ptr) = float2fix(value_buffer);
		} 

		// consecutive addressing mode --> use VGA_LINE to plot!
		// for xz projection?
		vertical_coord_xz = x_pio_read_ptr ;
		horizontal_coord_xz = z_pio_read_ptr ;
		// for yz projection?
		vertical_coord_yz = y_pio_read_ptr ;
		horizontal_coord_yz = z_pio_read_ptr ;
		// for xy projection? (might be upside down unless make it negative)
		vertical_coord_xy = x_pio_read_ptr ;
		horizontal_coord_xy = y_pio_read_ptr ;
		
		// reset the FPGA state machine
		*clock_pio_ptr = 0;
		*reset_pio_ptr = 0;
		*reset_pio_ptr = 1; // have not yet reset the integrator because have not yet provide a positive edge of the clock
		*clock_pio_ptr = 1; // but now with reset high, toggle the clock high to 1 (positive edge of the clock with the reset input high) --> satisfies the reset condition and resets the integrator
		// now put clock and reset back to low
		*clock_pio_ptr = 0;
		*reset_pio_ptr = 0; 
		
		// everytime the c program toggles the clock 1 to 0, step the integrator by one step
		// clock the integrators
		*clock_pio_ptr = 1;
		*clock_pio_ptr = 0;

		// initialize the previous states 
		horiz_prev_xz = (int)(-fix2float(*(horizontal_coord_xz))*scale_factor) ; 
		vert_prev_xz = (int)(-fix2float(*(vertical_coord_xz))*scale_factor) ;
		horiz_prev_yz = (int)(-fix2float(*(horizontal_coord_yz))*scale_factor) ;
		vert_prev_yz = (int)(-fix2float(*(vertical_coord_yz))*scale_factor) ;
		horiz_prev_xy = (int)(-fix2float(*(horizontal_coord_xy))*scale_factor) ;
		vert_prev_xy = (int)(-fix2float(*(vertical_coord_xy))*scale_factor) ;

		// clear screen
		VGA_box(0, 0, 639, 479, black);

		// start the animation
		goFlag = RESUME ;

		// start the user input thread
		sem_post(&user_input_semaphore);

	} // end while(1)
}

// Thread 2: user input thread (command line interface)
void * user_input_thread() {
	while(1) {
		// wait for reset_thread to be done
		sem_wait(&user_input_semaphore) ;
		
		// command line interface
		printf("Enter command (f, s, p, r, c, sigma, rho, beta, reset): ") ;
		j = scanf("%s", input_buffer) ;

		// strcmp --> string compare, if input_buffer's string value identical to "s", output 0
		if (strcmp(input_buffer, "f")==0) { // speed up plotting
			delay_time = (delay_time<2)?delay_time:(delay_time>>1); // delay_time / 2
		} else if (strcmp(input_buffer, "s")==0) { // slow down plotting
			delay_time = (delay_time>5242888)?delay_time:(delay_time<<1) ; // delay_time * 2
		} else if (strcmp(input_buffer, "p")==0) { // pause plotting
			goFlag = PAUSE;
		} else if (strcmp(input_buffer, "r")==0) { // resume plotting
			goFlag = RESUME;
		} else if (strcmp(input_buffer, "c")==0) { // clear screen
			VGA_box(0, 0, 639, 479, black);
		} else if (strcmp(input_buffer, "sigma")==0) { // change sigma
			printf("SIGMA: ") ;
			j = scanf("%f", &value_buffer) ;
			text_sigma = value_buffer ;
			*(sigma_pio_ptr) = float2fix(value_buffer);
		} else if (strcmp(input_buffer, "rho")==0) { // change rho
			printf("RHO: ") ;
			j = scanf("%f", &value_buffer) ;
			text_rho = value_buffer ;
			*(rho_pio_ptr) = float2fix(value_buffer);
		} else if (strcmp(input_buffer, "beta")==0) { // change beta
			printf("BETA: ") ;
			j = scanf("%f", &value_buffer) ;
			text_beta = value_buffer ;
			*(beta_pio_ptr) = float2fix(value_buffer);
		} else if (strcmp(input_buffer, "reset")==0) { // reset
			sem_post(&reset_semaphore) ;
			sem_wait(&user_input_semaphore) ;
		}
		sem_post(&user_input_semaphore) ;
	} // end while(1)
}

// Thread 3: integrator thread
// integrator_thread always run in background (check if goFlag is 0 or 1), synchronize w/ user_input_thread (resume or pause plotting) by goFlag
// actually do plotting and write text (but only if goFlag RESUME, 1)
void * integrator_thread() {
	while(1) {
		if (goFlag == PAUSE) {
			// no clock pulses send to integrator, always animate same image

			// text that shows initial conditions and parameters
			char text_buffer[256]; 
			sprintf(text_buffer, "X0: %f", text_x0);
			VGA_text (20, 5, text_buffer);
			sprintf(text_buffer, "Y0: %f", text_y0);
			VGA_text (20, 6, text_buffer);
			sprintf(text_buffer, "Z0: %f", text_z0);
			VGA_text (20, 7, text_buffer);
			sprintf(text_buffer, "SIGMA: %f", text_sigma);
			VGA_text (20, 8, text_buffer);
			sprintf(text_buffer, "RHO: %f", text_rho);
			VGA_text (20, 9, text_buffer);
			sprintf(text_buffer, "BETA: %f", text_beta);
			VGA_text (20, 10, text_buffer);

			//draw to the screen
			//VGA_line(int x1, int y1, int x2, int y2, short c)
			//draws a line segment between previous position (x1, y1) and current position (x2, y2)

			//for xz projection
			VGA_line(horiz_prev_xz + XZ_ORIGIN_H,
					 vert_prev_xz + XZ_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_xz))*scale_factor) + XZ_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_xz))*scale_factor) + XZ_ORIGIN_V,
					red) ;

			// for yz projection
			VGA_line(horiz_prev_yz + YZ_ORIGIN_H,
					 vert_prev_yz + YZ_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_yz))*scale_factor) + YZ_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_yz))*scale_factor) + YZ_ORIGIN_V,
					green) ;

			// for xy projection
			VGA_line(horiz_prev_xy + XY_ORIGIN_H,
					 vert_prev_xy + XY_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_xy))*scale_factor) + XY_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_xy))*scale_factor) + XY_ORIGIN_V,
					blue) ;

			// store previous value

			// for xz projection
			horiz_prev_xz = (int)(-fix2float(*(horizontal_coord_xz))*scale_factor) ;
			vert_prev_xz = (int)(-fix2float(*(vertical_coord_xz))*scale_factor) ;
			// for yz projection
			horiz_prev_yz = (int)(-fix2float(*(horizontal_coord_yz))*scale_factor) ;
			vert_prev_yz = (int)(-fix2float(*(vertical_coord_yz))*scale_factor) ;
			// for xy projection
			horiz_prev_xy = (int)(-fix2float(*(horizontal_coord_xy))*scale_factor) ;
			vert_prev_xy = (int)(-fix2float(*(vertical_coord_xy))*scale_factor) ;

		}
		if (goFlag == RESUME) {
			// clock the integrators
			*clock_pio_ptr = 1; // positive edge of clock, xyz reg assume the value of xyznew
			*clock_pio_ptr = 0;

			// slow down drawing --> control pace of plotting
			usleep(delay_time) ;

			// text that shows initial conditions and parameters
			char text_buffer[256]; 
			sprintf(text_buffer, "X0: %f", text_x0);
			VGA_text (20, 5, text_buffer);
			sprintf(text_buffer, "Y0: %f", text_y0);
			VGA_text (20, 6, text_buffer);
			sprintf(text_buffer, "Z0: %f", text_z0);
			VGA_text (20, 7, text_buffer);
			sprintf(text_buffer, "SIGMA: %f", text_sigma);
			VGA_text (20, 8, text_buffer);
			sprintf(text_buffer, "RHO: %f", text_rho);
			VGA_text (20, 9, text_buffer);
			sprintf(text_buffer, "BETA: %f", text_beta);
			VGA_text (20, 10, text_buffer);

			//draw to the screen
			//VGA_line(int x1, int y1, int x2, int y2, short c)
			//draws a line segment between previous position (x1, y1) and current position (x2, y2)

			// for xz projection
			VGA_line(horiz_prev_xz + XZ_ORIGIN_H,
					 vert_prev_xz + XZ_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_xz))*scale_factor) + XZ_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_xz))*scale_factor) + XZ_ORIGIN_V,
					red) ;

			// for yz projection
			VGA_line(horiz_prev_yz + YZ_ORIGIN_H,
					 vert_prev_yz + YZ_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_yz))*scale_factor) + YZ_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_yz))*scale_factor) + YZ_ORIGIN_V,
					green) ;

			// for xy projection
			VGA_line(horiz_prev_xy + XY_ORIGIN_H,
					 vert_prev_xy + XY_ORIGIN_V,
					(int)(-fix2float(*(horizontal_coord_xy))*scale_factor) + XY_ORIGIN_H,
					(int)(-fix2float(*(vertical_coord_xy))*scale_factor) + XY_ORIGIN_V,
					blue) ;

			// store previous value

			// for xz projection
			horiz_prev_xz = (int)(-fix2float(*(horizontal_coord_xz))*scale_factor) ;
			vert_prev_xz = (int)(-fix2float(*(vertical_coord_xz))*scale_factor) ;
			// for yz projection
			horiz_prev_yz = (int)(-fix2float(*(horizontal_coord_yz))*scale_factor) ;
			vert_prev_yz = (int)(-fix2float(*(vertical_coord_yz))*scale_factor) ;
			// for xy projection
			horiz_prev_xy = (int)(-fix2float(*(horizontal_coord_xy))*scale_factor) ;
			vert_prev_xy = (int)(-fix2float(*(vertical_coord_xy))*scale_factor) ;
		}
	} // end while(1)
}
	
int main(void)
{
  	
	// === need to mmap: =======================
	// FPGA_CHAR_BASE
	// FPGA_ONCHIP_BASE      
	// HW_REGS_BASE        
  
	// === get FPGA addresses ==================
    // Open /dev/mem
	if( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) 	{
		printf( "ERROR: could not open \"/dev/mem\"...\n" );
		return( 1 );
	}
    
    // get virtual addr that maps to physical
	h2p_lw_virtual_base = mmap( NULL, HW_REGS_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, HW_REGS_BASE );	
	if( h2p_lw_virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap1() failed...\n" );
		close( fd );
		return(1);
	}

	// lab 1 week 3 (store correct virtual memory to ptr)
	clock_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + CLOCK_PIO);
	reset_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + RESET_PIO);
	sigma_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + SIGMA_PIO);
	rho_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + RHO_PIO);
	beta_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + BETA_PIO);
	x0_pio_ptr = (int *)(h2p_lw_virtual_base + X0_PIO);
	y0_pio_ptr = (int *)(h2p_lw_virtual_base + Y0_PIO);
	z0_pio_ptr = (int *)(h2p_lw_virtual_base + Z0_PIO);
	x_pio_read_ptr =(unsigned int *)(h2p_lw_virtual_base + X_PIO_READ);
	y_pio_read_ptr =(unsigned int *)(h2p_lw_virtual_base + Y_PIO_READ);
	z_pio_read_ptr =(unsigned int *)(h2p_lw_virtual_base + Z_PIO_READ);

	// to prevent horizontal_coord to be NULL when integrator_thread starts immediately and goFlag = PAUSE
	//HEREEEE
	// for xz projection?
	vertical_coord_xz = x_pio_read_ptr ;
	horizontal_coord_xz = z_pio_read_ptr ;
	// for yz projection?
	vertical_coord_yz = y_pio_read_ptr ;
	horizontal_coord_yz = z_pio_read_ptr ;
	// for xy projection? (might be upside down unless make it negative)
	vertical_coord_xy = x_pio_read_ptr ;
	horizontal_coord_xy = y_pio_read_ptr ;


	// === get VGA char addr =====================
	// get virtual addr that maps to physical
	vga_char_virtual_base = mmap( NULL, FPGA_CHAR_SPAN, ( 	PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_CHAR_BASE );	
	if( vga_char_virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap2() failed...\n" );
		close( fd );
		return(1);
	}
    
    // Get the address that maps to the FPGA LED control 
	vga_char_ptr =(unsigned int *)(vga_char_virtual_base);


	// === get VGA pixel addr ====================
	// get virtual addr that maps to physical
	vga_pixel_virtual_base = mmap( NULL, SDRAM_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, SDRAM_BASE);	
	if( vga_pixel_virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap3() failed...\n" );
		close( fd );
		return(1);
	}
    
    // Get the address that maps to the FPGA pixel buffer
	vga_pixel_ptr =(unsigned int *)(vga_pixel_virtual_base);

	

	// ===========================================

	/* create a message to be displayed on the VGA 
          and LCD displays */
	char text_top_row[40] = "DE1-SoC ARM/FPGA\0";
	char text_bottom_row[40] = "Cornell ece5760\0";
	char text_next[40] = "Graphics primitives\0";
	char num_string[20], time_string[20] ;
	char color_index = 0 ;
	int color_counter = 0 ;
	
	// position of disk primitive
	int disc_x = 0;
	// position of circle primitive
	int circle_x = 0 ;
	// position of box primitive
	int box_x = 5 ;
	// position of vertical line primitive
	int Vline_x = 350;
	// position of horizontal line primitive
	int Hline_y = 250;

	//VGA_text (34, 1, text_top_row);
	//VGA_text (34, 2, text_bottom_row);
	// clear the screen
	VGA_box (0, 0, 639, 479, 0x0000);
	// clear the text
	VGA_text_clear();
	// write text
	VGA_text (10, 1, text_top_row);
	VGA_text (10, 2, text_bottom_row);
	VGA_text (10, 3, text_next);
	
	// R bits 11-15 mask 0xf800
	// G bits 5-10  mask 0x07e0
	// B bits 0-4   mask 0x001f
	// so color = B+(G<<5)+(R<<11);

	// lab 1 week 3 (declare objects of type pthread_t and initialize semaphores)

	// the thread identifiers
	pthread_t thread_reset_thread, thread_user_input_thread, thread_integrator_thread;

	// the semaphore inits
	// reset_semaphore is not ready because nothing has been input yet
	sem_init(&reset_semaphore, 0, 0); // 3rd argument is initial condition
	// user_input_semaphore is ready to take user input at init time
	sem_init(&user_input_semaphore, 0, 1);

	// for portability, explicitly create threads in a joinable state (system runs until thread returns, but none returns)
	// thread attribute used here to allow JOIN
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	//create the threads --> associate pthread functions created above w/ pthread objects created in main
	pthread_create(&thread_reset_thread,NULL,reset_thread,NULL);
	pthread_create(&thread_user_input_thread,NULL,user_input_thread,NULL);
	pthread_create(&thread_integrator_thread,NULL,integrator_thread,NULL);

	// start scheduler, start scheduling these threads, keep scheduling until thread joins (never will)
	pthread_join(thread_reset_thread, NULL);
	pthread_join(thread_user_input_thread, NULL);
	pthread_join(thread_integrator_thread, NULL);
	return 0;
} // end main

/****************************************************************************************
 * Subroutine to send a string of text to the VGA monitor 
****************************************************************************************/
void VGA_text(int x, int y, char * text_ptr)
{
  	volatile char * character_buffer = (char *) vga_char_ptr ;	// VGA character buffer
	int offset;
	/* assume that the text string fits on one line */
	offset = (y << 7) + x;
	while ( *(text_ptr) )
	{
		// write to the character buffer
		*(character_buffer + offset) = *(text_ptr);	
		++text_ptr;
		++offset;
	}
}

/****************************************************************************************
 * Subroutine to clear text to the VGA monitor 
****************************************************************************************/
void VGA_text_clear()
{
  	volatile char * character_buffer = (char *) vga_char_ptr ;	// VGA character buffer
	int offset, x, y;
	for (x=0; x<79; x++){
		for (y=0; y<59; y++){
	/* assume that the text string fits on one line */
			offset = (y << 7) + x;
			// write to the character buffer
			*(character_buffer + offset) = ' ';		
		}
	}
}

/****************************************************************************************
 * Draw a filled rectangle on the VGA monitor 
****************************************************************************************/
#define SWAP(X,Y) do{int temp=X; X=Y; Y=temp;}while(0) 

void VGA_box(int x1, int y1, int x2, int y2, short pixel_color)
{
	char  *pixel_ptr ; 
	int row, col;

	/* check and fix box coordinates to be valid */
	if (x1>639) x1 = 639;
	if (y1>479) y1 = 479;
	if (x2>639) x2 = 639;
	if (y2>479) y2 = 479;
	if (x1<0) x1 = 0;
	if (y1<0) y1 = 0;
	if (x2<0) x2 = 0;
	if (y2<0) y2 = 0;
	if (x1>x2) SWAP(x1,x2);
	if (y1>y2) SWAP(y1,y2);
	for (row = y1; row <= y2; row++)
		for (col = x1; col <= x2; ++col)
		{
			//640x480
			//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
			// set pixel color
			//*(char *)pixel_ptr = pixel_color;	
			VGA_PIXEL(col,row,pixel_color);	
		}
}

/****************************************************************************************
 * Draw a outline rectangle on the VGA monitor 
****************************************************************************************/
#define SWAP(X,Y) do{int temp=X; X=Y; Y=temp;}while(0) 

void VGA_rect(int x1, int y1, int x2, int y2, short pixel_color)
{
	char  *pixel_ptr ; 
	int row, col;

	/* check and fix box coordinates to be valid */
	if (x1>639) x1 = 639;
	if (y1>479) y1 = 479;
	if (x2>639) x2 = 639;
	if (y2>479) y2 = 479;
	if (x1<0) x1 = 0;
	if (y1<0) y1 = 0;
	if (x2<0) x2 = 0;
	if (y2<0) y2 = 0;
	if (x1>x2) SWAP(x1,x2);
	if (y1>y2) SWAP(y1,y2);
	// left edge
	col = x1;
	for (row = y1; row <= y2; row++){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;	
		VGA_PIXEL(col,row,pixel_color);		
	}
		
	// right edge
	col = x2;
	for (row = y1; row <= y2; row++){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;	
		VGA_PIXEL(col,row,pixel_color);		
	}
	
	// top edge
	row = y1;
	for (col = x1; col <= x2; ++col){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;	
		VGA_PIXEL(col,row,pixel_color);
	}
	
	// bottom edge
	row = y2;
	for (col = x1; col <= x2; ++col){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;
		VGA_PIXEL(col,row,pixel_color);
	}
}

/****************************************************************************************
 * Draw a horixontal line on the VGA monitor 
****************************************************************************************/
#define SWAP(X,Y) do{int temp=X; X=Y; Y=temp;}while(0) 

void VGA_Hline(int x1, int y1, int x2, short pixel_color)
{
	char  *pixel_ptr ; 
	int row, col;

	/* check and fix box coordinates to be valid */
	if (x1>639) x1 = 639;
	if (y1>479) y1 = 479;
	if (x2>639) x2 = 639;
	if (x1<0) x1 = 0;
	if (y1<0) y1 = 0;
	if (x2<0) x2 = 0;
	if (x1>x2) SWAP(x1,x2);
	// line
	row = y1;
	for (col = x1; col <= x2; ++col){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;	
		VGA_PIXEL(col,row,pixel_color);		
	}
}

/****************************************************************************************
 * Draw a vertical line on the VGA monitor 
****************************************************************************************/
#define SWAP(X,Y) do{int temp=X; X=Y; Y=temp;}while(0) 

void VGA_Vline(int x1, int y1, int y2, short pixel_color)
{
	char  *pixel_ptr ; 
	int row, col;

	/* check and fix box coordinates to be valid */
	if (x1>639) x1 = 639;
	if (y1>479) y1 = 479;
	if (y2>479) y2 = 479;
	if (x1<0) x1 = 0;
	if (y1<0) y1 = 0;
	if (y2<0) y2 = 0;
	if (y1>y2) SWAP(y1,y2);
	// line
	col = x1;
	for (row = y1; row <= y2; row++){
		//640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10)    + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;	
		VGA_PIXEL(col,row,pixel_color);			
	}
}


/****************************************************************************************
 * Draw a filled circle on the VGA monitor 
****************************************************************************************/

void VGA_disc(int x, int y, int r, short pixel_color)
{
	char  *pixel_ptr ; 
	int row, col, rsqr, xc, yc;
	
	rsqr = r*r;
	
	for (yc = -r; yc <= r; yc++)
		for (xc = -r; xc <= r; xc++)
		{
			col = xc;
			row = yc;
			// add the r to make the edge smoother
			if(col*col+row*row <= rsqr+r){
				col += x; // add the center point
				row += y; // add the center point
				//check for valid 640x480
				if (col>639) col = 639;
				if (row>479) row = 479;
				if (col<0) col = 0;
				if (row<0) row = 0;
				//pixel_ptr = (char *)vga_pixel_ptr + (row<<10) + col ;
				// set pixel color
				//*(char *)pixel_ptr = pixel_color;
				VGA_PIXEL(col,row,pixel_color);	
			}
					
		}
}

/****************************************************************************************
 * Draw a  circle on the VGA monitor 
****************************************************************************************/

void VGA_circle(int x, int y, int r, int pixel_color)
{
	char  *pixel_ptr ; 
	int row, col, rsqr, xc, yc;
	int col1, row1;
	rsqr = r*r;
	
	for (yc = -r; yc <= r; yc++){
		//row = yc;
		col1 = (int)sqrt((float)(rsqr + r - yc*yc));
		// right edge
		col = col1 + x; // add the center point
		row = yc + y; // add the center point
		//check for valid 640x480
		if (col>639) col = 639;
		if (row>479) row = 479;
		if (col<0) col = 0;
		if (row<0) row = 0;
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10) + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;
		VGA_PIXEL(col,row,pixel_color);	
		// left edge
		col = -col1 + x; // add the center point
		//check for valid 640x480
		if (col>639) col = 639;
		if (row>479) row = 479;
		if (col<0) col = 0;
		if (row<0) row = 0;
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10) + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;
		VGA_PIXEL(col,row,pixel_color);	
	}
	for (xc = -r; xc <= r; xc++){
		//row = yc;
		row1 = (int)sqrt((float)(rsqr + r - xc*xc));
		// right edge
		col = xc + x; // add the center point
		row = row1 + y; // add the center point
		//check for valid 640x480
		if (col>639) col = 639;
		if (row>479) row = 479;
		if (col<0) col = 0;
		if (row<0) row = 0;
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10) + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;
		VGA_PIXEL(col,row,pixel_color);	
		// left edge
		row = -row1 + y; // add the center point
		//check for valid 640x480
		if (col>639) col = 639;
		if (row>479) row = 479;
		if (col<0) col = 0;
		if (row<0) row = 0;
		//pixel_ptr = (char *)vga_pixel_ptr + (row<<10) + col ;
		// set pixel color
		//*(char *)pixel_ptr = pixel_color;
		VGA_PIXEL(col,row,pixel_color);	
	}
}

// =============================================
// === Draw a line
// =============================================
//plot a line 
//at x1,y1 to x2,y2 with color 
//Code is from David Rodgers,
//"Procedural Elements of Computer Graphics",1985
void VGA_line(int x1, int y1, int x2, int y2, short c) {
	int e;
	signed int dx,dy,j, temp;
	signed int s1,s2, xchange;
     signed int x,y;
	char *pixel_ptr ;
	
	/* check and fix line coordinates to be valid */
	if (x1>639) x1 = 639;
	if (y1>479) y1 = 479;
	if (x2>639) x2 = 639;
	if (y2>479) y2 = 479;
	if (x1<0) x1 = 0;
	if (y1<0) y1 = 0;
	if (x2<0) x2 = 0;
	if (y2<0) y2 = 0;
        
	x = x1;
	y = y1;
	
	//take absolute value
	if (x2 < x1) {
		dx = x1 - x2;
		s1 = -1;
	}

	else if (x2 == x1) {
		dx = 0;
		s1 = 0;
	}

	else {
		dx = x2 - x1;
		s1 = 1;
	}

	if (y2 < y1) {
		dy = y1 - y2;
		s2 = -1;
	}

	else if (y2 == y1) {
		dy = 0;
		s2 = 0;
	}

	else {
		dy = y2 - y1;
		s2 = 1;
	}

	xchange = 0;   

	if (dy>dx) {
		temp = dx;
		dx = dy;
		dy = temp;
		xchange = 1;
	} 

	e = ((int)dy<<1) - dx;  
	 
	for (j=0; j<=dx; j++) {
		//video_pt(x,y,c); //640x480
		//pixel_ptr = (char *)vga_pixel_ptr + (y<<10)+ x; 
		// set pixel color
		//*(char *)pixel_ptr = c;
		VGA_PIXEL(x,y,c);			
		 
		if (e>=0) {
			if (xchange==1) x = x + s1;
			else y = y + s2;
			e = e - ((int)dx<<1);
		}

		if (xchange==1) y = y + s2;
		else x = x + s1;

		e = e + ((int)dy<<1);
	}
}
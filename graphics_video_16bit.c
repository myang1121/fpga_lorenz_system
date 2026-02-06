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
#define DEFAULT_X0				float2fix(-1.0) // uhhh negative value how
#define DEFAULT_Y0				float2fix(0.1)
#define DEFAULT_Z0				float2fix(25.0)
// default parameters
#define DEFAULT_SIGMA			float2fix(10.0) 
#define DEFAULT_RHO				float2fix(28.0)
#define DEFAULT_BETA            float2fix(2.6666666) // 8/3


// lab 1 week 3 (global variables)
char input_buffer[64]; // accept keyboard inputs 
int delay_time; // control pace of drawing
int goFlag = 0; // if 1, integrator_thread run


// lab 1 week 3 (global objects of type relative to pthreads --> mutex objects, semaphores)
// access to text buffer
pthread_mutex_t input_buffer_lock= PTHREAD_MUTEX_INITIALIZER;
// delay_time protection
pthread_mutex_t delay_time_lock= PTHREAD_MUTEX_INITIALIZER; // do i need???
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
// axi bus marcos for pio offset 
#define X_PIO_READ 			  0x10 // offsets
#define Y_PIO_READ 			  0x20
#define Z_PIO_READ 			  0x30
// lw axi bus marcos for pio offset 
#define CLOCK_PIO 			  0x10
#define RESET_PIO 			  0x20
#define SIGMA_PIO 			  0x30
#define RHO_PIO 			  0x40
#define BETA_PIO 			  0x50
#define X0_PIO 			      0x60
#define Y0_PIO 			      0x70
#define Z0_PIO 			      0x80


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
	pixel_ptr = (int*)((char *)vga_pixel_ptr + (((y)*640+(x))<<1)) ; \ // 16 bit format in xy fornat
	*(short *)pixel_ptr = (color);\
} while(0)

// the light weight buss base
void *h2p_lw_virtual_base;

// lab 1 week 3 (pio pointers)
// axi bus pio pointer
volatile int * x_pio_read_ptr = NULL ; // signed int, i should use 32 bit fix pt?
volatile int * y_pio_read_ptr = NULL ;
volatile int * z_pio_read_ptr = NULL ;
// lw axi bus pio pointer
volatile unsigned int * clock_pio_ptr = NULL ;
volatile unsigned int * reset_pio_ptr = NULL ;
volatile unsigned int * sigma_pio_ptr = NULL ;
volatile unsigned int * rho_pio_ptr = NULL ;
volatile unsigned int * beta_pio_ptr = NULL ;
volatile int * x0_pio_ptr = NULL ; // signed int
volatile int * y0_pio_ptr = NULL ;
volatile int * z0_pio_ptr = NULL ;

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
		
		// clear the text
		VGA_text_clear();

		// stop the animation
		goFlag = 0;

		pthread_mutex_lock(&input_buffer_lock);
		// actual enter
		// use default values?
		// default x0, y0, z0, sigma, rho, beta
		printf("Default values? (y):") ;
		j = scanf("&s", input_buffer) ;
		// unlock the input_buffer
		pthread_mutex_unlock(&input_buffer_lock);

		// set initial conditions?
		// consecutive addressing mode --> just use VGA_PIXEL(x, y, color)?
		// for xz projection?
		vertical_coord = y_pio_read_ptr ;
		horizontal_coord = x_pio_read_ptr ;
		// for yz projection?

		// for xy projection? (might be upside down unless make it negative)

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
		horiz_prev = (int)(-fix2float(*(horizontal_coord))*scale_factor) ;
		vert_prev = (int)(-fix2float(*(vertical_coord))*scale_factor) ;

		// clear screen
		VGA_box(0, 0, 639, 479, black);

		// start the animation
		goFlag = 1 ;

		// start the user input thread
		sem_post(&user_input_semaphore);

	} // end while(1)
}

// Thread 2: user input thread (command line interface)
void * user_input_thread() {
	while(1) {
		// wait for reset_thread to be done
		sem_wait(&user_input_semaphore) ;

		pthread_mutex_lock(&input_buffer_lock);
		// command line interface
		printf("Enter command: ") ;
		j = scanf("%s", input_buffer) ;
		if (strcmp(input_buffer, "s")==0) { // slow down plotting
			delay_time = (delay_time>5242888)?delay_time:(delay_time<<1) ;
		} else if (strcmp(input_buffer, "f")==0) { // fast plotting
			delay_time = (delay_time<2)?delay_time:(delay_time>>1);
		} else if (strcmp(input_buffer, "p")==0) {
			goFlag = (goFlag==1)?0:1 ;
		} else if (strcmp(input_buffer, "reset")==0) {
			sem_post(&reset_semaphore) ;
			sem_wait(&user_input_semaphore) ;
		}
		pthread_mutex_unlock(&input_buffer_lock);
		sem_post(&user_input_semaphore) ;
	} // end while(1)
}

// Thread 3: integrator thread
// always run in background?
// actually do plotting and write text (but only if goFlag true)
void * integrator_thread() {
	while(1) {
		if (!goFlag) {
			// waiting for plotting to start...
			VGA_text (0, 55, "Lorenz System Demo");
			char text_title[40] = "ECE 5760\0" ;
			VGA_text(0, 54, text_title) ;
		}
		if (goFlag) {
			// clock the integrators
			*clock_pio_ptr = 1; // positive edge of clock, xyz reg assume the value of xyznew
			*clock_pio_ptr = 0;

			// slow down drawing --> control pace of plotting
			usleep(delay_time) ;

			// draw to the screen
			VGA_line(horiz_prev + 320,
					 vert_prev + 240,
					(int)(-fix2float(*(horizontal_coord))*scale_factor) + 320,
					(int)(-fix2float(*(vertifcal_coord))*scale_factor) + 240,
					green) ;

			// store previous value
			horiz_prev = (int)(-fix2float(*(horizontal_coord))*scale_factor) ;
			vert_prev = (int)(-fix2float(*(vertical_coord))*scale_factor) ;
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
	x0_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + X0_PIO);
	y0_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + Y0_PIO);
	z0_pio_ptr = (unsigned int *)(h2p_lw_virtual_base + Z0_PIO);


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

	// lab 1 week 3 (store correct virtual memory to ptr)
	x_pio_read_ptr =(unsigned int *)(vga_pixel_virtual_base + X_PIO_READ);
	y_pio_read_ptr =(unsigned int *)(vga_pixel_virtual_base + Y_PIO_READ);
	z_pio_read_ptr =(unsigned int *)(vga_pixel_virtual_base + Z_PIO_READ);

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

	// i should delete this whole while(1) ***just showing all the vga graphics primitives in use
	while(1) 
	{
		// start timer
		gettimeofday(&t1, NULL);
	
		//VGA_box(int x1, int y1, int x2, int y2, short pixel_color)
		VGA_box(64, 0, 240, 50, blue); // blue box
		VGA_box(250, 0, 425, 50, red); // red box
		VGA_box(435, 0, 600, 50, green); // green box
		
		// cycle thru the colors
		if (color_index++ == 11) color_index = 0;
		
		//void VGA_disc(int x, int y, int r, short pixel_color)
		VGA_disc(disc_x, 100, 20, colors[color_index]);
		disc_x += 35 ;
		if (disc_x > 640) disc_x = 0;
		
		//void VGA_circle(int x, int y, int r, short pixel_color)
		VGA_circle(320, 200, circle_x, colors[color_index]);
		VGA_circle(320, 200, circle_x+1, colors[color_index]);
		circle_x += 2 ;
		if (circle_x > 99) circle_x = 0;
		
		//void VGA_rect(int x1, int y1, int x2, int y2, short pixel_color)
		VGA_rect(10, 478, box_x, 478-box_x, rand()&0xffff);
		box_x += 3 ;
		if (box_x > 195) box_x = 10;
		
		//void VGA_line(int x1, int y1, int x2, int y2, short c)
		VGA_line(210+(rand()&0x7f), 350+(rand()&0x7f), 210+(rand()&0x7f), 
				350+(rand()&0x7f), colors[color_index]);
		
		// void VGA_Vline(int x1, int y1, int y2, short pixel_color)
		VGA_Vline(Vline_x, 475, 475-(Vline_x>>2), rand()&0xffff);
		Vline_x += 2 ;
		if (Vline_x > 620) Vline_x = 350;
		
		//void VGA_Hline(int x1, int y1, int x2, short pixel_color)
		VGA_Hline(400, Hline_y, 550, rand()&0xffff);
		Hline_y += 2 ;
		if (Hline_y > 400) Hline_y = 240;
		
		// stop timer
		gettimeofday(&t2, NULL);
		elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000000.0;      // sec to us
		elapsedTime += (t2.tv_usec - t1.tv_usec) ;   // us 
		sprintf(time_string, "T = %6.0f uSec  ", elapsedTime);
		VGA_text (10, 4, time_string);
		// set frame rate
		//usleep(17000);
		
	} // end while(1)
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
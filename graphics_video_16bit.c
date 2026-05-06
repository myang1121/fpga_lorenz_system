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
#include <stdint.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/time.h> 
#include <math.h>
#include <string.h>
//#include "address_map_arm_brl4.h"

// video display
#define SDRAM_BASE            0xC0000000
#define SDRAM_END             0xC3FFFFFF
#define SDRAM_SPAN			  0x04000000
// characters
#define FPGA_CHAR_BASE        0xC9000000 
#define FPGA_CHAR_END         0xC9001FFF
#define FPGA_CHAR_SPAN        0x00002000
/* Cyclone V FPGA devices */
#define HW_REGS_BASE          0xff200000
//#define HW_REGS_SPAN        0x00200000 
#define HW_REGS_SPAN          0x00005000 


#define PIO_RESET_OFFSET	  		0x00000000
#define PIO_COMPUTE_OFFSET    		0x00000010
#define PIO_DONE_OFFSET		  		0x00000020
#define PIO_FULL_RESET_OFFSET 		0x00000030
#define PIO_IMAGE_NORM_OFFSET		0x00000040
#define PIO_X_SHIFT_OFFSET			0x00000050
#define PIO_Y_SHIFT_OFFSET			0x00000060
#define PIO_UPDATE_DISPLAY_OFFSET	0x00000070

#define SDRAM_INPUT_SIZE      		0x00012C00
#define SDRAM_ACCUMULATOR_SIZE  	0x000E1000
#define SDRAM_DISPLAY_SIZE    		0x0004B000

#define SDRAM_DISPLAY_OFFSET  		0x00000000
#define SDRAM_ACCUMULATOR_OFFSET 	SDRAM_DISPLAY_OFFSET + SDRAM_DISPLAY_SIZE
#define SDRAM_INPUT_OFFSET			SDRAM_ACCUMULATOR_OFFSET + SDRAM_ACCUMULATOR_SIZE

#define DEFAULT_FOLDER "input_raw24_img"

// graphics primitives
void VGA_text (int, int, char *);
void VGA_text_clear();
void VGA_circle (int, int, int, int);
// 16-bit primary colors
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

// pixel macro
#define VGA_PIXEL(x,y,color) do{\
	int  *pixel_ptr ;\
	pixel_ptr = (int*)((char *)vga_pixel_ptr + (((y)*640+(x))<<1)) ; \
	*(uint32_t *)pixel_ptr = (color);\
} while(0)

#define FILE_TO_DISPLAY_COLOR(color) \
(((color & 0x000000FF) << 16) + (color & 0x0000FF00) + ((color & 0x00FF0000) >> 16))

int load_img(const char *filename, uint32_t *out_addr, int width, int height);
void compute_com(uint32_t *color_in, int width, int height, uint32_t *x_shift_out, uint32_t *y_shift_out);
int count_frames(const char *pattern);
void overlay_original(const char *filename, uint32_t *display_buf, int ox, int oy, int width, int height);
void run_drizzle(const char *folder, uint32_t *pixel_input_buffer);

// the light weight buss base
void *h2p_lw_virtual_base;

// pixel buffer
volatile unsigned int * vga_pixel_ptr = NULL ;
void *vga_pixel_virtual_base;

// character buffer
volatile unsigned int * vga_char_ptr = NULL ;
void *vga_char_virtual_base;

volatile uint32_t *pio_reset 			= NULL;
volatile uint32_t *pio_compute 			= NULL;
volatile uint32_t *pio_done 			= NULL;
volatile uint32_t *pio_full_reset 		= NULL;
volatile uint32_t *pio_image_norm 		= NULL;
volatile uint32_t *pio_x_shift 			= NULL;
volatile uint32_t *pio_y_shift 			= NULL;
volatile uint32_t *pio_update_display	= NULL;

// /dev/mem file id
int fd;

// measure time
struct timeval t1, t2;
double elapsedTime;

// command line input buffer
char input_buffer[128];
int j;
	
int main(int argc, char *argv[])
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
	vga_pixel_ptr =(uint32_t *)(vga_pixel_virtual_base);
	
	pio_reset 			= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_RESET_OFFSET));
	pio_compute 		= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_COMPUTE_OFFSET));
	pio_done 			= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_DONE_OFFSET));
	pio_full_reset 		= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_FULL_RESET_OFFSET));
	pio_image_norm 		= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_IMAGE_NORM_OFFSET));
	pio_x_shift 		= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_X_SHIFT_OFFSET));
	pio_y_shift 		= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_Y_SHIFT_OFFSET));
	pio_update_display 	= (volatile int32_t *)((char *)h2p_lw_virtual_base + (PIO_UPDATE_DISPLAY_OFFSET));
	// ===========================================

	// read raw 32-bit pixels into the VGA pixel buffer
	uint32_t *pixel_input_buffer = (uint32_t *)(vga_pixel_virtual_base) + (SDRAM_INPUT_OFFSET);

	int i;

	for (i = 0; i < SDRAM_INPUT_SIZE + SDRAM_ACCUMULATOR_SIZE + SDRAM_DISPLAY_SIZE; i++)
	{
		((uint32_t *)vga_pixel_virtual_base)[i] = 0x00000000;
	}

	// command line photo folder selection --> chooses which blurry set of images to drizzle
	// default input_raw24_img
	while (1) {
		printf("Enter command:\n");
		printf("s --> select folder and start drizzle\n");
		printf("d --> select default folder (%s) and start drizzle\n", DEFAULT_FOLDER);
		printf("c --> clear VGA screen\n");
		printf("q --> quit\n");
		fflush(stdout);
		j = scanf("%s", input_buffer);
		if (j != 1) continue;
		if (strcmp(input_buffer, "s") == 0) {
			printf("Enter folder name: ");
			fflush(stdout);
			j = scanf("%s", input_buffer);
			if (j != 1) {
				printf("ERROR: failed to read folder name\n");
				continue;
			}
			char pattern[256];
			sprintf(pattern, "%s/image_%%03d.bin", input_buffer);
			int nframes = count_frames(pattern);
			if (nframes == 0) {
				printf("ERROR: no frames found in '%s'\n", input_buffer);
				printf("  Expected: %s/image_001.bin, %s/image_002.bin, ...\n", input_buffer, input_buffer);
				continue;
			}
			printf("Found %d frames in '%s'\n", nframes, input_buffer);
			run_drizzle(input_buffer, pixel_input_buffer);
 
		} else if (strcmp(input_buffer, "d") == 0) {
			char pattern[256];
			sprintf(pattern, "%s/image_%%03d.bin", DEFAULT_FOLDER);
			int nframes = count_frames(pattern);
			if (nframes == 0) {
				printf("ERROR: no frames found in '%s'\n", DEFAULT_FOLDER);
				continue;
			}
			printf("Found %d frames in '%s'\n", nframes, DEFAULT_FOLDER);
			run_drizzle(DEFAULT_FOLDER, pixel_input_buffer);
		} else if (strcmp(input_buffer, "c") == 0) {
			printf("Clearing VGA screen...\n");
			for (i = 0; i < 640 * 480; i++) {
				((uint32_t *)vga_pixel_virtual_base)[i] = 0x00000000;
			}
			VGA_text_clear();
			printf("Screen cleared.\n");
		} else if (strcmp(input_buffer, "q") == 0) {
			printf("Exiting.\n");
			break;
		} else {
			printf("Unknown command: '%s'\n", input_buffer);
		}
	}
	close(fd);
	return 0;
} // end main

void run_drizzle(const char *folder, uint32_t *pixel_input_buffer) {
	char filename[256];
	char pattern[256];
	sprintf(pattern, "%s/image_%%03d.bin", folder);
 
	int total_frames = count_frames(pattern);
	if (total_frames == 0) {
		printf("ERROR: no frames to process\n");
		return;
	}
 
	// Clear all SDRAM regions
	int i;
	for (i = 0; i < SDRAM_INPUT_SIZE + SDRAM_ACCUMULATOR_SIZE + SDRAM_DISPLAY_SIZE; i++) {
		((uint32_t *)vga_pixel_virtual_base)[i] = 0x00000000;
	}
	VGA_text_clear();
 
	*pio_x_shift = 0;
	*pio_y_shift = 0;
 
	int img_num;
	int image_count = 0;
 
	printf("\nProcessing %d frames from '%s'...\n\n", total_frames, folder);
 
	struct timeval t_start, t_end;
	gettimeofday(&t_start, NULL);
 
	for (img_num = 1; img_num <= total_frames; img_num++) {
		gettimeofday(&t1, NULL);
		
		sprintf(filename, pattern, img_num);
		load_img(filename, pixel_input_buffer, 320, 240);
 
		// Compute COM on HPS
		int32_t x_shift, y_shift, x_com, y_com;
		compute_com(pixel_input_buffer, 320, 240, &x_com, &y_com);
 
		x_shift = (160 << 16) - x_com;
		y_shift = (120 << 16) - y_com;
 
		*pio_x_shift = x_shift;
		*pio_y_shift = y_shift;
 
		if (img_num == total_frames)
			*pio_update_display = 1;
		else
			*pio_update_display = 0;
 
		image_count = image_count + 1;
 
		*pio_compute = 1;
		*pio_image_norm = (1 << 16) / image_count;
		*pio_reset = 1;
 
		while (*pio_done) {}
		*pio_reset = 0;
		while (!(*pio_done)) {}
 
		gettimeofday(&t2, NULL);
		elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000000.0;
		elapsedTime += (t2.tv_usec - t1.tv_usec);
 
		printf("  Frame %3d/%d  (%.0f us)\n", img_num, total_frames, elapsedTime);
	}
 
	gettimeofday(&t_end, NULL);
	double total_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0;
	total_ms += (t_end.tv_usec - t_start.tv_usec) / 1000.0;
 
	printf("\nDrizzle complete! %d frames in %.1f ms (%.1f ms/frame)\n", 
		total_frames, total_ms, total_ms / total_frames);
 
	// original image show on top left of screen
	printf("Overlay original frame 1 for comparison\n");
 
	sprintf(filename, pattern, 1);
	uint32_t *display_buf = (uint32_t *)vga_pixel_virtual_base;
	overlay_original(filename, display_buf, 0, 0, 320, 240);
 
	// add a white border around overlayed original image 
	for (i = 0; i < 320; i++) {
		display_buf[i] = 0x00FFFFFF;
		display_buf[i + 239 * 640] = 0x00FFFFFF;
	}
	for (i = 0; i < 240; i++) {
		display_buf[i * 640] = 0x00FFFFFF;
		display_buf[319 + i * 640] = 0x00FFFFFF;
	}
 
	// VGA text labels
	VGA_text(2, 1, "ORIGINAL");
	VGA_text(42, 1, "DRIZZLE OUTPUT");
 
	printf("Done!\n");
	printf("Top left = original single frame (320x240)\n");
	printf("Full screen = drizzle superresolution (640x480)\n");
	fflush(stdout);
}
int load_img(const char *filename, uint32_t *out_addr, int width, int height)
{
	int x, y;
	uint32_t color;

	// === open 1 frame raw binary image ====================
	FILE *fp = fopen(filename, "rb");
	if (!fp) {
		printf("ERROR: raw binary image fail to open...\n");
		return(1);
	}

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			color = 0;

			if (fread(&color, 3, 1, fp) != 1) { // reads the pixel and check that the pixel was read
				printf( "ERROR: failed to read pixel\n");
				fclose(fp);
				return 1;
			}

			out_addr[x + y * 320] = FILE_TO_DISPLAY_COLOR(color);
		}
	}

	fclose(fp);

	return 0;
}

int count_frames(const char *pattern) {
	int count = 0;
	char filename[256];
	while (1) {
		sprintf(filename, pattern, count + 1); 
		FILE *fp = fopen(filename, "rb"); 
		if (!fp) break;
		fclose(fp); 
		count++;
	}
	return count;
}

void compute_com(uint32_t *color_in, int width, int height, uint32_t *com_x_out, uint32_t *com_y_out) {
	double sum_w = 0, sum_wx = 0, sum_wy = 0;
	int x, y;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			uint32_t color = color_in[x + y * width];
			uint32_t r = (color >> 16) & 0xFF; // after FILE_TO_DISPLAY_COLOR swap
			uint32_t g = (color >> 8) & 0xFF;
			uint32_t b = color & 0xFF;

			double brightness = r + g + b;
			if (brightness > 10.0) {
				sum_w += brightness;
				sum_wx += brightness * x;
				sum_wy += brightness * y;
			}
		}
	}
	if (sum_w > 0) {
		double float_com_x = sum_wx/sum_w;
		double float_com_y = sum_wy/sum_w;
		// convert the fractional part (the shift) to 11.16 fix pt
		// double frac_x = float_com_x - (int)float_com_x;
		// double frac_y = float_com_y - (int)float_com_y;
		*com_x_out = (uint32_t)(float_com_x * 65536.0); // 2^16
		*com_y_out = (uint32_t)(float_com_y * 65536.0);
		printf("COM calculation: (%.3f, %.3f), shift: (0x%08x, 0x%08x)\n", float_com_x, float_com_y, *com_x_out, *com_y_out);
	} else {
		*com_x_out = 0;
		*com_y_out = 0;
	}
}

void overlay_original(const char *filename, uint32_t *display_buf, int ox, int oy, int width, int height) {
	int x, y;
	uint32_t color;
 
	FILE *fp = fopen(filename, "rb");
	if (!fp) {
		printf("ERROR: could not open %s for overlay\n", filename);
		return;
	}
 
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			color = 0;
			if (fread(&color, 3, 1, fp) != 1) break;
 
			int dx = ox + x;
			int dy = oy + y;
			if (dx >= 0 && dx < 640 && dy >= 0 && dy < 480) {
				display_buf[dx + dy * 640] = FILE_TO_DISPLAY_COLOR(color);
			}
		}
	}
 
	fclose(fp);
}

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

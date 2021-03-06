/*
	What is it?
	------------------------------------------------------------------------

	Blur grayscale demo, including MMX implementation.

	Algorithm used here is described on my website (polish only).
	
	In short words: this is simple (and well known) average
	of 9 neighbour pixels.  However I've realized that
	blurring adjacent pixels (and scanlines) require fewer
	pixels fetches and fewer additions, because we can reuse
	some pixels fetched in previous iterations and partial
	sums calculated already.

	Compilation
	------------------------------------------------------------------------

	X11 view program

	$ gcc -IXscr -Iloadppm -DUSE_Xscr saturated_add.c Xscr/Xscr.c loadppm/load_ppm.c -lX11 -o program

	Additional libraries are available at github:
	* https://github.com/WojciechMula/Xscr
	* https://github.com/WojciechMula/loadppm


	Author: Wojciech Muła
	e-mail: wojciech_mula@poczta.onet.pl
	www:    http://0x80.pl/
	
	License: BSD
	
	initial release: 2007-07-14
	         update: 2008-06-08
	         update: 2013-10-06 - fixed MMX2
*/

#ifndef _XOPEN_SOURCE
#	define _XOPEN_SOURCE 600
#endif

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#define SUM_TABLE_SIZE		(1024*16)
#define MAX_GRAY_WIDTH		SUM_TABLE_SIZE
#define MAX_32bpp_WIDTH		(SUM_TABLE_SIZE/4)

uint16_t SUM_TABLE[3*SUM_TABLE_SIZE]__attribute__((aligned (16)));

uint8_t divide_lookup[255*9 + 1]; // sum \in [0..9*255]

void initialize_divide_lookup() {
	unsigned int i;
	for (i=0; i < sizeof(divide_lookup); i++)
		// divide_lookup[i] = i/9;
		divide_lookup[i] = (i * (65536/9) + (65536/9)) >> 16;
}


// C-language implementation

void
blur_gray_calc_sums(
	uint8_t  *src_img,
	uint8_t  border_color,
	uint16_t *sum_tbl,
	unsigned int width
) {
	unsigned x;
	uint8_t *pix;
	uint16_t a, b, c;

	pix = src_img;
	a = border_color;
	b = *pix++;
	for (x=0; x < width-1; x++) {
		c = *pix++;
		sum_tbl[x] = a + b + c;
		a = b;
		b = c;
	}

	c = border_color;
	sum_tbl[width-1] = a + b + c;
}


void
blur_gray_calc_avg(uint8_t *dst_img, unsigned int width) {
	unsigned x;
	uint8_t *pix;

	pix = dst_img;
	for (x=0; x < width; x++)
		*pix++ = divide_lookup[
			SUM_TABLE[x + 0*SUM_TABLE_SIZE] +
			SUM_TABLE[x + 1*SUM_TABLE_SIZE] +
			SUM_TABLE[x + 2*SUM_TABLE_SIZE]
		];
}



// x86 implementation

void
x86blur_gray_calc_sums(
	uint8_t  *src_img,
	uint8_t   border_color,
	uint16_t *sum_tbl,
	unsigned width
) {
	uint32_t dummy __attribute__((unused));

	__asm__ volatile (
	    // eax = border_color
	"   pushl %%eax                           \n\t"
	"   xorl %%ebx, %%ebx                     \n\t"
	"   xorl %%ecx, %%ecx                     \n\t"
	"                                         \n\t"
	"   movb (%%esi), %%bl                    \n\t" // b = pix[0]
	"   addl $1, %%esi                        \n\t"
	"                                         \n\t"
	"0:                                       \n\t"
	"   movb (%%esi), %%cl                    \n\t" // c = pix[x+1]
	"   addl %%ebx, %%eax                     \n\t" // a += b
	"   addl $1, %%esi                        \n\t" // pix++;
	"   addl %%ecx, %%eax                     \n\t" // a += c
	"                                         \n\t"
	"   movw %%ax, (%%edi)                    \n\t" // sum_tbl[x] = a
	"   movl %%ebx, %%eax                     \n\t" // a = b
	"   movl %%ecx, %%ebx                     \n\t" // b = c
	"                                         \n\t"
	"   addl $2, %%edi                        \n\t" // x++;
	"   subl $1, %%edx                        \n\t"
	"   jnz  0b                               \n\t"
	"                                         \n\t"
	"   popl %%ecx                            \n\t"
	"   add  %%ebx, %%eax                     \n\t"
	"   add  %%ecx, %%eax                     \n\t"
	"                                         \n\t"
	"   movw %%ax, (%%edi)                    \n\t"
	
	: "=S" (dummy), "=D" (dummy), "=d" (dummy), "=a" (dummy)
	: "S" (src_img), "D" (sum_tbl), "d" (width-1), "a" (border_color)
	: "%ebx", "%ecx", "memory"
	);
}

void
x86blur_gray_calc_avg(uint8_t *dst_img, unsigned int width) {
	uint32_t dummy __attribute__((unused));

	__asm__ volatile (
	"0:                                       \n\t"
	"   xorl %%eax, %%eax                     \n\t"
	"   movw         (%%esi), %%ax            \n\t" // ax  = SUM_TABLE[i]
	"   addw %c[ofs1](%%esi), %%ax            \n\t" // ax += SUM_TABLE[i + SUM_TABLE_SIZE]
	"   addw %c[ofs2](%%esi), %%ax            \n\t"	// ax += SUM_TABLE[i + 2*SUM_TABLE_SIZE]
	
	"   movb %c[dtbl](%%eax), %%al            \n\t" // bl = divide_lookup[ax]
	"   movb %%al, (%%edi)                    \n\t" // dst_img <- bl
	"                                         \n\t"
	"   addl $2, %%esi                        \n\t"
	"   addl $1, %%edi                        \n\t"
	"                                         \n\t"
	"   decl %%edx                            \n\t"
	"   jnz 0b                                \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=d" (dummy)
	: [ofs1] "e" (2*SUM_TABLE_SIZE), // SUM_TABLE + SUM_TABLE_SIZE
	  [ofs2] "e" (4*SUM_TABLE_SIZE), // SUM_TABLE + 2*SUM_TABLE_SIZE
	  [dtbl] "e" (divide_lookup),    // divtable address
	  "S" (SUM_TABLE), "D" (dst_img), "d" (width)
	: "%eax", "memory"
	);
}


// MMX implementation

void
mmxblur_gray_calc_sums(
	uint8_t  *src_img,
	uint8_t   border_color,
	uint16_t *sum_tbl,
	unsigned int width
) {
	uint32_t dummy __attribute__((unused));

	__asm__ volatile(
	"   pxor %%mm7, %%mm7                     \n\t"
	"0:                                       \n\t"
	"   movq 0(%%esi), %%mm0                  \n\t"
	"   movq 1(%%esi), %%mm1                  \n\t"
	"   movq 2(%%esi), %%mm2                  \n\t"
	"                                         \n\t"
	"   movq %%mm0, %%mm3                     \n\t"
	"   movq %%mm1, %%mm4                     \n\t"
	"   movq %%mm2, %%mm5                     \n\t"
	"                                         \n\t"
	"   punpcklbw %%mm7, %%mm0                \n\t"
	"   punpcklbw %%mm7, %%mm1                \n\t"
	"   punpcklbw %%mm7, %%mm2                \n\t"
	"                                         \n\t"
	"   punpckhbw %%mm7, %%mm3                \n\t"
	"   punpckhbw %%mm7, %%mm4                \n\t"
	"   punpckhbw %%mm7, %%mm5                \n\t"
	"                                         \n\t"
	"   paddw %%mm1, %%mm0                    \n\t"
	"   paddw %%mm2, %%mm0                    \n\t"
	"                                         \n\t"
	"   paddw %%mm4, %%mm3                    \n\t"
	"   paddw %%mm5, %%mm3                    \n\t"
	"                                         \n\t"
	"   movq %%mm0,  0(%%edi)                 \n\t"
	"   movq %%mm3,  8(%%edi)                 \n\t"
	"                                         \n\t"
	"   addl $8,  %%esi                       \n\t"
	"   addl $16, %%edi                       \n\t"
	"                                         \n\t"
	"   subl $1, %%ecx                        \n\t"
	"   jnz  0b                               \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=c" (dummy)
	: "S" (src_img), "D" (sum_tbl+1), "c" (width/8)
	: "memory"
	);

	// fix sum for first pixel:
	sum_tbl[0]  = sum_tbl[1];
	sum_tbl[0] -= (uint16_t)src_img[0];
	sum_tbl[0] += (uint16_t)border_color;
	
	// fix sum for last pixel:
	sum_tbl[width] -= (uint16_t)src_img[width];
	sum_tbl[width] += (uint16_t)border_color;
}

// MMX[2] implementation

void
mmx2blur_gray_calc_sums(
	uint8_t  *src_img,
	uint8_t   border_color,
	uint16_t *sum_tbl,
	unsigned int width
) {
	uint32_t dummy __attribute__((unused));

	__asm__ volatile(
	"   pxor %%mm7, %%mm7                     \n\t"
	"   movq 0(%%esi), %%mm6                  \n\t"
	"0:                                       \n\t"
	"   movq %%mm6, %%mm0                     \n\t"
	"   movq 8(%%esi), %%mm3                  \n\t"
	"   movq %%mm3, %%mm6                     \n\t"
	"                                         \n\t"
	"   movq %%mm0, %%mm1                     \n\t"
	"   movq %%mm0, %%mm2                     \n\t"
	"   movq %%mm3, %%mm4                     \n\t"
	"                                         \n\t"
	"   psrlq  $8, %%mm1                      \n\t"
	"   psrlq $16, %%mm2                      \n\t"
	"   psllq $56, %%mm3                      \n\t"
	"   psllq $48, %%mm4                      \n\t"
	"                                         \n\t"
	"   por %%mm3, %%mm1                      \n\t"
	"   por %%mm4, %%mm2                      \n\t"
	"                                         \n\t"
	"   movq %%mm0, %%mm3                     \n\t"
	"   movq %%mm1, %%mm4                     \n\t"
	"   movq %%mm2, %%mm5                     \n\t"
	"                                         \n\t"
	"   punpcklbw %%mm7, %%mm0                \n\t"
	"   punpcklbw %%mm7, %%mm1                \n\t"
	"   punpcklbw %%mm7, %%mm2                \n\t"
	"                                         \n\t"
	"   punpckhbw %%mm7, %%mm3                \n\t"
	"   punpckhbw %%mm7, %%mm4                \n\t"
	"   punpckhbw %%mm7, %%mm5                \n\t"
	"                                         \n\t"
	"   paddw %%mm1, %%mm0                    \n\t"
	"   paddw %%mm2, %%mm0                    \n\t"
	"                                         \n\t"
	"   paddw %%mm4, %%mm3                    \n\t"
	"   paddw %%mm5, %%mm3                    \n\t"
	"                                         \n\t"
	"   movq %%mm0,  0(%%edi)                 \n\t"
	"   movq %%mm3,  8(%%edi)                 \n\t"
	"                                         \n\t"
	"   addl $8,  %%esi                       \n\t"
	"   addl $16, %%edi                       \n\t"
	"                                         \n\t"
	"   subl $1, %%ecx                        \n\t"
	"   jnz  0b                               \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=c" (dummy)
	: "S" (src_img), "D" (sum_tbl+1), "c" (width/8)
	: "memory"
	);

	// fix sum for first pixel:
	sum_tbl[0]  = sum_tbl[1];
	sum_tbl[0] -= (uint16_t)src_img[0];
	sum_tbl[0] += (uint16_t)border_color;
	
	// fix sum for last pixel:
	sum_tbl[width] -= (uint16_t)src_img[width];
	sum_tbl[width] += (uint16_t)border_color;
}


void
mmxblur_gray_calc_avg(uint8_t *dst_img, unsigned int width) {
	uint32_t dummy __attribute__((unused));
	static uint16_t mul_const[4] = {65536/9, 65536/9, 65536/9, 65536/9};

	__asm__ volatile (
	"  movq (%%eax), %%mm7                    \n\t"
	"0:                                       \n\t"
	"  movq          (%%esi), %%mm0           \n\t"
	"  paddw %c[ofs1](%%esi), %%mm0           \n\t"
	"  paddw %c[ofs2](%%esi), %%mm0           \n\t"
	"                                         \n\t"
	"  movq           8(%%esi), %%mm1         \n\t"
	"  paddw %c[ofs1]+8(%%esi), %%mm1         \n\t"
	"  paddw %c[ofs2]+8(%%esi), %%mm1         \n\t"
	"                                         \n\t"
	"  pmulhw %%mm7, %%mm0                    \n\t"
	"  pmulhw %%mm7, %%mm1                    \n\t"
	"  packuswb %%mm1, %%mm0                  \n\t"
	"  movq %%mm0, (%%edi)                    \n\t"
	"                                         \n\t"
	"  addl $16, %%esi                        \n\t"
	"  addl $8,  %%edi                        \n\t"
	"                                         \n\t"
	"  dec %%ecx                              \n\t"
	"  jnz 0b                                 \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=c" (dummy)
	: [ofs1] "e" (2*SUM_TABLE_SIZE), // SUM_TABLE + SUM_TABLE_SIZE
	  [ofs2] "e" (4*SUM_TABLE_SIZE), // SUM_TABLE + 2*SUM_TABLE_SIZE
	  "S" (SUM_TABLE),
	  "D" (dst_img),
	  "a" (mul_const),
	  "c" (width/8)
	: "memory"
	);
}

// SSE2 implementation

void
sse2blur_gray_calc_sums(
	uint8_t  *src_img,
	uint8_t   border_color,
	uint16_t *sum_tbl,
	unsigned int width
) {
	uint32_t dummy __attribute__((unused));

	__asm__ volatile(
	"   pxor %%xmm7, %%xmm7                   \n\t"
	"   movdqu 0(%%esi), %%xmm6               \n\t"
	"0:                                       \n\t"
	"   movdqa %%xmm6, %%xmm0                 \n\t"
	"   movdqu 16(%%esi), %%xmm6              \n\t"
	"   movdqa %%xmm6, %%xmm1                 \n\t"
	"   movdqa %%xmm6, %%xmm2                 \n\t"
	"                                         \n\t"
	"   palignr $1, %%xmm0, %%xmm1            \n\t"
	"   palignr $2, %%xmm0, %%xmm2            \n\t"
	"                                         \n\t"
	"   movdqa %%xmm0, %%xmm3                 \n\t"
	"   movdqa %%xmm1, %%xmm4                 \n\t"
	"   movdqa %%xmm2, %%xmm5                 \n\t"
	"                                         \n\t"
	"   punpcklbw %%xmm7, %%xmm0              \n\t"
	"   punpcklbw %%xmm7, %%xmm1              \n\t"
	"   punpcklbw %%xmm7, %%xmm2              \n\t"
	"                                         \n\t"
	"   punpckhbw %%xmm7, %%xmm3              \n\t"
	"   punpckhbw %%xmm7, %%xmm4              \n\t"
	"   punpckhbw %%xmm7, %%xmm5              \n\t"
	"                                         \n\t"
	"   paddw %%xmm1, %%xmm0                  \n\t"
	"   paddw %%xmm2, %%xmm0                  \n\t"
	"                                         \n\t"
	"   paddw %%xmm4, %%xmm3                  \n\t"
	"   paddw %%xmm5, %%xmm3                  \n\t"
	"                                         \n\t"
	"   movdqa %%xmm0,  0(%%edi)              \n\t"
	"   movdqa %%xmm3, 16(%%edi)              \n\t"
	"                                         \n\t"
	"   addl $16, %%esi                       \n\t"
	"   addl $32, %%edi                       \n\t"
	"                                         \n\t"
	"   subl $1, %%ecx                        \n\t"
	"   jnz  0b                               \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=c" (dummy)
	: "S" (src_img), "D" (sum_tbl), "c" (width/16)
	: "memory"
	);

	// fix sum for first pixel:
	sum_tbl[0]  = sum_tbl[1];
	sum_tbl[0] -= (uint16_t)src_img[0];
	sum_tbl[0] += (uint16_t)border_color;
	
	// fix sum for last pixel:
	sum_tbl[width] -= (uint16_t)src_img[width];
	sum_tbl[width] += (uint16_t)border_color;
}


void
sse2blur_gray_calc_avg(uint8_t *dst_img, unsigned int width) {
	uint32_t dummy __attribute__((unused));
	static uint16_t mul_const[8] = {65536/9, 65536/9, 65536/9, 65536/9, 65536/9, 65536/9, 65536/9, 65536/9};

	__asm__ volatile(
	"  movdqu (%%eax), %%xmm7                 \n\t"
	"0:                                       \n\t"
	"  movdqa         (%%esi), %%xmm0         \n\t"
	"  paddw  %c[ofs1](%%esi), %%xmm0         \n\t"
	"  paddw  %c[ofs2](%%esi), %%xmm0         \n\t"
	"                                         \n\t"
	"  movdqa         16(%%esi), %%xmm1       \n\t"
	"  paddw %c[ofs2]+16(%%esi), %%xmm1       \n\t"
	"  paddw %c[ofs2]+16(%%esi), %%xmm1       \n\t"
	"                                         \n\t"
	"  pmulhw %%xmm7, %%xmm0                  \n\t"
	"  pmulhw %%xmm7, %%xmm1                  \n\t"
	"  packuswb %%xmm1, %%xmm0                \n\t"
	"  movdqa %%xmm0, (%%edi)                 \n\t"
	"                                         \n\t"
	"  addl $32, %%esi                        \n\t"
	"  addl $16, %%edi                        \n\t"
	"                                         \n\t"
	"  dec %%ecx                              \n\t"
	"  jnz 0b                                 \n\t"
	"                                         \n\t"
	: "=S" (dummy), "=D" (dummy), "=c" (dummy)
	: [ofs1] "e" (2*SUM_TABLE_SIZE), // SUM_TABLE + SUM_TABLE_SIZE
	  [ofs2] "e" (4*SUM_TABLE_SIZE), // SUM_TABLE + 2*SUM_TABLE_SIZE
	  "S" (SUM_TABLE),
	  "D" (dst_img),
	  "a" (mul_const),
	  "c" (width/16)
	: "memory"
	);
}

// scheme
#define define_blur_gray_img_fun(fun_name, sum_fun_name, avg_fun_name)\
void fun_name(													\
	uint8_t *src_img,											\
	uint8_t *dst_img,											\
	unsigned int width,											\
	unsigned int height,										\
	uint8_t border_color										\
) {																\
	unsigned y, i;												\
	int offset = 2*SUM_TABLE_SIZE;								\
																\
	for (i=0; i < width; i++)									\
		SUM_TABLE[i] = border_color*3;							\
																\
	sum_fun_name(												\
		src_img,												\
		border_color,											\
		&SUM_TABLE[SUM_TABLE_SIZE],								\
		width													\
	);															\
																\
	for (y=0; y < height-1; y++) {								\
		sum_fun_name(											\
			src_img + (y+1)*width,								\
			border_color,										\
			&SUM_TABLE[offset],									\
			width												\
		);														\
		avg_fun_name(											\
			dst_img + (y*width),								\
			width												\
		);														\
		offset += SUM_TABLE_SIZE;								\
		if (offset == 3*SUM_TABLE_SIZE)							\
			offset = 0;											\
	}															\
																\
	for (i=0; i < width; i++)									\
		SUM_TABLE[i] = border_color*3;							\
																\
	avg_fun_name(												\
		dst_img + (y*width),									\
		width													\
	);															\
}


define_blur_gray_img_fun(
	blur_gray_img,
	blur_gray_calc_sums,
	blur_gray_calc_avg
)


define_blur_gray_img_fun(
	x86blur_gray_img,
	x86blur_gray_calc_sums,
	x86blur_gray_calc_avg
)


define_blur_gray_img_fun(
	mmxblur_gray_img,
	mmxblur_gray_calc_sums,
	mmxblur_gray_calc_avg
)


define_blur_gray_img_fun(
	mmx2blur_gray_img,
	mmx2blur_gray_calc_sums,
	mmxblur_gray_calc_avg
)


define_blur_gray_img_fun(
	sse2blur_gray_img,
	sse2blur_gray_calc_sums,
	sse2blur_gray_calc_avg
)


#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef USE_Xscr
#	include "Xscr.h"
#	include "load_ppm.h"
#endif

#define IMG_WIDTH	640
#define IMG_HEIGHT	480
#define PIX_COUNT	(IMG_WIDTH*IMG_HEIGHT)

typedef enum {C_lang, x86, MMX, MMX2, SSE2} Function;

Function current = C_lang;
uint8_t gray_border_color = 0x00;

uint8_t *data;
uint8_t *img;

void die(char* info, ...) {
	va_list ap;

	va_start(ap, info);
	vfprintf(stderr, info, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	exit(EXIT_FAILURE);
}

#ifdef USE_Xscr
void Xhelp() {
	puts(
"b/space - blur image using current blur routine\n"
"1 - select routine 1 (C language reference implementation)\n"
"2 - select routine 2 (assembler x86)\n"
"3 - select routine 3 (assembler MMX)\n"
"4 - select routine 4 (assembler MMX2)\n"
"5 - select routine 4 (assembler SSE2)\n"
"r - revert image\n"
"\n"
"q - quit\n"
	);
}

void keyboard(int x, int y, Time t, KeySym c, KeyOrButtonState s, unsigned int kb_mask) {
	if (s == Released) return;

	switch (c) {
		case XK_q:
		case XK_Q:
		case XK_Escape:
			Xscr_quit();
			break;

		case XK_r:
		case XK_R:
			memcpy( (void*)img, (void*)data, PIX_COUNT);
			Xscr_redraw();
			break;

		case XK_1:
			current = C_lang;
			break;
		
		case XK_2:
			current = x86;
			break;
		
		case XK_3:
			current = MMX;
			break;

		case XK_4:
			current = MMX2;
			break;

		case XK_5:
			current = SSE2;
			break;

		case XK_space:
		case XK_b:
		case XK_B:
			switch (current) {
				case C_lang:
					blur_gray_img(img, img, IMG_WIDTH, IMG_HEIGHT, gray_border_color);
					break;
				case x86:
					x86blur_gray_img(img, img, IMG_WIDTH, IMG_HEIGHT, gray_border_color);
					break;
				case MMX:
					mmxblur_gray_img(img, img, IMG_WIDTH, IMG_HEIGHT, gray_border_color);
					break;
				case MMX2:
					mmx2blur_gray_img(img, img, IMG_WIDTH, IMG_HEIGHT, gray_border_color);
					break;
				case SSE2:
					sse2blur_gray_img(img, img, IMG_WIDTH, IMG_HEIGHT, gray_border_color);
					break;
			}
				     
			Xscr_redraw();
			break;
	}
}
#endif

void usage() {
	puts(
"Usage:\n"
"\n"
"progname test x86|mmx|mmx2|sse2 count\n"
"\n"
"   Run x86 or mmx routine count times\n"
"\n"
#ifdef USE_Xscr
"\n"
"progname view file_ppm_640x480\n"
"\n"
"   Display in X Window given file and blur using\n"
"   selected routine.  See help message printed\n"
"   on console for details.\n"
#endif
	);
}

int main(int argc, char* argv[]) {

#ifdef USE_Xscr
	FILE* f;
	int width, height, maxval, result;
#endif
	int count;

#define iskeyword(string, index) (strcasecmp(argv[index], string) == 0)

	// progname test x86|mmx|mmx2 count
	if (argc >= 4 && iskeyword("test", 1)) {
		count = atoi(argv[3]) <= 0 ? 100 : atoi(argv[3]);
		if (iskeyword("MMX", 2)) {
			posix_memalign((void*)&data, 16, PIX_COUNT);
			if (!data) die("No free memory");

			printf("repeat count: %d\n", count);
			while (count--)
				mmxblur_gray_img(data, data, IMG_WIDTH, IMG_HEIGHT, 0x00);

			free(data);
			return 0;
		}
		else
		if (iskeyword("MMX2", 2)) {
			posix_memalign((void*)&data, 16, PIX_COUNT);
			if (!data) die("No free memory");

			printf("repeat count: %d\n", count);
			while (count--)
				mmx2blur_gray_img(data, data, IMG_WIDTH, IMG_HEIGHT, 0x00);

			free(data);
			return 0;
		}
		else
		if (iskeyword("SSE2", 2)) {
			posix_memalign((void*)&data, 16, PIX_COUNT);
			if (!data) die("No free memory");

			printf("repeat count: %d\n", count);
			while (count--)
				sse2blur_gray_img(data, data, IMG_WIDTH, IMG_HEIGHT, 0x00);

			free(data);
			return 0;
		}
		else
		if (iskeyword("x86", 2)) {
			posix_memalign((void*)&data, 16, PIX_COUNT);
			if (!data) die("No free memory");
			
			printf("repeat count: %d\n", count);
			while (count--)
				x86blur_gray_img(data, data, IMG_WIDTH, IMG_HEIGHT, 0x00);

			free(data);
			return 0;
		}
		else {
			usage();
			return 1;
		}

	}
#ifdef USE_Xscr
	else
	// progname view file_ppm_640x480
	if (argc >= 3 && iskeyword("view", 1)) {
		f = fopen(argv[2], "rb");
		if (!f)
			die("Can't open file %s\n", argv[2]);
		
		result = ppm_load_gray(f, &width, &height, &maxval, &data, 0, Weigted);
		fclose(f);
		if (result < 0)
			die("PPM error: %s\n", PPM_errormsg[-result]);
		
		if (width != IMG_WIDTH || height != IMG_HEIGHT)
			die("Image %dx%d required", IMG_WIDTH, IMG_HEIGHT);

		posix_memalign((void*)&img, 16, PIX_COUNT);
		if (!img)
			die("No free memory");
		
		memcpy((void*)img, (void*)data, PIX_COUNT);

		initialize_divide_lookup();

		Xhelp();
		result = Xscr_mainloop(
			IMG_WIDTH,
			IMG_HEIGHT,
			DEPTH_gray,
			False,
			img,
			keyboard, NULL, NULL,
			"Blur grayscale images demo"
		);
		if (result < 0)
			die("Xscr error: %s\n", Xscr_error_str(result));
		else
			return 0;
	}
#endif
	else {
		// print usage
		usage();
		return 1;
	}

	return 0;
}

/*
vim: ts=4 sw=4 nowrap noexpandtab
*/

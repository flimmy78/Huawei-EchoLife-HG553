/*      video_common.c
 *
 *      Video stream functions for motion.
 *      Copyright 2000 by Jeroen Vreeken (pe1rxq@amsat.org)
 *      	  2006 by Krzysztof Blaszkowski (kb@sysmikro.com.pl)	
 *      	  2007 by Angel Carpinteo (ack@telefonica.net)
 *      This software is distributed under the GNU public license version 2
 *      See also the file 'COPYING'.
 *
 */

#include "motion.h"
#include "video.h"
/* for rotation */
#include "rotate.h"

#ifdef MJPEGT 
#include <mjpegtools/jpegutils.h>
#include <mjpegtools/mjpeg_types.h>
#endif


typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;

#define CLAMP(x)        ((x)<0?0:((x)>255)?255:(x))
//add by z67625
#define CLIP(color) (unsigned char)(((color)>0xFF)?0xff:(((color)<0)?0:(color)))
#ifndef MDEBUG
#define MDEBUG if(debug) printf("%s: %s %d:",__FILE__, __FUNCTION__, __LINE__); \
              if(debug) printf
#endif
#ifndef MLOG
#define MLOG if(debug) printf
#endif
extern int debug;


typedef struct {
	int is_abs;
	int len;
	int val;
} code_table_t;

/*
 *   sonix_decompress_init
 *   =====================
 *   pre-calculates a locally stored table for efficient huffman-decoding.
 *
 *   Each entry at index x in the table represents the codeword
 *   present at the MSB of byte x.
 *
 */
static void sonix_decompress_init(code_table_t * table)
{
	int i;
	int is_abs, val, len;

	for (i = 0; i < 256; i++) {
		is_abs = 0;
		val = 0;
		len = 0;
		if ((i & 0x80) == 0) {
			/* code 0 */
			val = 0;
			len = 1;
		} else if ((i & 0xE0) == 0x80) {
			/* code 100 */
			val = +4;
			len = 3;
		} else if ((i & 0xE0) == 0xA0) {
			/* code 101 */
			val = -4;
			len = 3;
		} else if ((i & 0xF0) == 0xD0) {
			/* code 1101 */
			val = +11;
			len = 4;
		} else if ((i & 0xF0) == 0xF0) {
			/* code 1111 */
			val = -11;
			len = 4;
		} else if ((i & 0xF8) == 0xC8) {
			/* code 11001 */
			val = +20;
			len = 5;
		} else if ((i & 0xFC) == 0xC0) {
			/* code 110000 */
			val = -20;
			len = 6;
		} else if ((i & 0xFC) == 0xC4) {
			/* code 110001xx: unknown */
			val = 0;
			len = 8;
		} else if ((i & 0xF0) == 0xE0) {
			/* code 1110xxxx */
			is_abs = 1;
			val = (i & 0x0F) << 4;
			len = 8;
		}
		table[i].is_abs = is_abs;
		table[i].val = val;
		table[i].len = len;
	}
}

/*
 *   sonix_decompress
 *   ================
 *   decompresses an image encoded by a SN9C101 camera controller chip.
 *
 *   IN    width
 *         height
 *         inp     pointer to compressed frame (with header already stripped)
 *   OUT   outp    pointer to decompressed frame
 *
 *         Returns 0 if the operation was successful.
 *         Returns <0 if operation failed.
 *
 */
int sonix_decompress(unsigned char *outp, unsigned char *inp, int width, int height)
{
	int row, col;
	int val;
	int bitpos;
	unsigned char code;
	unsigned char *addr;

	/* local storage */
	static code_table_t table[256];
	static int init_done = 0;

	if (!init_done) {
		init_done = 1;
		sonix_decompress_init(table);
		/* do sonix_decompress_init first! */
		//return -1; // so it has been done and now fall through
	}

	bitpos = 0;
	for (row = 0; row < height; row++) {

		col = 0;

		/* first two pixels in first two rows are stored as raw 8-bit */
		if (row < 2) {
			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
			bitpos += 8;
			*outp++ = code;

			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
			bitpos += 8;
			*outp++ = code;

			col += 2;
		}

		while (col < width) {
			/* get bitcode from bitstream */
			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));

			/* update bit position */
			bitpos += table[code].len;

			/* calculate pixel value */
			val = table[code].val;
			if (!table[code].is_abs) {
				/* value is relative to top and left pixel */
				if (col < 2) {
					/* left column: relative to top pixel */
					val += outp[-2 * width];
				} else if (row < 2) {
					/* top row: relative to left pixel */
					val += outp[-2];
				} else {
					/* main area: average of left pixel and top pixel */
					val += (outp[-2] + outp[-2 * width]) / 2;
				}
			}

			/* store pixel */
			*outp++ = CLAMP(val);
			col++;
		}
	}

	return 0;
}

/*
 * BAYER2RGB24 ROUTINE TAKEN FROM:
 *
 * Sonix SN9C10x based webcam basic I/F routines
 * Takafumi Mizuno <taka-qce@ls-a.jp>
 *
 */

void bayer2rgb24(unsigned char *dst, unsigned char *src, long int width, long int height)
{
	long int i;
	unsigned char *rawpt, *scanpt;
	long int size;

	rawpt = src;
	scanpt = dst;
	size = width * height;

	for (i = 0; i < size; i++) {
		if (((i / width) & 1) == 0) {	// %2 changed to & 1
			if ((i & 1) == 0) {
				/* B */
				if ((i > width) && ((i % width) > 0)) {
					*scanpt++ = *rawpt;     /* B */
					*scanpt++ = (*(rawpt - 1) + *(rawpt + 1) + *(rawpt + width) + *(rawpt - width)) / 4;	/* G */
					*scanpt++ = (*(rawpt - width - 1) + *(rawpt - width + 1) + *(rawpt + width - 1) + *(rawpt + width + 1)) / 4;    /* R */
				} else {
					/* first line or left column */
					*scanpt++ = *rawpt;     /* B */
					*scanpt++ = (*(rawpt + 1) + *(rawpt + width)) / 2;	/* G */
					*scanpt++ = *(rawpt + width + 1);       /* R */
				}
			} else {
				/* (B)G */
				if ((i > width) && ((i % width) < (width - 1))) {
					*scanpt++ = (*(rawpt - 1) + *(rawpt + 1)) / 2;  /* B */
					*scanpt++ = *rawpt;	/* G */
					*scanpt++ = (*(rawpt + width) + *(rawpt - width)) / 2;  /* R */
				} else {
					/* first line or right column */
					*scanpt++ = *(rawpt - 1);       /* B */
					*scanpt++ = *rawpt;	/* G */
					*scanpt++ = *(rawpt + width);   /* R */
				}
			}
		} else {
			if ((i & 1) == 0) {
				/* G(R) */
				if ((i < (width * (height - 1))) && ((i % width) > 0)) {
					*scanpt++ = (*(rawpt + width) + *(rawpt - width)) / 2;  /* B */
					*scanpt++ = *rawpt;	/* G */
					*scanpt++ = (*(rawpt - 1) + *(rawpt + 1)) / 2;  /* R */
				} else {
					/* bottom line or left column */
					*scanpt++ = *(rawpt - width);   /* B */
					*scanpt++ = *rawpt;	/* G */
					*scanpt++ = *(rawpt + 1);       /* R */
				}
			} else {
				/* R */
				if (i < (width * (height - 1)) && ((i % width) < (width - 1))) {
					*scanpt++ = (*(rawpt - width - 1) + *(rawpt - width + 1) + *(rawpt + width - 1) + *(rawpt + width + 1)) / 4;    /* B */
					*scanpt++ = (*(rawpt - 1) + *(rawpt + 1) + *(rawpt - width) + *(rawpt + width)) / 4;	/* G */
					*scanpt++ = *rawpt;     /* R */
				} else {
					/* bottom line or right column */
					*scanpt++ = *(rawpt - width - 1);       /* B */
					*scanpt++ = (*(rawpt - 1) + *(rawpt - width)) / 2;	/* G */
					*scanpt++ = *rawpt;     /* R */
				}
			}
		}
		rawpt++;
	}

}

void conv_yuv422to420p(unsigned char *map, unsigned char *cap_map, int width, int height)
{
	unsigned char *src, *dest, *src2, *dest2;
	int i, j;

	/* Create the Y plane */
	src = cap_map;
	dest = map;
	for (i = width * height; i > 0; i--) {
		*dest++ = *src;
		src += 2;
	}
	/* Create U and V planes */
	src = cap_map + 1;
	src2 = cap_map + width * 2 + 1;
	dest = map + width * height;
	dest2 = dest + (width * height) / 4;
	for (i = height / 2; i > 0; i--) {
		for (j = width / 2; j > 0; j--) {
			*dest = ((int) *src + (int) *src2) / 2;
			src += 2;
			src2 += 2;
			dest++;
			*dest2 = ((int) *src + (int) *src2) / 2;
			src += 2;
			src2 += 2;
			dest2++;
		}
		src += width * 2;
		src2 += width * 2;
	}
}

void conv_uyvyto420p(unsigned char *map, unsigned char *cap_map, unsigned int width, unsigned int height)
{
	uint8_t *pY = map;
	uint8_t *pU = pY + (width * height);
	uint8_t *pV = pU + (width * height)/4;
	uint32_t uv_offset = width * 4 * sizeof(uint8_t);
	uint32_t ix, jx;

	for (ix = 0; ix < height; ix++) {
		for (jx = 0; jx < width; jx += 2) {
			uint16_t calc;
			if ((ix&1) == 0) {
				calc = *cap_map;
				calc += *(cap_map + uv_offset);
				calc /= 2;
				*pU++ = (uint8_t) calc;
			}
			cap_map++;
			*pY++ = *cap_map++;
			if ((ix&1) == 0) {
				calc = *cap_map;
				calc += *(cap_map + uv_offset);
				calc /= 2;
				*pV++ = (uint8_t) calc;
			}
			cap_map++;
			*pY++ = *cap_map++;
		}
	}
}



void conv_rgb24toyuv420p(unsigned char *map, unsigned char *cap_map, int width, int height)
{

    unsigned int i, j, size;
  unsigned char R, G, B, R1, G1, B1, Rd, Gd, Bd, Rd1, Gd1, Bd1;
  int Y, Yd, Y11, Yd1, Cb, Cr;
  unsigned char * inbuf = cap_map;
  unsigned char * inbuf1 = cap_map + (width * 3);
  unsigned char *output = map;
  size = width * height >> 2;
  for (i = size, j = 0; i > 0; i--)
    
    {
      B = inbuf[0];
      G = inbuf[1];
      R = inbuf[2];
      B1 = inbuf[3];
      G1 = inbuf[4];
      R1 = inbuf[5];
      Bd = inbuf1[0];
      Gd = inbuf1[1];
      Rd = inbuf1[2];
      Bd1 = inbuf1[3];
      Gd1 = inbuf1[4];
      Rd1 = inbuf1[5];
      inbuf += 6;
      inbuf1 += 6;
      j++;
      if (j >= width / 2)
	{
	  j = 0;
	  inbuf += (width * 3);
	  inbuf1 += (width * 3);
	}
      Y = CLIP ((77 * R + 150 * G + 29 * B) >> 8);
      Y11 = CLIP ((77 * R1 + 150 * G1 + 29 * B1) >> 8);
      Yd = CLIP ((77 * Rd + 150 * Gd + 29 * Bd) >> 8);
      Yd1 = CLIP ((77 * Rd1 + 150 * Gd1 + 29 * Bd1) >> 8);
      Cb = CLIP (((-43 * R - 85 * G + 128 * B) >> 8) + 128);
      Cr = CLIP (((128 * R - 107 * G - 21 * B) >> 8) + 128);
      *cap_map++ = (unsigned char) Y;
      *cap_map++ = (unsigned char) Y11;
      *cap_map++ = (unsigned char) Yd;
      *cap_map++ = (unsigned char) Yd1;
      *cap_map++ = (unsigned char) Cb;
      *cap_map++ = (unsigned char) Cr;
      *output++ = (unsigned char)Y;
      *output++ = (unsigned char) Y11;
      *output++ = (unsigned char) Yd;
      *output++ = (unsigned char) Yd1;
      *output++ = (unsigned char) Cb;
      *output++ = (unsigned char) Cr;
      
    }

#if 0
	unsigned char *y, *u, *v;
	unsigned char *r, *g, *b;
	int i, loop;

	b = cap_map;
	g = b + 1;
	r = g + 1;
	y = map;
	u = y + width * height;
	v = u + (width * height) / 4;
	memset(u, 0, width * height / 4);
	memset(v, 0, width * height / 4);

	for (loop = 0; loop < height; loop++) {
		for (i = 0; i < width; i += 2) {
			*y++ = (9796 ** r + 19235 ** g + 3736 ** b) >> 15;
			*u += ((-4784 ** r - 9437 ** g + 14221 ** b) >> 17) + 32;
			*v += ((20218 ** r - 16941 ** g - 3277 ** b) >> 17) + 32;
			r += 3;
			g += 3;
			b += 3;
			*y++ = (9796 ** r + 19235 ** g + 3736 ** b) >> 15;
			*u += ((-4784 ** r - 9437 ** g + 14221 ** b) >> 17) + 32;
			*v += ((20218 ** r - 16941 ** g - 3277 ** b) >> 17) + 32;
			r += 3;
			g += 3;
			b += 3;
			u++;
			v++;
		}

		if ((loop & 1) == 0) {
			u -= width / 2;
			v -= width / 2;
		}
	}
#endif
}

int conv_jpeg2yuv420(struct context *cnt, unsigned char *dst, netcam_buff * buff, int width, int height)
{
	netcam_context netcam;

	if (!buff || !dst)
		return 3;

	if (!buff->ptr)
		return 2;	/* Error decoding MJPEG frame */

	memset(&netcam, 0, sizeof(netcam));
	netcam.imgcnt_last = 1;
	netcam.latest = buff;
	netcam.width = width;
	netcam.height = height;
	netcam.cnt = cnt;

	pthread_mutex_init(&netcam.mutex, NULL);
	pthread_cond_init(&netcam.cap_cond, NULL);
	pthread_cond_init(&netcam.pic_ready, NULL);
	pthread_cond_init(&netcam.exiting, NULL);

	if (setjmp(netcam.setjmp_buffer)) {
		return NETCAM_GENERAL_ERROR | NETCAM_JPEG_CONV_ERROR;
	}

	return netcam_proc_jpeg(&netcam, dst);
}


#ifdef MJPEGT 
void mjpegtoyuv420p(unsigned char *map, unsigned char *cap_map, int width, int height, unsigned int size)
{
	uint8_t *yuv[3];
	unsigned char *y, *u, *v;
	int loop;

	yuv[0] = malloc(width * height * sizeof(yuv[0][0]));
	yuv[1] = malloc(width * height / 4 * sizeof(yuv[1][0]));
	yuv[2] = malloc(width * height / 4 * sizeof(yuv[2][0]));

	decode_jpeg_raw(cap_map, size, 0, 420, width, height, yuv[0], yuv[1], yuv[2]);

	y=map;
	u=y+width*height;
	v=u+(width*height)/4;
	memset(y, 0, width*height);
	memset(u, 0, width*height/4);
	memset(v, 0, width*height/4);

	for(loop=0; loop<width*height; loop++) {
		*map++=yuv[0][loop];
	}

	for(loop=0; loop<width*height/4; loop++) {
		*map++=yuv[1][loop];
	}

	for(loop=0; loop<width*height/4; loop++) {
		*map++=yuv[2][loop];
	}

	free(yuv[0]);
	free(yuv[1]);
	free(yuv[2]);
}
#endif



#define MAX2(x, y) ((x) > (y) ? (x) : (y))
#define MIN2(x, y) ((x) < (y) ? (x) : (y))

/* Constants used by auto brightness feature
 * Defined as constant to make it easier for people to tweak code for a
 * difficult camera.
 * The experience gained from people could help improving the feature without
 * adding too many new options.
 * AUTOBRIGHT_HYSTERESIS sets the minimum the light intensity must change before
 * we adjust brigtness.
 * AUTOBRIGHTS_DAMPER damps the speed with which we adjust the brightness
 * When the brightness changes a lot we step in large steps and as we approach the
 * target value we slow down to avoid overshoot and oscillations. If the camera
 * adjusts too slowly decrease the DAMPER value. If the camera oscillates try
 * increasing the DAMPER value. DAMPER must be minimum 1.
 * MAX and MIN are the max and min values of brightness setting we will send to
 * the camera device.
 */
#define AUTOBRIGHT_HYSTERESIS 10
#define AUTOBRIGHT_DAMPER 5
#define AUTOBRIGHT_MAX 255
#define AUTOBRIGHT_MIN 0

int vid_do_autobright(struct context *cnt, struct video_dev *viddev)
{

	int brightness_window_high;
	int brightness_window_low;
	int brightness_target;
	int i, j = 0, avg = 0, step = 0;
	unsigned char *image = cnt->imgs.image_ring_buffer;

	int make_change = 0;

	if (cnt->conf.brightness)
		brightness_target = cnt->conf.brightness;
	else
		brightness_target = 128;

	brightness_window_high = MIN2(brightness_target + AUTOBRIGHT_HYSTERESIS, 255);
	brightness_window_low = MAX2(brightness_target - AUTOBRIGHT_HYSTERESIS, 1);

	for (i = 0; i < cnt->imgs.motionsize; i += 101) {
		avg += image[i];
		j++;
	}
	avg = avg / j;

	/* average is above window - turn down brightness - go for the target */
	if (avg > brightness_window_high) {
		step = MIN2((avg - brightness_target) / AUTOBRIGHT_DAMPER + 1, viddev->brightness - AUTOBRIGHT_MIN);
		if (viddev->brightness > step + 1 - AUTOBRIGHT_MIN) {
			viddev->brightness -= step;
			make_change = 1;
		}
	} else if (avg < brightness_window_low) {
		/* average is below window - turn up brightness - go for the target */
		step = MIN2((brightness_target - avg) / AUTOBRIGHT_DAMPER + 1, AUTOBRIGHT_MAX - viddev->brightness);
		if (viddev->brightness < AUTOBRIGHT_MAX - step) {
			viddev->brightness += step;
			make_change = 1;
		}
	}

	return make_change;
}

/*****************************************************************************
	Wrappers calling the actual capture routines
 *****************************************************************************/

#ifndef WITHOUT_V4L
/* big lock for vid_start to ensure exclusive access to viddevs while adding 
 * devices during initialization of each thread
 */
static pthread_mutex_t vid_mutex;

/* for the v4l stuff: */
#include <sys/mman.h>
#include <sys/utsname.h>
#include <dirent.h>

/* Here we setup the viddevs structure which is used globally in the vid_*
 * functions.
 */
struct video_dev **viddevs = NULL;

/**
 * vid_init
 *
 * Called from motion.c at the very beginning before setting up the threads.
 * Function prepares the viddevs struct for the threads and the vid_mutex
 */
void vid_init(void)
{
	if (!viddevs) {
		viddevs = mymalloc(sizeof(struct video_dev *));
		viddevs[0] = NULL;
	}

	pthread_mutex_init(&vid_mutex, NULL);
}

/**
 * vid_close
 *
 * vid_close is called from motion.c when Motion is stopped or restarted
 * It gets rid of all open video devices. It is called BEFORE vid_cleanup.
 */
void vid_close(void)
{
	int i = -1;
    MLOG("in vid close");
	if (viddevs) {
		while (viddevs[++i]) {
#ifdef MOTION_V4L2
			if (viddevs[i]->v4l2)
				v4l2_close(viddevs[i]);
#else
			int tmp = viddevs[i]->fd;
            if(tmp > 0)
            {
    			viddevs[i]->fd = -1;
    			close(tmp);
            }
#endif
		}
	}
    MLOG("leave vid_close");
}

/**
 * vid_cleanup
 *
 * vid_cleanup is called from motion.c when Motion is stopped or restarted
 * It free all the memory held by the viddevs structs.
 */
void vid_cleanup(void)
{
	int i = -1;
	if (viddevs) {
		while (viddevs[++i]) {
#ifdef MOTION_V4L2
			if (viddevs[i]->v4l2)
				v4l2_cleanup(viddevs[i]);
			else
#endif
            MLOG("before unmap\n");
				munmap(viddevs[i]->v4l_buffers[0], viddevs[i]->size_map);
			free(viddevs[i]);
            MLOG("after unmap\n");
		}
		free(viddevs);
		viddevs = NULL;
	}
}

/**
 * vid_start
 *
 * vid_start setup the capture device. This will be either a V4L device or a netcam.
 * The function does the following:
 * - If the camera is a netcam - netcam_start is called and function returns
 * - Width and height are checked for valid value (multiple of 16)
 * - Copy the config height and width to the imgs struct. Note that height and width are
 *   only copied to the from the conf struct to the imgs struct during program startup
 *   The width and height can no later be changed via http remote control as this would
 *   require major re-memory allocations of all image buffers.
 * - Setup basic V4L properties incl palette incl setting 
 * - Open the device
 * - Returns the device number.
 *
 * Parameters:
 *     cnt        Pointer to the context for this thread
 *
 * "Global" variable
 *     viddevs    The viddevs struct is "global" within the context of video.c
 *                and used in functions vid_*.
 *     vid_mutex  Mutex needed to handle exclusive access to the viddevs struct when
 *                each thread adds a new video device during startup calling vid_start
 *
 * Returns
 *     device number
 *     -1 if failed to open device.
 */
 #ifndef _VIDIO_
 #define _VIDIO_
#define VIDIOCSSTART        _IO('v',50)     /* start grabing frames */
#define VIDIOCSSTOP         _IO('v',51)     /* stop grabing frames */
#define VIDIOCSPID         _IOW('v',52, int)    /* stop grabing frames */
#endif
static int vid_v4lx_start(struct context *cnt)
{
	struct config *conf = &cnt->conf;
	int dev = -1;
    int pid = 0;
    FILE *fp = NULL;

	/* Start a new block so we can make declarations without breaking good old
	 * gcc 2.95 or older.
	 */
	{
		int i = -1;
		int width, height, input, norm, tuner_number;
		unsigned long frequency;

		/* We use width and height from conf in this function. They will be assigned
		 * to width and height in imgs here, and cap_width and cap_height in 
		 * rotate_data won't be set until in rotate_init.
		 * Motion requires that width and height is a multiple of 16 so we check
		 * for this first.
		 */
		if (conf->width % 16) {
			motion_log(LOG_ERR, 0, "config image width (%d) is not modulo 16", conf->width);
			return -1;
		}

		if (conf->height % 16) {
			motion_log(LOG_ERR, 0, "config image height (%d) is not modulo 16", conf->height);
			return -1;
		}

		width = conf->width;
		height = conf->height;
		input = conf->input;
		norm = conf->norm;
		frequency = conf->frequency;
		tuner_number = conf->tuner_number;

		pthread_mutex_lock(&vid_mutex);

		/* Transfer width and height from conf to imgs. The imgs values are the ones
		 * that is used internally in Motion. That way, setting width and height via
		 * http remote control won't screw things up.
		 */
		cnt->imgs.width = width;
		cnt->imgs.height = height;

		/* First we walk through the already discovered video devices to see
		 * if we have already setup the same device before. If this is the case
		 * the device is a Round Robin device and we set the basic settings
		 * and return the file descriptor
		 */
		while (viddevs[++i]) {
			if (!strcmp(conf->video_device, viddevs[i]->video_device)) {
				int fd;
				cnt->imgs.type = viddevs[i]->v4l_fmt;
				switch (cnt->imgs.type) {
				case VIDEO_PALETTE_GREY:
					cnt->imgs.motionsize = width * height;
					cnt->imgs.size = width * height;
					break;
				case VIDEO_PALETTE_YUYV:
				case VIDEO_PALETTE_RGB24:
				case VIDEO_PALETTE_YUV422:
					cnt->imgs.type = VIDEO_PALETTE_YUV420P;
				case VIDEO_PALETTE_YUV420P:
					cnt->imgs.motionsize = width * height;
					cnt->imgs.size = (width * height * 3) / 2;
					break;
				}
				fd = viddevs[i]->fd;
				pthread_mutex_unlock(&vid_mutex);
				return fd;
			}
		}

		viddevs = myrealloc(viddevs, sizeof(struct video_dev *) * (i + 2), "vid_start");
		viddevs[i] = mymalloc(sizeof(struct video_dev));
		memset(viddevs[i], 0, sizeof(struct video_dev));
		viddevs[i + 1] = NULL;

		pthread_mutexattr_init(&viddevs[i]->attr);
		pthread_mutex_init(&viddevs[i]->mutex, NULL);

		viddevs[i]->video_device = conf->video_device;//"/dev/video0"

		dev = open(viddevs[i]->video_device, O_RDWR);

		if (dev < 0) {
            MDEBUG("failed to open video device\n");
			motion_log(LOG_ERR, 1, "Failed to open video device %s", conf->video_device);
			return -1;
		}
        //add by z67625
        if(NULL != (fp = fopen(cnt->conf.pid_file,"r")))
        {
            char cmd[16] = "";
            fgets(cmd,16,fp);
            pid = atoi(cmd);
            fclose(fp);
        }
        MLOG("\nget pid is %d\n",pid);
        if(ioctl(dev,VIDIOCSPID,&pid) == -1)
        {
            MDEBUG("ioctl pid error \n");
        }
        MLOG("=======open devi fd is %d\n",dev);
        //add by z67625
		viddevs[i]->fd = dev;
		viddevs[i]->input = input;
		viddevs[i]->height = height;
		viddevs[i]->width = width;
		viddevs[i]->freq = frequency;
		viddevs[i]->tuner_number = tuner_number;

		/* We set brightness, contrast, saturation and hue = 0 so that they only get
		 * set if the config is not zero.
		 */
		viddevs[i]->brightness = 0;
		viddevs[i]->contrast = 0;
		viddevs[i]->saturation = 0;
		viddevs[i]->hue = 0;
		viddevs[i]->owner = -1;
		viddevs[i]->v4l_fmt = VIDEO_PALETTE_YUV420P;
#ifdef MOTION_V4L2
		/* First lets try V4L2 and if it's not supported V4L1 */

		viddevs[i]->v4l2 = 1;

		if (!v4l2_start(cnt, viddevs[i], width, height, input, norm, frequency, tuner_number)) {
			/* restore width & height because could be changed in v4l2_start ()*/
			viddevs[i]->width = width;
			viddevs[i]->height = height;
#endif

			if (!v4l_start(cnt, viddevs[i], width, height, input, norm, frequency, tuner_number)) {
				pthread_mutex_unlock(&vid_mutex);
				return -1;
			}
#ifdef MOTION_V4L2
			viddevs[i]->v4l2 = 0;
		}
#endif
		if (viddevs[i]->v4l2 == 0) {
			motion_log(-1, 0, "Using V4L1");
		} else {
			motion_log(-1, 0, "Using V4L2");
			/* Update width & height because could be changed in v4l2_start () */
			width = viddevs[i]->width;
			height = viddevs[i]->height;
			cnt->conf.width = width;
			cnt->conf.height = height;
			cnt->imgs.width = width;
			cnt->imgs.height = height;
		}

		cnt->imgs.type = viddevs[i]->v4l_fmt;

		switch (cnt->imgs.type) {
		case VIDEO_PALETTE_GREY:
			cnt->imgs.size = width * height;
			cnt->imgs.motionsize = width * height;
			break;
		case VIDEO_PALETTE_YUYV:
		case VIDEO_PALETTE_RGB24:
		case VIDEO_PALETTE_YUV422:
			cnt->imgs.type = VIDEO_PALETTE_YUV420P;
		case VIDEO_PALETTE_YUV420P:
			cnt->imgs.size = (width * height * 3) / 2;
			cnt->imgs.motionsize = width * height;
			break;
		}

		pthread_mutex_unlock(&vid_mutex);
	}

	return dev;
}
#endif				/*WITHOUT_V4L */

int vid_start(struct context *cnt)
{
	struct config *conf = &cnt->conf;
	int dev = -1;

	if (conf->netcam_url) {
		return netcam_start(cnt);
	}
#ifndef WITHOUT_V4L
	dev = vid_v4lx_start(cnt);
    if(dev < 0)
    {
        MDEBUG("%s :get dev error",__FUNCTION__);
    }
#endif				/*WITHOUT_V4L */

	return dev;
}

/**
 * vid_next
 *
 * vid_next fetches a video frame from a either v4l device or netcam
 *
 * Parameters:
 *     cnt        Pointer to the context for this thread
 *     map        Pointer to the buffer in which the function puts the new image
 *
 * Global variable
 *     viddevs    The viddevs struct is "global" within the context of video.c
 *                and used in functions vid_*.
 * Returns
 *     0                        Success
 *    -1                        Fatal V4L error
 *    -2                        Fatal Netcam error
 *    Positive numbers...
 *    with bit 0 set            Non fatal V4L error (not implemented)
 *    with bit 1 set            Non fatal Netcam error
 */
int vid_next(struct context *cnt, unsigned char *map)
{
	struct config *conf = &cnt->conf;
	int ret = -1;

	if (conf->netcam_url) {
		if (cnt->video_dev == -1)
			return NETCAM_GENERAL_ERROR;

		ret = netcam_next(cnt, map);
		return ret;
	}
#ifndef WITHOUT_V4L

	/* We start a new block so we can make declarations without breaking
	 * gcc 2.95 or older
	 */
	{
		int i = -1;
		int width, height;
		int dev = cnt->video_dev;

		/* NOTE: Since this is a capture, we need to use capture dimensions. */
		width = cnt->rotate_data.cap_width;
		height = cnt->rotate_data.cap_height;

		while (viddevs[++i])
			if (viddevs[i]->fd == dev)
				break;

		if (!viddevs[i])
			return V4L_FATAL_ERROR;

		if (viddevs[i]->owner != cnt->threadnr) {
			pthread_mutex_lock(&viddevs[i]->mutex);
			viddevs[i]->owner = cnt->threadnr;
			viddevs[i]->frames = conf->roundrobin_frames;
			cnt->switched = 1;
		}
#ifdef MOTION_V4L2
		if (viddevs[i]->v4l2) {
			v4l2_set_input(cnt, viddevs[i], map, width, height, conf);

			ret = v4l2_next(cnt, viddevs[i], map, width, height);
		} else {
#endif
			v4l_set_input(cnt, viddevs[i], map, width, height, conf->input, conf->norm,
				      conf->roundrobin_skip, conf->frequency, conf->tuner_number);

			ret = v4l_next(viddevs[i], map, width, height);
#ifdef MOTION_V4L2
		}
#endif
		if (--viddevs[i]->frames <= 0) {
			viddevs[i]->owner = -1;
			pthread_mutex_unlock(&viddevs[i]->mutex);
		}

		if (cnt->rotate_data.degrees > 0) {
			/* rotate the image as specified */
			rotate_map(cnt, map);
		}
	}
#endif				/*WITHOUT_V4L */
	return ret;
}

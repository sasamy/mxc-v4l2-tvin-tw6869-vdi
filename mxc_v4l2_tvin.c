/*
 * Copyright 2007-2015 Freescale Semiconductor, Inc. All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * @file mxc_v4l2_tvin.c
 *
 * @brief Mxc TVIN For Linux 2 driver test application
 *
 */

/*=======================================================================
										INCLUDE FILES
=======================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <sys/time.h>
#include "mxcfb.h"
#include "ipu.h"
#include "g2d.h"

//#define CAPTURE_TO_FILE

#define G2D_CACHEABLE    0

#define TFAIL -1
#define TPASS 0

#define NUMBER_BUFFERS    4

char v4l_capture_dev[100] = "/dev/video0";
#ifdef BUILD_FOR_ANDROID
char fb_display_dev[100] = "/dev/graphics/fb1";
char fb_display_bg_dev[100] = "/dev/graphics/fb0";
#else
char fb_display_dev[100] = "/dev/fb1";
char fb_display_bg_dev[100] = "/dev/fb0";
#endif
int fd_capture_v4l = 0;
int fd_fb_display = 0;
int fd_ipu = 0;
unsigned char * g_fb_display = NULL;
int g_input = 1;
int g_display_num_buffers = 3;
int g_capture_num_buffers = NUMBER_BUFFERS;
int g_in_width = 720;
int g_in_height = 480;
int g_in_fmt = V4L2_PIX_FMT_UYVY;
int g_display_width = 0;
int g_display_height = 0;
int g_display_top = 0;
int g_display_left = 0;
int g_display_fmt = V4L2_PIX_FMT_UYVY;
int g_display_base_phy;;
int g_display_size;
int g_display_fg = 1;
int g_display_id = 1;
struct fb_var_screeninfo g_screen_info;
int g_frame_count = 0x7FFFFFFF;
int g_frame_size;
bool g_g2d_render = 0;
int g_g2d_fmt;
int g_mem_type = V4L2_MEMORY_USERPTR;
int g_vdi_enable = 0;
int g_vdi_motion = 0;

struct testbuffer
{
	unsigned char *start;
	size_t offset;
	unsigned int length;
};

struct testbuffer display_buffers[3];
struct testbuffer capture_buffers[NUMBER_BUFFERS];
struct g2d_buf *g2d_buffers[NUMBER_BUFFERS];

static void draw_image_to_framebuffer(struct g2d_buf *buf, int img_width, int img_height, int img_format, 
		 struct fb_var_screeninfo *screen_info, int left, int top, int to_width, int to_height, int set_alpha, int rotation)
{
	struct g2d_surface src, dst;
	void *g2dHandle;

	if (((left+to_width) > (int)screen_info->xres) || ((top+to_height) > (int)screen_info->yres)) {
		printf("Bad display image dimensions!\n");
		return;
	}

#if G2D_CACHEABLE
        g2d_cache_op(buf, G2D_CACHE_FLUSH);
#endif

	if (g2d_open(&g2dHandle) == -1 || g2dHandle == NULL) {
		printf("Fail to open g2d device!\n");
		g2d_free(buf);
		return;
	}

/*
	NOTE: in this example, all the test image data meet with the alignment requirement.
	Thus, in your code, you need to pay attention on that.

	Pixel buffer address alignment requirement,
	RGB/BGR:  pixel data in planes [0] with 16bytes alignment,
	NV12/NV16:  Y in planes [0], UV in planes [1], with 64bytes alignment,
	I420:    Y in planes [0], U in planes [1], V in planes [2], with 64 bytes alignment,
	YV12:  Y in planes [0], V in planes [1], U in planes [2], with 64 bytes alignment,
	NV21/NV61:  Y in planes [0], VU in planes [1], with 64bytes alignment,
	YUYV/YVYU/UYVY/VYUY:  in planes[0], buffer address is with 16bytes alignment.
*/

	src.format = img_format;
	switch (src.format) {
	case G2D_RGB565:
	case G2D_RGBA8888:
	case G2D_RGBX8888:
	case G2D_BGRA8888:
	case G2D_BGRX8888:
	case G2D_BGR565:
	case G2D_YUYV:
	case G2D_UYVY:
		src.planes[0] = buf->buf_paddr;
		break;
	case G2D_NV12:
		src.planes[0] = buf->buf_paddr;
		src.planes[1] = buf->buf_paddr + img_width * img_height;
		break;
	case G2D_I420:
		src.planes[0] = buf->buf_paddr;
		src.planes[1] = buf->buf_paddr + img_width * img_height;
		src.planes[2] = src.planes[1]  + img_width * img_height / 4;
		break;
	case G2D_YV12:
		src.planes[0] = buf->buf_paddr;
		src.planes[2] = buf->buf_paddr + img_width * img_height;
		src.planes[1] = src.planes[2]  + img_width * img_height / 4;
		break;
	case G2D_NV16:
		src.planes[0] = buf->buf_paddr;
		src.planes[1] = buf->buf_paddr + img_width * img_height;
                break;
	default:
		printf("Unsupport image format in the example code\n");
		return;
	}

	src.left = 0;
	src.top = 0;
	src.right = img_width;
	src.bottom = img_height;
	src.stride = img_width;
	src.width  = img_width;
	src.height = img_height;
	src.rot  = G2D_ROTATION_0;

	dst.planes[0] = g_display_base_phy;
	dst.left = left;
	dst.top = top;
	dst.right = left + to_width;
	dst.bottom = top + to_height;
	dst.stride = screen_info->xres;
	dst.width = screen_info->xres;
	dst.height = screen_info->yres;
	dst.rot = rotation;
	dst.format = screen_info->bits_per_pixel == 16 ? G2D_RGB565 : (screen_info->red.offset == 0 ? G2D_RGBA8888 : G2D_BGRA8888);

	if (set_alpha) {
		src.blendfunc = G2D_ONE;
		dst.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
	
		src.global_alpha = 0x80;
		dst.global_alpha = 0xff;
	
		g2d_enable(g2dHandle, G2D_BLEND);
		g2d_enable(g2dHandle, G2D_GLOBAL_ALPHA);
	}

	g2d_blit(g2dHandle, &src, &dst);
	g2d_finish(g2dHandle);

	if (set_alpha) {
		g2d_disable(g2dHandle, G2D_GLOBAL_ALPHA);
		g2d_disable(g2dHandle, G2D_BLEND);
	}

	g2d_close(g2dHandle);
}

int start_capturing(void)
{
        int i;
        struct v4l2_buffer buf;
        enum v4l2_buf_type type;

        for (i = 0; i < g_capture_num_buffers; i++) {
                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = g_mem_type;
                buf.index = i;
		if (g_mem_type == V4L2_MEMORY_USERPTR) {
			buf.length = capture_buffers[i].length;
			buf.m.userptr = (unsigned long)capture_buffers[i].offset;
		}
                if (ioctl(fd_capture_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
                        printf("VIDIOC_QUERYBUF error\n");
                        return TFAIL;
                }

		if (g_mem_type == V4L2_MEMORY_MMAP) {
	                capture_buffers[i].length = buf.length;
	                capture_buffers[i].start = mmap(NULL, capture_buffers[i].length,
	                    PROT_READ | PROT_WRITE, MAP_SHARED, fd_capture_v4l, buf.m.offset);
			memset(capture_buffers[i].start, 0xFF, capture_buffers[i].length);

			if (ioctl(fd_capture_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
			    printf("VIDIOC_QUERYBUF for DMA address error\n");
			    return TFAIL;
			}
			capture_buffers[i].offset = (size_t) buf.m.offset;
		}
	}

	for (i = 0; i < g_capture_num_buffers; i++) {
		memset(&buf, 0, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = g_mem_type;
		buf.index = i;
		if (g_mem_type == V4L2_MEMORY_USERPTR)
			buf.m.offset = (unsigned int)capture_buffers[i].start;
		else
			buf.m.offset = capture_buffers[i].offset;
		buf.length = capture_buffers[i].length;
		if (ioctl(fd_capture_v4l, VIDIOC_QBUF, &buf) < 0) {
			printf("VIDIOC_QBUF error\n");
			return TFAIL;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl (fd_capture_v4l, VIDIOC_STREAMON, &type) < 0) {
		printf("VIDIOC_STREAMON error\n");
		return TFAIL;
	}
	return TPASS;
}

void memfree(int buf_size, int buf_cnt)
{
	int i;
        unsigned int page_size;

	page_size = getpagesize();
	buf_size = (buf_size + page_size - 1) & ~(page_size - 1);

	for (i = 0; i < buf_cnt; i++) {
		if (capture_buffers[i].start)
			munmap(capture_buffers[i].start, buf_size);
		if (capture_buffers[i].offset)
			ioctl(fd_ipu, IPU_FREE, &capture_buffers[i].offset);
	}
}

int memalloc(int buf_size, int buf_cnt)
{
	int i, ret = TPASS;
        unsigned int page_size;

	for (i = 0; i < buf_cnt; i++) {
		page_size = getpagesize();
		buf_size = (buf_size + page_size - 1) & ~(page_size - 1);
		capture_buffers[i].length = capture_buffers[i].offset = buf_size;
		ret = ioctl(fd_ipu, IPU_ALLOC, &capture_buffers[i].offset);
		if (ret < 0) {
			printf("ioctl IPU_ALLOC fail\n");
			ret = TFAIL;
			goto err;
		}
		capture_buffers[i].start = mmap(0, buf_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd_ipu, capture_buffers[i].offset);
		if (!capture_buffers[i].start) {
			printf("mmap fail\n");
			ret = TFAIL;
			goto err;
		}
	}

	return ret;
err:
	memfree(buf_size, buf_cnt);
	return ret;
}

int prepare_capture_buffers(void)
{
	int ret = TPASS;

	if (g_mem_type == V4L2_MEMORY_USERPTR)
		ret = memalloc(g_frame_size, g_capture_num_buffers);

	return ret;
}

int prepare_g2d_buffers(void)
{
	int i;

	for (i = 0; i < g_capture_num_buffers; i++) {
#if G2D_CACHEABLE
		g2d_buffers[i] = g2d_alloc(g_frame_size, 1);//alloc physical contiguous memory for source image data with cacheable attribute
#else
		g2d_buffers[i] = g2d_alloc(g_frame_size, 0);//alloc physical contiguous memory for source image data
#endif
		if(!g2d_buffers[i]) {
			printf("Fail to allocate physical memory for image buffer!\n");
			return TFAIL;
		}
	}

	return 0;
}

int prepare_display_buffers(void)
{
	int i;

	g_fb_display = (unsigned char *)mmap(0, g_display_size * g_display_num_buffers, 
		PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb_display, 0);
	if (g_fb_display == NULL) {
		printf("v4l2 tvin test: display mmap failed\n");
		return TFAIL;
	}

	for (i = 0; i < g_display_num_buffers; i++) {
		display_buffers[i].length = g_display_size;
		display_buffers[i].offset = g_display_base_phy + g_display_size * i;
		display_buffers[i].start = g_fb_display + (g_display_size * i);
	}
	return TPASS;
}

int v4l_capture_setup(void)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	v4l2_std_id id;

	if (ioctl (fd_capture_v4l, VIDIOC_QUERYCAP, &cap) < 0) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s is no V4L2 device\n",
					v4l_capture_dev);
			return TFAIL;
		} else {
			fprintf (stderr, "%s isn not V4L device,unknow error\n",
			v4l_capture_dev);
			return TFAIL;
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf (stderr, "%s is no video capture device\n",
			v4l_capture_dev);
		return TFAIL;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming i/o\n",
			v4l_capture_dev);
		return TFAIL;
	}

	if (ioctl (fd_capture_v4l, VIDIOC_G_STD, &id) < 0) {
		printf("VIDIOC_G_STD failed\n");
		return TFAIL;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = g_in_fmt;
	if (ioctl (fd_capture_v4l, VIDIOC_S_FMT, &fmt) < 0) {
		fprintf (stderr, "%s iformat not supported \n",
			v4l_capture_dev);
		return TFAIL;
	}

	g_in_width = fmt.fmt.pix.width;
	g_in_height = fmt.fmt.pix.height;
	printf("g_in_width = %d, g_in_height = %d.\r\n", g_in_width, g_in_height);

	switch (g_in_fmt) {
		case V4L2_PIX_FMT_RGB565:
			g_frame_size = g_in_width * g_in_height * 2;
			g_g2d_fmt = G2D_RGB565;
			break;

		case V4L2_PIX_FMT_UYVY:
			g_frame_size = g_in_width * g_in_height * 2;
			g_g2d_fmt = G2D_UYVY;
			break;

		case V4L2_PIX_FMT_YUYV:
			g_frame_size = g_in_width * g_in_height * 2;
			g_g2d_fmt = G2D_YUYV;
			break;

		case V4L2_PIX_FMT_YUV420:
			g_frame_size = g_in_width * g_in_height * 3 / 2;
			g_g2d_fmt = G2D_I420;
			break;

		case V4L2_PIX_FMT_NV12:
			g_frame_size = g_in_width * g_in_height * 3 / 2;
			g_g2d_fmt = G2D_NV12;
			break;

		default:
			printf("Unsupported format.\n");
			return TFAIL;
	}

	memset(&req, 0, sizeof (req));
	req.count = g_capture_num_buffers;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = g_mem_type;
	if (ioctl (fd_capture_v4l, VIDIOC_REQBUFS, &req) < 0) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s does not support "
					 "memory mapping\n", v4l_capture_dev);
			return TFAIL;
		} else {
			fprintf (stderr, "%s does not support "
					 "memory mapping, unknow error\n", v4l_capture_dev);
			return TFAIL;
		}
	}

	if (req.count < 2) {
		fprintf (stderr, "Insufficient buffer memory on %s\n",
			 v4l_capture_dev);
		return TFAIL;
	}

	return TPASS;
}

int fb_display_setup(void)
{
	int fd_fb_bg = 0;
	struct mxcfb_gbl_alpha alpha;
	char node[8];
	struct fb_fix_screeninfo fb_fix;
	struct mxcfb_pos pos;

	if (ioctl(fd_fb_display, FBIOGET_VSCREENINFO, &g_screen_info) < 0) {
		printf("fb_display_setup FBIOGET_VSCREENINFO failed\n");
		return TFAIL;
	}

	if (ioctl(fd_fb_display, FBIOGET_FSCREENINFO, &fb_fix) < 0) {
		printf("fb_display_setup FBIOGET_FSCREENINFO failed\n");
		return TFAIL;
	}

	printf("fb_fix.id = %s.\r\n", fb_fix.id);
	if ((strcmp(fb_fix.id, "DISP4 FG") == 0) || (strcmp(fb_fix.id, "DISP3 FG") == 0)) {
		g_display_fg = 1;
		if (g_g2d_render) {
			pos.x = 0;
			pos.y = 0;
		} else {
			pos.x = g_display_left;
			pos.y = g_display_top;
		}
		if (ioctl(fd_fb_display, MXCFB_SET_OVERLAY_POS, &pos) < 0) {
			printf("fb_display_setup MXCFB_SET_OVERLAY_POS failed\n");
			return TFAIL;
		}

		if (!g_g2d_render) {
			g_screen_info.xres = g_display_width;
			g_screen_info.yres = g_display_height;
			g_screen_info.yres_virtual = g_screen_info.yres * g_display_num_buffers;
			g_screen_info.nonstd = g_display_fmt;
			if (ioctl(fd_fb_display, FBIOPUT_VSCREENINFO, &g_screen_info) < 0) {
				printf("fb_display_setup FBIOPUET_VSCREENINFO failed\n");
				return TFAIL;
			}

			ioctl(fd_fb_display, FBIOGET_FSCREENINFO, &fb_fix);
			ioctl(fd_fb_display, FBIOGET_VSCREENINFO, &g_screen_info);
		}

		sprintf(node, "%d", g_display_id - 1);	//for iMX6
#ifdef BUILD_FOR_ANDROID
		strcpy(fb_display_bg_dev, "/dev/graphics/fb");
#else
		strcpy(fb_display_bg_dev, "/dev/fb");
#endif
		strcat(fb_display_bg_dev, node);
		if ((fd_fb_bg = open(fb_display_bg_dev, O_RDWR )) < 0) {
			printf("Unable to open bg frame buffer\n");
			return TFAIL;
		}

		/* Overlay setting */
		alpha.alpha = 0;
		alpha.enable = 1;
		if (ioctl(fd_fb_bg, MXCFB_SET_GBL_ALPHA, &alpha) < 0) {
			printf("Set global alpha failed\n");
			close(fd_fb_bg);
			return TFAIL;
		}

		if (g_g2d_render) {
			ioctl(fd_fb_bg, FBIOGET_VSCREENINFO, &g_screen_info);

			g_screen_info.yres_virtual = g_screen_info.yres * g_display_num_buffers;
			if (ioctl(fd_fb_display, FBIOPUT_VSCREENINFO, &g_screen_info) < 0) {
				printf("fb_display_setup FBIOPUET_VSCREENINFO failed\n");
				return TFAIL;
			}
			ioctl(fd_fb_display, FBIOGET_FSCREENINFO, &fb_fix);
			ioctl(fd_fb_display, FBIOGET_VSCREENINFO, &g_screen_info);
		}
	} else {
		g_display_fg = 0;

		if (!g_g2d_render) {
			printf("It is background screen, only full screen default format was supported.\r\n");
			g_display_width = g_screen_info.xres;
			g_display_height = g_screen_info.yres;
			g_display_num_buffers = 3;
			g_screen_info.yres_virtual = g_screen_info.yres * g_display_num_buffers;

			if (ioctl(fd_fb_display, FBIOPUT_VSCREENINFO, &g_screen_info) < 0) {
				printf("fb_display_setup FBIOPUET_VSCREENINFO failed\n");
				return TFAIL;
			}

			ioctl(fd_fb_display, FBIOGET_FSCREENINFO, &fb_fix);
			ioctl(fd_fb_display, FBIOGET_VSCREENINFO, &g_screen_info);

			if (g_screen_info.bits_per_pixel == 16)
				g_display_fmt = V4L2_PIX_FMT_RGB565;
			else if (g_screen_info.bits_per_pixel == 24)
				g_display_fmt = V4L2_PIX_FMT_RGB24;
			else
				g_display_fmt = V4L2_PIX_FMT_RGB32;
		}
	}

	ioctl(fd_fb_display, FBIOBLANK, FB_BLANK_UNBLANK);

	g_display_base_phy = fb_fix.smem_start;
	printf("fb: smem_start = 0x%x, smem_len = 0x%x.\r\n", (unsigned int)fb_fix.smem_start, (unsigned int)fb_fix.smem_len);

	g_display_size = g_screen_info.xres * g_screen_info.yres * g_screen_info.bits_per_pixel / 8;
	printf("fb: frame buffer size = 0x%x bytes.\r\n", g_display_size);

	printf("fb: g_screen_info.xres = %d, g_screen_info.yres = %d.\r\n", g_screen_info.xres, g_screen_info.yres);
	printf("fb: g_display_left = %d.\r\n", g_display_left);
	printf("fb: g_display_top = %d.\r\n", g_display_top);
	printf("fb: g_display_width = %d.\r\n", g_display_width);
	printf("fb: g_display_height = %d.\r\n", g_display_height);

	return TPASS;
}

int mxc_v4l_tvin_test(void)
{
	struct v4l2_buffer capture_buf;
	int i;
	struct ipu_task task;
	int total_time;
	struct timeval tv_start, tv_current;
	int display_buf_count = 0;

//	struct timeval fps_old, fps_current;  //qiang_debug added
//	int fps_period;  //qiang_debug added

#ifdef CAPTURE_TO_FILE
	char still_file[100] = "./still.yuv";
	int fd_still = 0;

	if ((fd_still = open(still_file, O_RDWR | O_CREAT | O_TRUNC, 0x0666)) < 0)
	{
		printf("Unable to create y frame recording file\n");
	}
#endif

	if (prepare_capture_buffers() < 0) {
		printf("prepare_capture_buffers failed\n");
		return TFAIL;
	}

	if (g_g2d_render) {
		if (prepare_g2d_buffers() < 0) {
			printf("prepare_g2d_buffers failed\n");
			return TFAIL;
		}
	} else {
		if (prepare_display_buffers() < 0) {
			printf("prepare_display_buffers failed\n");
			return TFAIL;
		}
	}

	if (start_capturing() < 0) {
		printf("start_capturing failed\n");
		return TFAIL;
	}

	memset(&task, 0, sizeof(struct ipu_task));
	task.input.width  = g_in_width;
	task.input.height = g_in_height;
	task.input.crop.w = g_in_width;
	task.input.crop.h = g_in_height;
	task.input.format = g_in_fmt;
	task.input.deinterlace.enable = g_vdi_enable;
	task.input.deinterlace.motion = g_vdi_motion;
	task.input.paddr = capture_buffers[0].offset;
	if (g_g2d_render) {
		// For VDI only
		task.output.width = g_in_width;
		task.output.height = g_in_height;
		task.output.crop.w = g_in_width;
		task.output.crop.h = g_in_height;
		task.output.format = g_in_fmt;
		task.output.paddr = g2d_buffers[0]->buf_paddr;
	} else {
		task.output.width = g_display_width;
		task.output.height = g_display_height;
		task.output.crop.w = g_display_width;
		task.output.crop.h = g_display_height;
		task.output.format = g_display_fmt;
		task.output.paddr = display_buffers[0].offset;
	}

	if (ioctl(fd_ipu, IPU_CHECK_TASK, &task) != IPU_CHECK_OK) {
		printf("IPU_CHECK_TASK failed.\r\n");
		return TFAIL;
	}

	gettimeofday(&tv_start, 0);
	printf("start time = %d s, %d us\n", (unsigned int) tv_start.tv_sec,
		(unsigned int) tv_start.tv_usec);

//	gettimeofday(&fps_old, 0);  //qiang_debug added
	for (i = 0; i < g_frame_count; i++) {
		memset(&capture_buf, 0, sizeof(capture_buf));
		capture_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		capture_buf.memory = g_mem_type;
		if (ioctl(fd_capture_v4l, VIDIOC_DQBUF, &capture_buf) < 0) {
			printf("VIDIOC_DQBUF failed.\n");
			return TFAIL;
		}
/*
		gettimeofday(&fps_current, 0);
		fps_period = (fps_current.tv_sec - fps_old.tv_sec) * 1000000L;
		fps_period += (fps_current.tv_usec - fps_old.tv_usec);
		memcpy(&fps_old, &fps_current, sizeof(struct timeval));
		if ((fps_period < 32000) || (fps_period > 34000))
			printf("frame %d, %d ms.\r\n", i, fps_period/1000);
*/  //qiang_debug added

#ifdef CAPTURE_TO_FILE
		if(i>100 && i<120)
			write(fd_still, capture_buffers[capture_buf.index].start, g_in_width*g_in_height*2);
#endif

		if (g_g2d_render)
			task.output.paddr = g2d_buffers[capture_buf.index]->buf_paddr;
		else
			task.output.paddr = display_buffers[display_buf_count].offset;

		task.input.paddr = capture_buffers[capture_buf.index].offset;
		if ((task.input.paddr != 0) && (task.output.paddr != 0)) {
			if (ioctl(fd_ipu, IPU_QUEUE_TASK, &task) < 0) {
				printf("IPU_QUEUE_TASK failed\n");
				return TFAIL;
			}
		}

		if (ioctl(fd_capture_v4l, VIDIOC_QBUF, &capture_buf) < 0) {
			printf("VIDIOC_QBUF failed\n");
			return TFAIL;
		}

		if (g_g2d_render) {
			draw_image_to_framebuffer(g2d_buffers[capture_buf.index], g_in_width, g_in_height, g_g2d_fmt, &g_screen_info, g_display_left, g_display_top, g_display_width, g_display_height, 0, G2D_ROTATION_0);
		} else {
			g_screen_info.xoffset = 0;
			g_screen_info.yoffset = display_buf_count * g_display_height;
			if (ioctl(fd_fb_display, FBIOPAN_DISPLAY, &g_screen_info) < 0) {
				printf("FBIOPAN_DISPLAY failed, count = %d\n", i);
				break;
			}

			display_buf_count ++;
			if (display_buf_count >= g_display_num_buffers)
				display_buf_count = 0;
		}
	}
	gettimeofday(&tv_current, 0);
	total_time = (tv_current.tv_sec - tv_start.tv_sec) * 1000000L;
	total_time += tv_current.tv_usec - tv_start.tv_usec;
	printf("total time for %u frames = %u us =  %lld fps\n", i, total_time, (i * 1000000ULL) / total_time);

#ifdef CAPTURE_TO_FILE
	close(fd_still);
#endif

	return TPASS;
}

int process_cmdline(int argc, char **argv)
{
	int i, val;
	char node[8];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-ow") == 0) {
			g_display_width = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-oh") == 0) {
			g_display_height = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-ot") == 0) {
			g_display_top = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-ol") == 0) {
			g_display_left = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-c") == 0) {
			g_frame_count = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-g2d") == 0) {
			g_g2d_render = 1;
			g_mem_type = V4L2_MEMORY_USERPTR;
		}
		else if (strcmp(argv[i], "-m") == 0) {
			g_vdi_enable = 1;
			g_vdi_motion = 2;
			g_mem_type = V4L2_MEMORY_USERPTR;
		}
		else if (strcmp(argv[i], "-x") == 0) {
			val = atoi(argv[++i]);
			sprintf(node, "%d", val);
			strcpy(v4l_capture_dev, "/dev/video");
			strcat(v4l_capture_dev, node);
		}
		else if (strcmp(argv[i], "-d") == 0) {
			val = atoi(argv[++i]);
			g_display_id = val;
			sprintf(node, "%d", val);
#ifdef BUILD_FOR_ANDROID
			strcpy(fb_display_dev, "/dev/graphics/fb");
#else
			strcpy(fb_display_dev, "/dev/fb");
#endif
			strcat(fb_display_dev, node);
		}
		else if (strcmp(argv[i], "-if") == 0) {
			i++;
			g_in_fmt = v4l2_fourcc(argv[i][0], argv[i][1],argv[i][2],argv[i][3]);
			if ((g_in_fmt != V4L2_PIX_FMT_NV12) &&
				(g_in_fmt != V4L2_PIX_FMT_UYVY) &&
				(g_in_fmt != V4L2_PIX_FMT_YUYV) &&
				(g_in_fmt != V4L2_PIX_FMT_YUV420)) {
				printf("Default capture format is used: UYVY\n");
				g_in_fmt = V4L2_PIX_FMT_UYVY;
			}
		}
		else if (strcmp(argv[i], "-of") == 0) {
			i++;
			g_display_fmt = v4l2_fourcc(argv[i][0], argv[i][1],argv[i][2],argv[i][3]);
			if ((g_display_fmt != V4L2_PIX_FMT_RGB565) &&
				(g_display_fmt != V4L2_PIX_FMT_RGB24) &&
				(g_display_fmt != V4L2_PIX_FMT_RGB32) &&
				(g_display_fmt != V4L2_PIX_FMT_BGR32) &&
				(g_display_fmt != V4L2_PIX_FMT_UYVY) &&
				(g_display_fmt != V4L2_PIX_FMT_YUYV)) {
				printf("Default display format is used: UYVY\n");
				g_display_fmt = V4L2_PIX_FMT_UYVY;
			}
		}
		else if (strcmp(argv[i], "-help") == 0) {
			printf("MXC Video4Linux TVin Test\n\n" \
				   "Syntax: mxc_v4l2_tvin.out\n" \
				   " -ow <capture display width>\n" \
				   " -oh <capture display height>\n" \
				   " -ot <display top>\n" \
				   " -ol <display left>\n" \
				   " -g2d <GPU g2d render> \n" \
				   " -m <motion> enable VDI motion\n"
				   " -c <capture counter> \n" \
				   " -x <capture device> 0 = /dev/video0; 1 = /dev/video1 ...>\n" \
				   " -d <output frame buffer> 0 = /dev/fb0; 1 = /dev/fb1 ...>\n" \
				   " -if <capture format, only YU12, YUYV, UYVY and NV12 are supported> \n" \
				   " -of <display format, only RGBP, RGB3, RGB4, BGR4, YUYV, and UYVY are supported> \n");
			return TFAIL;
		}
	}

	if ((g_display_width == 0) || (g_display_height == 0)) {
		printf("Zero display width or height\n");
		return TFAIL;
	}

	return TPASS;
}

int main(int argc, char **argv)
{
	int i;
	enum v4l2_buf_type type;

	if (process_cmdline(argc, argv) < 0) {
		return TFAIL;
	}

	if ((fd_capture_v4l = open(v4l_capture_dev, O_RDWR, 0)) < 0) {
		printf("Unable to open %s\n", v4l_capture_dev);
		return TFAIL;
	}

	if (v4l_capture_setup() < 0) {
		printf("Setup v4l capture failed.\n");
		return TFAIL;
	}

	if ((fd_ipu = open("/dev/mxc_ipu", O_RDWR, 0)) < 0) {
		printf("open ipu dev fail\n");
		close(fd_capture_v4l);
		return TFAIL;
	}

	if ((fd_fb_display = open(fb_display_dev, O_RDWR, 0)) < 0) {
		printf("Unable to open %s\n", fb_display_dev);
		close(fd_ipu);
		close(fd_capture_v4l);
		return TFAIL;
	}

	if (fb_display_setup() < 0) {
		printf("Setup fb display failed.\n");
		close(fd_ipu);
		close(fd_capture_v4l);
		close(fd_fb_display);
		return TFAIL;
	}

	mxc_v4l_tvin_test();

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(fd_capture_v4l, VIDIOC_STREAMOFF, &type);

	if (g_display_fg)
		ioctl(fd_fb_display, FBIOBLANK, FB_BLANK_NORMAL);

	if (g_mem_type == V4L2_MEMORY_USERPTR) {
		memfree(g_frame_size, g_capture_num_buffers);
	} else {
		for (i = 0; i < g_capture_num_buffers; i++)
			munmap(capture_buffers[i].start, capture_buffers[i].length);
	}

	if (g_g2d_render) {
		for (i = 0; i < g_capture_num_buffers; i++) {
			g2d_free(g2d_buffers[i]);
		}
	} else {
		munmap(g_fb_display, g_display_size * g_display_num_buffers);
	}

	close(fd_ipu);
	close(fd_capture_v4l);
	close(fd_fb_display);
	return 0;
}

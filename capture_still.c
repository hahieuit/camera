/*
* ======================================================================
*
*  COPYRIGHT 2017 All rights reserved.
*
* ======================================================================
*
*                           DISCLAIMER
* NO WARRANTIES
* expressly disclaims any warranty for the SOFTWARE
* PRODUCT. The SOFTWARE PRODUCT and any related documentation is
* provided "as is" without warranty of any kind, either expressed or
* implied, including, without limitation, the implied warranties or
* merchantability, fitness for a particular purpose, or noninfringe-
* ment. The entire risk arising out of the use or performance of the
* SOFTWARE PRODUCT remains with the user.
*
* NO LIABILITY FOR DAMAGES.
* Under no circumstances is liable for any damages
* whatsoever (including, without limitation, damages for loss of busi-
* ness profits, business interruption, loss of business information,
* or any other pecuniary loss) arising out of the use of or inability
* to use this product.
*
* ======================================================================
*
*  File:        capture_still.cpp
*
*  History:     05 Sept 2019
*
*  Description: JNI functions for capturing images library
*
* ======================================================================
*/
#ifdef __cplusplus
extern "C"{
#endif

#define LOG_TAG "capture_still.c"
#include <android/log.h>
#include "jni.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <string.h>
#include <malloc.h>

#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define ipu_fourcc(a,b,c,d)\
        (((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))

#define IPU_PIX_FMT_YUYV    ipu_fourcc('Y','U','Y','V') /*!< 16 YUV 4:2:2 */
#define IPU_PIX_FMT_UYVY    ipu_fourcc('U','Y','V','Y') /*!< 16 YUV 4:2:2 */
#define IPU_PIX_FMT_NV12    ipu_fourcc('N','V','1','2') /* 12 Y/CbCr 4:2:0 */
#define IPU_PIX_FMT_YUV420P ipu_fourcc('I','4','2','0') /*!< 12 YUV 4:2:0 */
#define IPU_PIX_FMT_YUV420P2 ipu_fourcc('Y','U','1','2') /*!< 12 YUV 4:2:0 */
#define IPU_PIX_FMT_YUV422P ipu_fourcc('4','2','2','P') /*!< 16 YUV 4:2:2 */
#define IPU_PIX_FMT_YUV444  ipu_fourcc('Y','4','4','4') /*!< 24 YUV 4:4:4 */
#define IPU_PIX_FMT_RGB565  ipu_fourcc('R','G','B','P') /*!< 16 RGB-5-6-5 */
#define IPU_PIX_FMT_BGR24   ipu_fourcc('B','G','R','3') /*!< 24 BGR-8-8-8 */
#define IPU_PIX_FMT_RGB24   ipu_fourcc('R','G','B','3') /*!< 24 RGB-8-8-8 */
#define IPU_PIX_FMT_BGR32   ipu_fourcc('B','G','R','4') /*!< 32 BGR-8-8-8-8 */
#define IPU_PIX_FMT_BGRA32  ipu_fourcc('B','G','R','A') /*!< 32 BGR-8-8-8-8 */
#define IPU_PIX_FMT_RGB32   ipu_fourcc('R','G','B','4') /*!< 32 RGB-8-8-8-8 */
#define IPU_PIX_FMT_RGBA32  ipu_fourcc('R','G','B','A') /*!< 32 RGB-8-8-8-8 */
#define IPU_PIX_FMT_ABGR32  ipu_fourcc('A','B','G','R') /*!< 32 ABGR-8-8-8-8 */

static int g_convert = 1;
static int g_width = 640;
static int g_height = 480;
static int g_top = 0;
static int g_left = 0;
static unsigned long g_pixelformat = IPU_PIX_FMT_YUYV;
static int g_bpp = 16;
static int g_camera_framerate = 30;
static int g_capture_mode = 1;
static char g_v4l_device[100] = "/dev/video0";
static const char *classPathName = "com/android/Camera/Native";

enum results{failed=-1, success, file_already_exist};

/* Convert to YUV420 format */
void fmt_convert(char *dest, char *src, struct v4l2_format fmt)
{
        int row, col, pos = 0;
        int bpp, yoff, uoff, voff;

        if (fmt.fmt.pix.pixelformat == IPU_PIX_FMT_YUYV) {
                bpp = 2;
                yoff = 0;
                uoff = 1;
                voff = 3;
        }
        else if (fmt.fmt.pix.pixelformat == IPU_PIX_FMT_UYVY) {
                bpp = 2;
                yoff = 1;
                uoff = 0;
                voff = 2;
        }
        else {	/* YUV444 */
                bpp = 4;
                yoff = 0;
                uoff = 1;
                voff = 2;
        }

        /* Copy Y */
        for (row = 0; row < fmt.fmt.pix.height; row++)
                for (col = 0; col < fmt.fmt.pix.width; col++)
                        dest[pos++] = src[row * fmt.fmt.pix.bytesperline + col * bpp + yoff];

        /* Copy U */
        for (row = 0; row < fmt.fmt.pix.height; row += 2) {
                for (col = 0; col < fmt.fmt.pix.width; col += 2)
                        dest[pos++] = src[row * fmt.fmt.pix.bytesperline + col * bpp + uoff];
        }

        /* Copy V */
        for (row = 0; row < fmt.fmt.pix.height; row += 2) {
                for (col = 0; col < fmt.fmt.pix.width; col += 2)
                        dest[pos++] = src[row * fmt.fmt.pix.bytesperline + col * bpp + voff];
        }
}

int bytes_per_pixel(int fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_NV12:
		return 1;
		break;
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
		return 2;
		break;
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
		return 3;
		break;
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_ABGR32:
		return 4;
		break;
	default:
		return 1;
		break;
	}
	return 0;
}

int v4l_capture_setup(int * fd_v4l)
{
        struct v4l2_streamparm parm;
        struct v4l2_format fmt;
        struct v4l2_crop crop;
        int ret = 0;

        if ((*fd_v4l = open(g_v4l_device, O_RDWR, 0)) < 0)
        {
                printf("Unable to open %s\n", g_v4l_device);
                return -1;
        }

        sleep(3);
        printf("capturing after 3 seconds");
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	parm.parm.capture.capturemode = g_capture_mode;

	if ((ret = ioctl(*fd_v4l, VIDIOC_S_PARM, &parm)) < 0)
	{
		printf("VIDIOC_S_PARM failed\n");
		return ret;
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.left = g_left;
	crop.c.top = g_top;
	crop.c.width = g_width;
	crop.c.height = g_height;
	if ((ret = ioctl(*fd_v4l, VIDIOC_S_CROP, &crop)) < 0)
	{
		printf("set cropping failed\n");
		return ret;
	}

	memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = g_pixelformat;
        fmt.fmt.pix.width = g_width;
        fmt.fmt.pix.height = g_height;
        fmt.fmt.pix.sizeimage = fmt.fmt.pix.width * fmt.fmt.pix.height * g_bpp / 8;
        fmt.fmt.pix.bytesperline = g_width * bytes_per_pixel(g_pixelformat);

        if ((ret = ioctl(*fd_v4l, VIDIOC_S_FMT, &fmt)) < 0)
        {
                printf("set format failed\n");
                return ret;
        }

        return ret;
}

int v4l_capture_image(int fd_v4l, const char *still_file)
{
        struct v4l2_format fmt;
        int fd_still = 0, ret = 0;
        char *buf1, *buf2;
        struct stat buffer;

        /* if file already exist just return file_already_exist do not overwrite the file*/
        int exist = stat(&still_file,&buffer);
        if(exist == 0) {
            return file_already_exist;
        }

        if ((fd_still = open(&still_file, O_RDWR | O_CREAT | O_TRUNC, 0x0666)) < 0) {
                printf("Unable to create y frame recording file\n");
                return -1;
        }

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if ((ret = ioctl(fd_v4l, VIDIOC_G_FMT, &fmt)) < 0) {
                printf("get format failed\n");
                return ret;
        } else {
                printf("\t Width = %d\n", fmt.fmt.pix.width);
                printf("\t Height = %d\n", fmt.fmt.pix.height);
                printf("\t Image size = %d\n", fmt.fmt.pix.sizeimage);
                printf("\t Pixel format = %c%c%c%c\n",
                        (char)(fmt.fmt.pix.pixelformat & 0xFF),
                        (char)((fmt.fmt.pix.pixelformat & 0xFF00) >> 8),
                        (char)((fmt.fmt.pix.pixelformat & 0xFF0000) >> 16),
                        (char)((fmt.fmt.pix.pixelformat & 0xFF000000) >> 24));
        }

        buf1 = (char *)malloc(fmt.fmt.pix.sizeimage);
        buf2 = (char *)malloc(fmt.fmt.pix.sizeimage);
        if (!buf1 || !buf2)
                goto exit0;

        memset(buf1, 0, fmt.fmt.pix.sizeimage);
        memset(buf2, 0, fmt.fmt.pix.sizeimage);

        if (read(fd_v4l, buf1, fmt.fmt.pix.sizeimage) != fmt.fmt.pix.sizeimage) {
                printf("v4l2 read error.\n");
                goto exit0;
        }

        if ((g_convert == 1) && (g_pixelformat != IPU_PIX_FMT_YUV422P)
		&& (g_pixelformat != IPU_PIX_FMT_YUV420P2)) {
                fmt_convert(buf2, buf1, fmt);
                //TODO: convert YUV420 to jpeg format
                write(fd_still, buf2, fmt.fmt.pix.width * fmt.fmt.pix.height * 3 / 2);
        }
        else {
                write(fd_still, buf1, fmt.fmt.pix.sizeimage);
        }

exit0:
        if (buf1)
                free(buf1);
        if (buf2)
                free(buf2);
        close(fd_still);
        close(fd_v4l);

	return ret;
}

static jint
capture_still(JNIEnv* env, jobject thiz, jstring path, jstring filename) {
    int fd_v4l;
    int i;
    int ret;
    char still_file[100] = {0};

    ret = v4l_capture_setup(&fd_v4l);
    if (ret)
    {
        return ret;
    }

    strcpy(still_file, path);
    strcat(still_file, filename);
    ret = v4l_capture_image(fd_v4l, &still_file);

    return ret;

}

static JNINativeMethod methods[] = {
  {"capture_still", "(LJAVA/LANG/STRING,LJAVA/LANG/STRING)I", (void*)capture_still },
};

/*
 * Register several native methods for one class.
 */
static int registerNativeMethods(JNIEnv* env, const char* className,
    JNINativeMethod* gMethods, int numMethods)
{
    jclass clazz;
    clazz = env->FindClass(className);
    if (clazz == NULL) {
        ALOGE("Native registration unable to find class '%s'", className);
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        ALOGE("RegisterNatives failed for '%s'", className);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

/*
 * Register native methods for all classes we know about.
 *
 * returns JNI_TRUE on success.
 */
static int registerNatives(JNIEnv* env)
{
  if (!registerNativeMethods(env, classPathName,
                 methods, sizeof(methods) / sizeof(methods[0]))) {
    return JNI_FALSE;
  }
  return JNI_TRUE;
}
// ----------------------------------------------------------------------------
/*
 * This is called by the VM when the shared library is first loaded.
 */

typedef union {
    JNIEnv* env;
    void* venv;
} UnionJNIEnvToVoid;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    UnionJNIEnvToVoid uenv;
    uenv.venv = NULL;
    jint result = -1;
    JNIEnv* env = NULL;

    ALOGI("JNI_OnLoad libOv7740");
    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_4) != JNI_OK) {
        ALOGE("ERROR: GetEnv failed");
        goto bail;
    }
    env = uenv.env;
    if (registerNatives(env) != JNI_TRUE) {
        ALOGE("ERROR: registerNatives failed");
        goto bail;
    }

    result = JNI_VERSION_1_4;

bail:
    return result;
}

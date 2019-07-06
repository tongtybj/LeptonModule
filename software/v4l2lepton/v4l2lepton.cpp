#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>                /* low-level i/o */
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <semaphore.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <stdint.h>

#include "Palettes.h"
#include "SPI.h"
#include "Lepton_I2C.h"

#define PACKET_SIZE 164
#define PACKET_SIZE_UINT16 (PACKET_SIZE/2)
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE_UINT16 (PACKET_SIZE_UINT16*PACKETS_PER_FRAME)
#define FPS 27;

static char const *v4l2dev = "/dev/video1";
static char *spidev = NULL;
static int v4l2sink = -1;
static int width = 80;                //640;    // Default for Flash
static int height = 60;        //480;    // Default for Flash
static char *vidsendbuf = NULL;
static int vidsendsiz = 0;
static int threshold = 100; // degree celsius
static bool is_bin = false; // if output binary image
static bool is_gray_temp = false; // if output grayscale image relative to celsius
static bool is_range = false; // if range is set by arg
static int temp_min = 0; // minimum celsius temp for grayscale image
static int temp_max = 255; // maximum celsius temp for grayscale image

static int resets = 0;
static uint8_t result[PACKET_SIZE*PACKETS_PER_FRAME];
static uint16_t *frameBuffer;

static void init_device() {
    SpiOpenPort(spidev);
}

float frameval2celsius(float frameval, float fpatemp) {
    // based on http://takesan.hatenablog.com/entry/2016/02/18/194252
    return ((0.05872*frameval-472.22999f+fpatemp) - 32.0f)/1.8f;
}

static void grab_frame() {

    resets = 0;
    for (int j = 0; j < PACKETS_PER_FRAME; j++) {
        read(spi_cs_fd, result + sizeof(uint8_t) * PACKET_SIZE * j, sizeof(uint8_t) * PACKET_SIZE);
        int packetNumber = result[j * PACKET_SIZE + 1];
        if (packetNumber != j) {
            j = -1;
            resets += 1;
            usleep(1000);
            if (resets == 750) {
                SpiClosePort();
                lepton_reboot();
                usleep(750000);
                SpiOpenPort(spidev);
            }
        }
    }
    if (resets >= 30) {
      fprintf( stderr, "done reading, reset times: %d \n", resets );
    }

    frameBuffer = (uint16_t *)result;
    int row, column;
    uint16_t value;
    uint16_t minValue = 65535;
    uint16_t maxValue = 0;

    for (int i = 0; i < FRAME_SIZE_UINT16; i++) {
        if (i % PACKET_SIZE_UINT16 < 2) {
            continue;
        }

        int temp = result[i * 2];
        result[i * 2] = result[i * 2 + 1];
        result[i * 2 + 1] = temp;

        value = frameBuffer[i];
        if (value > maxValue) {
            maxValue = value;
        }
        if (value < minValue) {
            minValue = value;
        }
        column = i % PACKET_SIZE_UINT16 - 2;
        row = i / PACKET_SIZE_UINT16;
    }

    float diff = maxValue - minValue;
    float scale = 255 / diff;

    int fpatemp = lepton_fpa_temperature(); 
    float fpatemp_f = fpatemp/100 *1.8f - 459.67f;

    for (int i = 0; i < FRAME_SIZE_UINT16; i++) {
        if (i % PACKET_SIZE_UINT16 < 2) {
            continue;
        }
        value = (frameBuffer[i] - minValue) * scale;
        const int *colormap;
        if (is_gray_temp) {
            colormap = colormap_grayscale;
        } else {
            colormap = colormap_ironblack;
        }
        column = (i % PACKET_SIZE_UINT16) - 2;
        row = i / PACKET_SIZE_UINT16;

        float value_temp = frameval2celsius(frameBuffer[i], fpatemp_f);

        // Set video buffer pixel to scaled colormap value
        int idx = row * width * 3 + column * 3;
        if (is_bin) {
            int bin_color;
            if (value_temp > threshold) {
                bin_color = 255;
            } else {
                bin_color = 0;
            }
            vidsendbuf[idx + 0] = bin_color;
            vidsendbuf[idx + 1] = bin_color;
            vidsendbuf[idx + 2] = bin_color;
        } else if (is_range) {
            value_temp = (value_temp - temp_min) * 255/(temp_max - temp_min);
            if (value_temp < 0) {
              value_temp = 0;
            } else if (value_temp>255) {
              value_temp = 255;
            }
            vidsendbuf[idx + 0] = colormap[3 * (int)value_temp];
            vidsendbuf[idx + 1] = colormap[3 * (int)value_temp + 1];
            vidsendbuf[idx + 2] = colormap[3 * (int)value_temp + 2];
        } else {
            vidsendbuf[idx + 0] = colormap[3 * value];
            vidsendbuf[idx + 1] = colormap[3 * value + 1];
            vidsendbuf[idx + 2] = colormap[3 * value + 2];
        }
    }

    /*
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    memset( vidsendbuf, 0, 3);
    memcpy( vidsendbuf+3, vidsendbuf, vidsendsiz-3 );
    */
}

static void stop_device() {
    SpiClosePort();
}

static void open_vpipe()
{
    v4l2sink = open(v4l2dev, O_WRONLY);
    if (v4l2sink < 0) {
        fprintf(stderr, "Failed to open v4l2sink device. (%s)\n", strerror(errno));
        exit(-2);
    }
    // setup video for proper format
    struct v4l2_format v;
    int t;
    v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    t = ioctl(v4l2sink, VIDIOC_G_FMT, &v);
    if( t < 0 )
        exit(t);
    v.fmt.pix.width = width;
    v.fmt.pix.height = height;
    v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    vidsendsiz = width * height * 3;
    v.fmt.pix.sizeimage = vidsendsiz;
    t = ioctl(v4l2sink, VIDIOC_S_FMT, &v);
    if( t < 0 )
        exit(t);
    vidsendbuf = (char*)malloc( vidsendsiz );
}

static pthread_t sender;
static sem_t lock1,lock2;
static void *sendvid(void *v)
{
    (void)v;
    for (;;) {
        sem_wait(&lock1);
        if (vidsendsiz != write(v4l2sink, vidsendbuf, vidsendsiz))
            exit(-1);
        sem_post(&lock2);
    }
}

void usage(char *exec)
{
    printf("Usage: %s [options]\n"
           "Options:\n"
           "  -d | --device name       Use name as spidev device "
               "(/dev/spidev0.1 by default)\n"
           "  -h | --help              Print this message\n"
           "  -v | --video name        Use name as v4l2loopback device "
               "(%s by default)\n"
           "  -b | --binary            Set to output binary image\n"
           "  -g | --gray              Set to output grayscale image\n"
           "  -r | --range             Set range for output grayscale image in celsius (eg. 15-40)\n"
           "  -t | --threshold temp    Set temperature threshold in celsius degree\n"
           "", exec, v4l2dev);
}

static const char short_options [] = "d:hv:t:bgr:";

static const struct option long_options [] = {
    { "device",  required_argument, NULL, 'd' },
    { "help",    no_argument,       NULL, 'h' },
    { "video",   required_argument, NULL, 'v' },
    { "binary",   required_argument, NULL, 'b' },
    { "threshold",   required_argument, NULL, 't' },
    { "gray",   required_argument, NULL, 'b' },
    { "threshold",   required_argument, NULL, 't' },
    { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
    struct timespec ts;

    // processing command line parameters
    for (;;) {
        int index;
        int c;

        c = getopt_long(argc, argv,
                        short_options, long_options,
                        &index);

        if (-1 == c)
            break;

        switch (c) {
            case 0:
                break;

            case 'd':
                spidev = optarg;
                break;

            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);

            case 'v':
                v4l2dev = optarg;
                break;

            case 'b':
                is_bin = true;
                break;

            case 'g':
                is_gray_temp = true;
                break;

            case 'r': {
                char delim[] = "-";
                char *ptr = strtok(optarg, delim);
                is_range = true;
                if (ptr != NULL) { 
                    temp_min = atoi(ptr);
                    ptr = strtok(NULL, delim);
                    if (ptr != NULL) {
                      temp_max = atoi(ptr);
                    }
                } 
                break;
                      }
            case 't':
                threshold = atoi(optarg);
                break;

            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    open_vpipe();

    // open and lock response
    if (sem_init(&lock2, 0, 1) == -1)
        exit(-1);
    sem_wait(&lock2);

    if (sem_init(&lock1, 0, 1) == -1)
        exit(-1);
    pthread_create(&sender, NULL, sendvid, NULL);

    for (;;) {
        // wait until a frame can be written
        fprintf( stderr, "Waiting for sink\n" );
        sem_wait(&lock2);
        // setup source
        init_device(); // open and setup SPI
        for (;;) {
            grab_frame();
            // push it out
            sem_post(&lock1);
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2;
            // wait for it to get written (or is blocking)
            if (sem_timedwait(&lock2, &ts))
                break;
        }
        stop_device(); // close SPI
    }
    close(v4l2sink);
    return 0;
}

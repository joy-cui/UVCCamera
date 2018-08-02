/////////////////////////////////////////////////////////////////////////////////////
#ifndef __VCAPTURE_NATIVE_H__
#define __VCAPTURE_NATIVE_H__

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>              // low-level i/o
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>          // for videodev2.h

#include <linux/videodev2.h>
#include <linux/usbdevice_fs.h>

using  namespace std;

/////////////////////////////////////////////////////////////////////////////////////
#define ERROR_DEVICE_NOT_EXIST       -1
#define ERROR_DEVICE_TYPE_ERROR      -2
#define ERROR_DEVICE_OPEN_FALIED     -3
#define ERROR_DEVICE_CAP_NOT_SUPPORT -4
#define ERROR_DEVICE_CAP_ERROR       -5

#define ERROR_VIDIOC_QUERYBUF        -6
#define ERROR_VIDIOC_QBUF            -7
#define ERROR_VIDIOC_DQBUF           -70
#define ERROR_REQBUFS                -71

#define ERROR_MMAP_FAILD             -8
#define ERROR_UNMMAP_FAILD           -88
#define ERROR_LOCAL                  -9
#define ERROR_VIDIOC_STREAMON        -10
#define ERROR_VIDIOC_STREAMOFF       -11
#define ERROR_SELECT                 -12

#define SUCCESSED                     0
#define CAP_BUF_SIZE                  4

#define CLEAR(x) memset (&(x), 0, sizeof (x))

//Defines video stream output format, used by the capture device.
typedef struct
{
    unsigned int videoType;
    unsigned int videoWidth;
    unsigned int videoHeight;
    unsigned int videoFrameRate;
    unsigned int deviceIndex;
    
}VCapFormat_t;

/////////////////////////////////////////////////////////////////////////////////////
class CV4L2Capture
{
public:
    CV4L2Capture(void);
    ~CV4L2Capture(void);
    
    //Open video capture device by V4L2 Media Foundation platform.
    int CameraOpen(int devIndex, VCapFormat_t *vcapParam);
    
    //Close the video capture device and release the memory.
    int CameraClose(void);
    
    //Start video capture device to capture the video stream.
    int CameraStart(int devIndex);
    
    //Stop the video capture device and pause the thread.
    int CameraStop(void);
    
    //Read out a video frame from the capture device's buffer.
    int FrameRead(char *buffer, int *width, int *height);
    
    //Read out a video I420 frame from the capture device's buffer.
    int FrameReadI420(char *buffer, int *width, int *height);
    
    //Get and print the capability of the camera devices.
    int GetCapability(int devIndex);
    
private:
    
    //send request command to the device and get the results.
    int xioctl(int fd, int request, void *arg);
    
private:
    
    //the local control parameters for the V4L2 devices.
    VCapFormat_t    m_InputFormat;
    int             m_CameraFd;
    int             m_DevInitFlag;
    int             m_CameraWidth;
    int             m_CameraHeight;
    char            m_DeviceName[64];
    char *          m_YUVPlane;
    
    unsigned char*  m_pUserBuffer[CAP_BUF_SIZE];
    unsigned int    m_BufferLength[CAP_BUF_SIZE];
    unsigned int    m_BufferCount;
    unsigned int    m_FrameCaptured;
};

#endif  // End of __VCAPTURE_NATIVE_H__

/////////////////////////////////////////////////////////////////////////////////////

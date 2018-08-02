/////////////////////////////////////////////////////////////////////////////////////

#include "vcapture.h"
#include "GPU_codec_api.h"

/////////////////////////////////////////////////////////////////////////////////////
CV4L2Capture::CV4L2Capture(void)
{
    //Initialize local control parameters of V4L2 devices.
    memset(&m_InputFormat, 0, sizeof(VCapFormat_t));
    
    m_CameraFd      = -1;
    m_DevInitFlag   = 0;
    m_FrameCaptured = 0;
    m_BufferCount   = CAP_BUF_SIZE;
    
    for (unsigned int i = 0; i < m_BufferCount; i++)
    {
        m_BufferLength[i] = 0;
        m_pUserBuffer[i] = NULL;
    }
    
    m_YUVPlane = new uint8_t[2048];
}

/////////////////////////////////////////////////////////////////////////////////////
CV4L2Capture::~CV4L2Capture(void)
{
    //Close the camera device if necessary.
    if (m_DevInitFlag != 0)
    {
        CameraClose();
    }
    
    delete[] m_YUVPlane;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::CameraOpen(int devIndex, VCapFormat_t *vcapParam)
{
    //initialize the input video camera capture parameters.
    m_InputFormat.videoType = vcapParam->videoType;
    m_InputFormat.videoWidth = vcapParam->videoWidth;
    m_InputFormat.videoHeight = vcapParam->videoHeight;
    m_InputFormat.videoFrameRate = vcapParam->videoFrameRate;
    m_InputFormat.deviceIndex = vcapParam->deviceIndex;
    
    sprintf(m_DeviceName,"/dev/video%d", devIndex);
    
    //open the camera device with valid device name and type.
    struct stat st;
	if (-1 == stat(m_DeviceName, &st))
	{
		return ERROR_DEVICE_NOT_EXIST;
	}
    
	if (!S_ISCHR(st.st_mode))
	{
		return ERROR_DEVICE_TYPE_ERROR;
	}
    
    m_CameraFd = open(m_DeviceName, O_RDWR, 0);
	if (-1 == m_CameraFd)
	{
		return ERROR_DEVICE_OPEN_FALIED;
	}
	
	//get the features of current device, with "VIDIOC_QUERYCAP".
	struct v4l2_capability cap;
	CLEAR (cap);
	
	if (-1 == xioctl(m_CameraFd, VIDIOC_QUERYCAP, &cap))
	{
	    return ERROR_DEVICE_CAP_ERROR;
	}
	
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	{
		return ERROR_DEVICE_CAP_NOT_SUPPORT;
	}
	
	if (!(cap.capabilities & V4L2_CAP_STREAMING))
	{
		return ERROR_DEVICE_CAP_NOT_SUPPORT;
	}
	
	//Setting the video format of the camera output frames.
	struct v4l2_format fmt;
	CLEAR (fmt);
	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = vcapParam->videoWidth;
	fmt.fmt.pix.height = vcapParam->videoHeight;
	fmt.fmt.pix.pixelformat = vcapParam->videoType;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
	
	if (-1 == xioctl(m_CameraFd, VIDIOC_S_FMT, &fmt))
	{
		return ERROR_DEVICE_CAP_ERROR;
	}
	
	// Note VIDIOC_S_FMT may change width and height.
	unsigned int min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;
    
    for (unsigned int i = 0; i < m_BufferCount; i++)
    {
        m_BufferLength[i] = fmt.fmt.pix.sizeimage;
    }
    
    m_CameraWidth = fmt.fmt.pix.width;
    m_CameraHeight = fmt.fmt.pix.height;
	
	//set the framerate of the capture stream.
	struct v4l2_streamparm streamParm;
	memset(&streamParm, 0, sizeof(struct v4l2_streamparm));
	
	streamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	streamParm.parm.capture.timeperframe.denominator = vcapParam->videoFrameRate;
	streamParm.parm.capture.timeperframe.numerator = 1;
	
	if (-1 == xioctl(m_CameraFd, VIDIOC_S_PARM, &streamParm))
	{
		return ERROR_DEVICE_CAP_ERROR;
	}
	
	//Update the capture parameters with current setting.
	struct v4l2_streamparm curParm;
	memset(&curParm, 0, sizeof(struct v4l2_streamparm));
	curParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	if (0 == xioctl(m_CameraFd, VIDIOC_G_PARM, &curParm))
	{
	    unsigned int denominator = curParm.parm.capture.timeperframe.denominator;
	    if (vcapParam->videoFrameRate != denominator)
	    {
	        return ERROR_DEVICE_CAP_NOT_SUPPORT;
	    }
	}
	
	//Update the camera initialize flag to open device.
    m_DevInitFlag = 1;
    m_FrameCaptured = 0;
	
	//All are ok, return the state.
	return SUCCESSED;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::CameraClose(void)
{
    int ret_code = SUCCESSED;
    
    //close the camera if it had been opened already.
    if (m_DevInitFlag != 0)
    {
        if (-1 == close(m_CameraFd))
        {
        	ret_code = ERROR_LOCAL;
        }
    }
    
    //release the memory buffer, which used for camera.
    for (unsigned int i = 0; i < m_BufferCount; i++)
    {
        if (m_pUserBuffer[i] != NULL)
        {
            munmap(m_pUserBuffer[i], m_BufferLength[i]);
            m_pUserBuffer[i] = NULL;
            m_BufferLength[i] = 0;
        }
    }
    
    //Update the camera initialize flag to close device.
    m_CameraFd = -1;
    m_DevInitFlag = 0;
    
    //Succed to close the encoder, return the result.
    return ret_code;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::CameraStart(int devIndex)
{
    //check the camera device index.
    if (devIndex != (int)m_InputFormat.deviceIndex)
    {
        return ERROR_DEVICE_CAP_ERROR;
    }
    
    //initialize the capture buffer mode, we use user_ptr mode.
    struct v4l2_requestbuffers req;
    CLEAR (req);
    
    req.count = m_BufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (-1 == xioctl (m_CameraFd, VIDIOC_REQBUFS, &req))
    {
        return ERROR_UNMMAP_FAILD;
    }
    
    //allocate buffers which used for capture YUV 4:2:2 frame.
    for (unsigned int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        CLEAR (buf);
        
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        
        if (-1 == xioctl(m_CameraFd, VIDIOC_QUERYBUF, &buf))
        {
            return ERROR_UNMMAP_FAILD;
        }
        
        m_BufferLength[i] = buf.length;
        m_pUserBuffer[i] = (unsigned char *)mmap(NULL, buf.length, 
                           PROT_READ | PROT_WRITE, MAP_SHARED,
                           m_CameraFd, buf.m.offset);
        
        if (m_pUserBuffer[i] == MAP_FAILED)
        {
            return ERROR_UNMMAP_FAILD;
        }
    }
    
    //start the camera device, waiting for the video frames.
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (-1 == xioctl(m_CameraFd, VIDIOC_STREAMON, &type))
    {
        return ERROR_UNMMAP_FAILD;
    }
    
    //Succeed to start the camera, return the code.
    return SUCCESSED;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::CameraStop(void)
{
    //close the camera device and stop capturing frames.
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (-1 == xioctl(m_CameraFd, VIDIOC_STREAMOFF, &type))
    {
        return ERROR_UNMMAP_FAILD;
    }
    
    //release the memory buffer, which used for camera.
    for (unsigned int i = 0; i < m_BufferCount; i++)
    {
        if (m_pUserBuffer[i] != NULL)
        {
            munmap(m_pUserBuffer[i], m_BufferLength[i]);
            m_pUserBuffer[i] = NULL;
            m_BufferLength[i] = 0;
        }
    }
    
    //Succeed to stop the camera, return the code.
    return SUCCESSED;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::FrameRead(char *buffer, int *width, int *height)
{
    unsigned int i;
    
    //the camera device must be opened in the first.
	if (m_DevInitFlag == 0)
	{
	    return ERROR_DEVICE_OPEN_FALIED;
	}
	
	//listening to the camera device port for reading video frames.
	fd_set fds;
    FD_ZERO (&fds);
    FD_SET (m_CameraFd, &fds);
    
    int ret = select(m_CameraFd + 1, &fds, NULL, NULL, NULL);
    if (ret <= 0)
    {
        return ERROR_SELECT;
    }
    
    //Get the camera device buffer with non-block I/O reading.
    struct v4l2_buffer buf;
    CLEAR (buf);
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (-1 == xioctl(m_CameraFd, VIDIOC_DQBUF, &buf))
    {
        //ignore the EIO error code, nothing mind.
        if (errno != EIO)
        {
            return ERROR_SELECT;
        }
    }
    
    //valid the buffer index, no more than array size.
    if (buf.index >= m_BufferCount)
    {
        return ERROR_SELECT;
    }
    
    //output the incoming video frame data and image format.
    int frame_size_byte = buf.bytesused;
    
    memcpy(buffer, m_pUserBuffer[buf.index], frame_size_byte);
    *width = m_CameraWidth;
    *height = m_CameraHeight;
    
    //queued the frame buffer to the camera device buffer list.
    if (-1 == xioctl(m_CameraFd, VIDIOC_QBUF, &buf))
    {
        return ERROR_SELECT;
    }
    
    //return the incoming frame length to the caller.
    return frame_size_byte;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::FrameReadI420(char *buffer, int *width, int *height)
{
    int i, j;
    int frame_size_byte = 0;
    
    //the camera device must be opened in the first.
	if (m_DevInitFlag == 0)
	{
	    return ERROR_DEVICE_OPEN_FALIED;
	}
	
	//listening to the camera device port for reading video frames.
	fd_set fds;
    FD_ZERO (&fds);
    FD_SET (m_CameraFd, &fds);
    
    int ret = select(m_CameraFd + 1, &fds, NULL, NULL, NULL);
    if (ret <= 0)
    {
        return ERROR_SELECT;
    }
    
    //Get the camera device buffer with non-block I/O reading.
    struct v4l2_buffer buf;
    CLEAR (buf);
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (-1 == xioctl(m_CameraFd, VIDIOC_DQBUF, &buf))
    {
        //ignore the EIO error code, nothing mind.
        if (errno != EIO)
        {
            return ERROR_SELECT;
        }
    }
    
    //valid the buffer index, no more than array size.
    if (buf.index >= m_BufferCount)
    {
        return ERROR_SELECT;
    }
    
    //output the incoming video frame data and image format.
    if (buf.bytesused > 0)
    {
        if (m_InputFormat.videoType == V4L2_PIX_FMT_YUYV)
        {
            int dst_size_y = m_CameraWidth * m_CameraHeight;
            int dst_size_u = (m_CameraWidth >> 1) * (m_CameraHeight >> 1);
            int dst_size_v = (m_CameraWidth >> 1) * (m_CameraHeight >> 1);
            
            char *dst_buf_y = buffer;
            char *dst_buf_u = dst_buf_y + dst_size_y;
            char *dst_buf_v = dst_buf_u + dst_size_u;
            
            char *src_buf_y = (char *)m_pUserBuffer[buf.index];
            char *src_buf_u = src_buf_y + 1;
            char *src_buf_v = src_buf_y + 3;
            
            //copy the Y plane to the destination buffer, YU YV YU YV.
            for (i = 0; i < dst_size_y; i++)
            {
                *dst_buf_y = *src_buf_y;
                dst_buf_y += 1;
                src_buf_y += 2;
            }
            
            //copy the U/V plane to the destination buffer, YU YV YU YV.
            for (i = 0; i < m_CameraHeight; i += 2)
            {
                for (j = 0; j < m_CameraWidth; j += 2)
                {
                    *dst_buf_u = *src_buf_u;
                    dst_buf_u += 1;
                    src_buf_u += 4;
                    
                    *dst_buf_v = *src_buf_v;
                    dst_buf_v += 1;
                    src_buf_v += 4;
                }
                
                src_buf_u += m_CameraWidth * 2;
                src_buf_v += m_CameraWidth * 2;
            }
            
            //update the output frame parameters with I420 format.
            frame_size_byte = dst_size_y + dst_size_u + dst_size_v;
            *width = m_CameraWidth;
            *height = m_CameraHeight;
        }
    }
    
    //queued the frame buffer to the camera device buffer list.
    if (-1 == xioctl(m_CameraFd, VIDIOC_QBUF, &buf))
    {
        return ERROR_SELECT;
    }
    
    //return the incoming frame length to the caller.
    return frame_size_byte;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::xioctl(int fd, int request, void *arg)
{
    int r = 0;
	do {
		r = ioctl(fd, request, arg);
	} while (-1 == r && EINTR == errno);
    
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////
int CV4L2Capture::GetCapability(int devIndex)
{
    struct v4l2_capability dev_cap;
	struct v4l2_format dev_fmt;
	struct v4l2_streamparm dev_stream;
	
	//the camera device must be opened in the first.
	if (m_DevInitFlag == 0)
	{
	    printf("the camera had not been opened at %d\n", devIndex);
        return ERROR_DEVICE_CAP_ERROR;
	}
	
    //check the camera device index.
    if (devIndex != (int)m_InputFormat.deviceIndex)
    {
        printf("the camera index is error at %d\n", devIndex);
        return ERROR_DEVICE_CAP_ERROR;
    }
	
    //get the features of current device, with "VIDIOC_QUERYCAP".
    if (-1 == xioctl(m_CameraFd, VIDIOC_QUERYCAP, &dev_cap))
    {
        if (EINVAL == errno)
    	{
    		printf("%s is no V4L2 device\n", m_DeviceName);
    		return ERROR_DEVICE_CAP_ERROR;
    	}
    	else
    	{
    		printf("%s error %d, %s\n", "VIDIOC_QUERYCAP", errno, strerror(errno));
    		return ERROR_DEVICE_CAP_ERROR;
    	}
    }
	
    //output the capture capability of camera devices.
    printf("\nVIDOOC_QUERYCAP\n");
    printf("the camera driver is %s\n", dev_cap.driver);
    printf("the camera card is %s\n", dev_cap.card);
    printf("the camera bus info is %s\n", dev_cap.bus_info);
    printf("the version is %d\n", dev_cap.version);
    
    if (!(dev_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
    	printf( "%s is no video capture device\n", m_DeviceName);
    	return ERROR_DEVICE_CAP_ERROR;
    }
    
    if (!(dev_cap.capabilities & V4L2_CAP_STREAMING))
    {
    	printf( "%s does not support streaming i/o\n", m_DeviceName);
    	return ERROR_DEVICE_CAP_ERROR;
    }
	
    //display the supportted video capture format.
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("Support format:\n");
    
    while (ioctl(m_CameraFd,VIDIOC_ENUM_FMT, &fmtdesc) != -1)
    {
        printf("\t%d.%s\n", fmtdesc.index + 1, fmtdesc.description);
        fmtdesc.index++;
    }
    
    //Get the current capture format.
    memset(&dev_fmt, 0, sizeof(struct v4l2_format));
    dev_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (-1 == ioctl(m_CameraFd, VIDIOC_G_FMT, &dev_fmt))
    {
        printf("%s error %d, %s\n", "VIDIOC_G_FMT", errno, strerror(errno));
        return ERROR_DEVICE_CAP_ERROR;
    }
    
    printf("Current Format: width is %d\n", dev_fmt.fmt.pix.width);
    printf("Current Format: height is %d\n", dev_fmt.fmt.pix.height);
    
    if (dev_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
    {
        printf("Current Format: pixelformat is V4L2_PIX_FMT_MJPEG\n");
    }
    else if (dev_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
    {
        printf("Current Format: pixelformat is V4L2_PIX_FMT_YUYV\n");
    }
    else
    {
        printf("Current Format: pixelformat is %d\n", dev_fmt.fmt.pix.pixelformat);
    }
    
    //Get the current stream parameters.
    memset(&dev_stream, 0, sizeof(struct v4l2_format));
    dev_stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (-1 == xioctl(m_CameraFd, VIDIOC_G_PARM, &dev_stream))
    {
        printf("%s error %d, %s\n", "VIDIOC_G_PARM", errno, strerror(errno));
        return ERROR_DEVICE_CAP_ERROR;
    }
    
    printf("Current Format: framerate is %d\n", dev_stream.parm.capture.timeperframe.denominator);
    printf("Current Format: capability is %d\n", dev_stream.parm.capture.capability);
    printf("Current Format: capturemode is %d\n", dev_stream.parm.capture.capturemode);
    
    //All are ok, return the state.
    return SUCCESSED;
}


#include "vcapture.h"
#include <pthread.h>

char g_yuv_buffer[1024*4096];

volatile int   g_capture_stop = 0;

/////////////////////////////////////////////////////////////////////////////////////////
void *thread_capture(void *argu)
{
    FILE * outFile = fopen("cap_frames.yuv", "wb");
	if (!outFile)
	{
		printf("open YUV file error!\n");
		return NULL;
	}
	
	VCapFormat_t format;
    format.videoWidth = 1280;
    format.videoHeight = 720;
    format.videoFrameRate = 10;
    format.deviceIndex = 0;
    format.videoType = V4L2_PIX_FMT_YUYV;
	
    CV4L2Capture *capture = new CV4L2Capture;
    int ret = capture->CameraOpen(format.deviceIndex, &format);
    if (ret != SUCCESSED)
    {
        printf("failed to open camera with %d, error code is %d\n", format.deviceIndex, ret);
        return NULL;
    }
    
    ret = capture->CameraStart(format.deviceIndex);
    if (ret != SUCCESSED)
    {
        printf("failed to start video camera with %d, error code is %d\n", format.deviceIndex, ret);
        return NULL;
    }
    
    printf("start to capture video frames...\n");
    
    int file_length = 0;
    int total_frames = 0;
    int width = 0;
    int height = 0;
    
    capture->GetCapability(0);
    
    while (total_frames < 50)
    {
        int ret = capture->FrameReadI420(g_yuv_buffer, &width, &height);
        if (ret <= 0)
        {
            continue;
        }
        
        fwrite(g_yuv_buffer, 1, ret, outFile);
        file_length += ret;
        
		total_frames++;
        if ((total_frames % 25) == 0)
        {
            printf("now captured %d frames. file_length = %d\n", total_frames, file_length);
        }
    }
    
    /////////////////////////////////////////////////////////////////////////////////////
    capture->CameraStop();
    capture->CameraClose();
    delete capture;
    fclose(outFile);
    
    printf("totaly captured %d frames.\n", total_frames);
    
    g_capture_stop = 1;
    return argu;
}

//////////////////////////////////////////////////////////////////////////////////////////
int main(void)
{
    struct timeval time1, time2;
    struct timezone tz;
    int time_cost_ms = 0;
    
	printf("\r\n\r\nstart the main program...\n");
	printf("*******************************************************\n");
    
    gettimeofday (&time1 , &tz);
    
    ///////////////////////////////////////////////////////////////////////
    pthread_t tid_cap;
    int err = pthread_create(&tid_cap, NULL, thread_capture, NULL);
    if (err != 0)
    {
        printf("can't create thread: %s\n", strerror(err));
        return -1;
    }
    else
    {
        printf("create video capture thread OK\n");
    }
    
    g_capture_stop = 0;
    while (g_capture_stop == 0)
    {
        usleep(1000*100);
    }
        
    ///////////////////////////////////////////////////////////////////////
    
	gettimeofday (&time2 , &tz);
	time_cost_ms = (time2.tv_sec - time1.tv_sec) * 1000 + (time2.tv_usec - time1.tv_usec) / 1000;

	printf("\ntotal cost time %d ms\n", time_cost_ms);
	
	printf("\n*******************************************************\n");
	printf("video encoding is over!\n");
	
	usleep(2000 * 1000);
	
	return 0;
}

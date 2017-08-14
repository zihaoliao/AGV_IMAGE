#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>  
#include <fcntl.h>  
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <libv4l2.h>
#include <libv4l1.h>
#include <pthread.h>
#include <sched.h>//对于线程的优先定义
#include "v4l2_video.h"
#include "avilib.h"
#include "log.h"
#define VIDEO_LOG 0
#define VIDEO_SAVE 0
#define PRINT_LOG 0
#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define CV_PI   3.141592653589//7932384626433832795
#define IMAGE_WIDTH 320
#define IMAGE_HEIGHT 240
#define MAX_RADIUS 400	//sqrt(IMAGE_WIDTH*IMAGE_WIDTH+IMAGE_HEIGHT*IMAGE_HEIGHT)
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 600
#define HSV_RED 25.0
#define MIN_RED 45
//全局变量的定义
//获取视频相关
struct buffer {       //该结构体定义了一个开始，和一个采集长度的数据
	unsigned char * start;
	size_t length;
};
//对于加锁的一些定义
static int V4L2_flag = 0;
pthread_mutex_t mutex_v4l2;                   //发送定义锁

static int time_in_sec_capture=1000000;
unsigned char *get_v4l2=NULL;

static char * dev_name = NULL;
static int fd = -1;
struct buffer * buffers = NULL;

static unsigned int n_buffers = 0;
static int fbfd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static char *fbp=NULL;
static long screensize=0;
struct v4l2_buffer              buf;
struct v4l2_requestbuffers      req;
enum v4l2_buf_type              type;

//关于图像处理的定义
FILE *fp;
double v_scbroblast[3];
int mWalkFlag = 1;
static long int frame = 0;

//延时定义
int timeVal = 0, timeVal1 = 0;

//建立线程函数
pthread_t v4l2_video_thread_id;
void* v4l2_video_Update_Handler(void* arg);
void v4l2_video_Create_Update_Thread();


//unsigned char *tmpbuffer=NULL;
static void errno_exit (const char * s)
{
	fprintf (stderr, "%s error %d, %s\n",s, errno, strerror (errno));
	exit (EXIT_FAILURE);
}


//对于摄像头的命令定义
static int xioctl (int fd,int request,void * arg)
{
	int r;
	do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);
	return r;
}
//停止采集
static void stop_capturing (void)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
	errno_exit ("VIDIOC_STREAMOFF");
}

//开始采集图像
static void start_capturing (void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;
		CLEAR (buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
		errno_exit ("VIDIOC_QBUF");
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl (fd, VIDIOC_STREAMON, &type)) //开始视频显示函数
	errno_exit ("VIDIOC_STREAMON");

}

//注销设备文件
static void uninit_device (void)
{
	unsigned int i;

	for (i = 0; i < n_buffers; ++i)
		if (-1 == munmap (buffers[i].start, buffers[i].length))
			errno_exit ("munmap");

	if (-1 == munmap(fbp, screensize)){
		printf(" Error: framebuffer device munmap() failed.\n");
		exit (EXIT_FAILURE) ;
	}    
	free (buffers);
}

static void init_mmap (void)
{
	struct v4l2_requestbuffers req; //向驱动申请贞缓冲，一般不超过5个
	//mmap framebuffer
	fbp = (char *)mmap(NULL,screensize,PROT_READ | PROT_WRITE,MAP_SHARED ,fbfd, 0);
	if ((int)fbp == -1) {
		printf("Error: failed to map framebuffer device to memory.\n");
		exit (EXIT_FAILURE) ;
	}
	memset(fbp, 0, screensize);
	CLEAR (req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
    //申请count大小的缓存。
	if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s does not support memory mapping\n", dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 4) {    //if (req.count < 2)
		fprintf (stderr, "Insufficient buffer memory on %s\n",dev_name);
		exit (EXIT_FAILURE);
	}

	buffers = calloc (req.count, sizeof (*buffers));

	if (!buffers) {
		fprintf (stderr, "Out of memory\n");
		exit (EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
		errno_exit ("VIDIOC_QUERYBUF");

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start =(unsigned char *)mmap (NULL,buf.length,PROT_READ | PROT_WRITE ,MAP_SHARED,fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
		errno_exit ("mmap");
	}

}
//初始化摄像头
static void init_device (void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;


	// Get fixed screen information
	if (-1==xioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
		printf("Error reading fixed information.\n");
		exit (EXIT_FAILURE);
	}

	// Get variable screen information
	if (-1==xioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		printf("Error reading variable information.\n");
		exit (EXIT_FAILURE);
	}
	screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;


	if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s is no V4L2 device\n",dev_name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf (stderr, "%s is no video capture device\n",dev_name);
		exit (EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming i/o\n",dev_name);
		exit (EXIT_FAILURE);
	}



	CLEAR (cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;

		if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
			case EINVAL:    
				break;
			default:
				break;
			}
		}
	}else {     }

	CLEAR (fmt);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width =IMAGE_WIDTH ;  
	fmt.fmt.pix.height =IMAGE_HEIGHT;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
	errno_exit ("VIDIOC_S_FMT");
	init_mmap ();

}
//关闭摄像头设备
static void close_device (void)
{
	if (-1 == close (fd))
	errno_exit ("close");
	fd = -1;
	close(fbfd);
}
//打开摄像头设备
static void open_device (void)
{
	struct stat st;  

	if (-1 == stat (dev_name, &st)) {
		fprintf (stderr, "Cannot identify '%s': %d, %s\n",dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf (stderr, "%s is no device\n", dev_name);
		exit (EXIT_FAILURE);
	}

	//open framebuffer
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd==-1) {
		printf("Error: cannot open framebuffer device.\n");
		exit (EXIT_FAILURE);
	}

	//open camera
	//fd= open("ceshi.h264",O_RDWR| O_NONBLOCK,0);
	fd = open (dev_name, O_RDWR| O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf (stderr, "Cannot open '%s': %d, %s\n",dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
}

static void usage (FILE * fp,int argc,char ** argv)
{
	fprintf (fp,
	"Usage: %s [options]\n\n"
	"Options:\n"
	"-d | --device name Video device name [/dev/video]\n"
	"-h | --help Print this message\n"
	"-t | --how long will display in seconds\n"
	"",
	argv[0]);
}

static const char short_options [] = "d:ht:";
static const struct option long_options [] = {
	{ "device", required_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ "time", no_argument, NULL, 't' },
	{ 0, 0, 0, 0 }
};
/**
 * @brief 初始化摄像头的外设和初始化线程
 * @param 无
 * @retval 
 *  @arg  0 初始化成功, 
 *  @arg >0 错误号 
 *TODO:定义具体错误号
 */
uint8_t v4l2_video_Init(int argc,char ** argv){
     
    dev_name = "/dev/video0";//设备名称
   
	for (;;)  
	{
		int index;
		int c;

		c = getopt_long(argc, argv,short_options, long_options,&index);
		if (-1 == c)
		break;

		switch (c) {
		case 0:
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'h':
			usage (stdout, argc, argv);
			exit (EXIT_SUCCESS);
		case 't':
			time_in_sec_capture = atoi(optarg);
			break;

		default:
			usage (stderr, argc, argv);
			exit (EXIT_FAILURE);
		}
	}
    printf("good at long_options\n");
	open_device ();//打开摄像头
	printf("good at open_device\n");
	init_device ();//初始化摄像头
	printf("good at init_device\n");
	start_capturing ();//开始采集图像
	printf("good at start_capturing\n");
   
	//建立相关线程
    v4l2_video_Create_Update_Thread();
}

void v4l2_video_Create_Update_Thread(){
    pthread_attr_t thread_attr;
	struct sched_param schedule_param;
	pthread_attr_init(&thread_attr);
	schedule_param.sched_priority = 99;
	pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED); //有这行，设置优先级才会生效
	pthread_attr_setschedpolicy(&thread_attr,SCHED_RR);
	pthread_attr_setschedparam(&thread_attr, &schedule_param);
	pthread_create(&v4l2_video_thread_id, &thread_attr,v4l2_video_Update_Handler, NULL);
}

void  markFrame(int max_x, int max_y, int color)
{

	short *dest_b = (short *)(fbp)+vinfo.yoffset * 1024 + vinfo.xoffset+680;//在指定点做标记	
	if (max_x >= 4 && max_y >= 4 && max_x <= (IMAGE_WIDTH - 4) && max_y <= (IMAGE_HEIGHT - 4))
	{
		dest_b[max_y*SCREEN_WIDTH + max_x - 1] = color;
		dest_b[max_y*SCREEN_WIDTH + max_x] = color;
		dest_b[max_y*SCREEN_WIDTH + max_x + 1] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x - 1] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x + 1] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x - 1] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x + 1] = color;
		dest_b[max_y*SCREEN_WIDTH + max_x - 2] = color;
		dest_b[max_y*SCREEN_WIDTH + max_x - 3] = color;
		dest_b[max_y*SCREEN_WIDTH + max_x + 2] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x - 2] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x + 3] = color;
		dest_b[(max_y - 1)*SCREEN_WIDTH + max_x + 2] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x - 2] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x - 3] = color;
		dest_b[(max_y + 1)*SCREEN_WIDTH + max_x + 2] = color;
	}
	else;// printf("max point is on the border\n");
}
void  markOnePoint(int max_x, int max_y, int color)
{
	if (max_x <= 1 || max_x >= IMAGE_WIDTH - 2)
		return;
	short *dest_b = (short *)(fbp)+vinfo.yoffset * 1024 + vinfo.xoffset+680;//在指定点做标记	
	dest_b[max_y*SCREEN_WIDTH + max_x - 1] = color;
	dest_b[max_y*SCREEN_WIDTH + max_x] = color;
	dest_b[max_y*SCREEN_WIDTH + max_x + 1] = color;
}

//图像处理函数
int int_abs(int a)
{  return (a>0?a:-a);  }
// (∑Xi)/n //(∑Yi)/n
double Sum_Average(double d[IMAGE_HEIGHT],double k[IMAGE_HEIGHT])
{
	unsigned int i=0;
	double z=0;
	int mm=0;
	for(i=0;i<IMAGE_HEIGHT;i++)
	{
		if(k[i]>=0.0&&d[i]>=0)
		{z = z + d[i];
			mm++;
		}
	}
	if (mm == 0)
		return -1;
	z = z/mm;
	return z;
}
//∑(Xi Yi)
double X_Y_By(double m[IMAGE_HEIGHT],double n[IMAGE_HEIGHT])
{
	unsigned int i=0;
	double z=0;
	int mm=0;
	for(i=0;i<IMAGE_HEIGHT;i++)
	{
		if(m[i]>=0.0&&n[i]>=0)
		{
			z = z + m[i]*n[i];
			mm++;
		}
	}
	return z;
}
//∑(Xi ^ 2)
double Squre_sum(double c[IMAGE_HEIGHT])
{
	unsigned int i=0;
	double z=0;
	for(i=0;i<IMAGE_HEIGHT;i++)
	{
		if(c[i]>=0.0)
		z = z + c[i]*c[i];
	}
	return z;
}
//R = （∑Yi) / n - a1（∑Xi) / n 
//K	=	[n∑(Xi Yi) - （∑Xi ∑Yi)] / (n∑Xi ^ 2 - ∑Xi∑Xi) 
//		=	[∑(Xi Yi) - 1/n*（∑Xi ∑Yi)] / (∑Xi ^ 2 - 1/n*∑Xi∑Xi)  整数溢出？
//最小二乘拟合
double* leastSquare(double center_middle[IMAGE_HEIGHT]){
	int min=20;
	double y_line[IMAGE_HEIGHT];
	double x_pixl_of_second_left_margin[IMAGE_HEIGHT];
	int m = 1;
	int i = 0;
	int count_times = 2;//算
	double K, K1, R, R1;
	static double K_line[2] = { 0 };
	//去掉不连续的点
	while (count_times>0)
	{
		//printf("<><><><><><>%d\t",count_times);
		for ( i = 0; i<(IMAGE_HEIGHT - 1); i++)
		{
			while (center_middle[i] == -1.0&&i<IMAGE_HEIGHT)//得到第i行
				i++;
			m = 1;
			while (center_middle[i + m] == -1.0 && (i + m)<IMAGE_HEIGHT)//得到下一个可用行
			{
				m++;
			}
			if (((i + m)<IMAGE_HEIGHT) && ((center_middle[i] - center_middle[i + m]>15) || (center_middle[i] - center_middle[i + m]<-15)))
			{   				//这代表？判断准则：如果两个可用行的横向间隔超过20，都舍弃，继续判断接下来的行；
				center_middle[i] = -1.0;
				center_middle[i + m] = -1.0;
				count_times = 5;//？抵消掉断开的
				i = i + m;
			}
		}
		count_times--;
	}
	//R = （∑Yi) / n - a1（∑Xi) / n 
	//K= [n∑(Xi Yi) - （∑Xi ∑Yi)] / (n∑Xi ^ 2 - ∑Xi∑Xi) = [∑(Xi Yi) - 1/n*（∑Xi ∑Yi)] / (∑Xi ^ 2 - 1/n*∑Xi∑Xi)  整数溢出？
	//最小二乘拟合
	int k_count = 0;
	for ( i = 0; i<IMAGE_HEIGHT; i++)
	{
		x_pixl_of_second_left_margin[i] = center_middle[i];//做两次最小二乘，横着一次，竖着一次
		y_line[i] = i + 1.0;
		//markFrame(center_middle[i], i, 63488);
		if (center_middle[i]<0.0)
			k_count++;
	}
	//无效点的数目超过IMAGE_HEIGHT-min个点
	if (k_count >= (IMAGE_HEIGHT-min)){
		K_line[0] = 0;
		K_line[1] = 0;
		return K_line;
	}
	double x_sum_average = Sum_Average(y_line, center_middle);// (∑Xi)/n
	double y_sum_average = Sum_Average(center_middle, center_middle);//(∑Yi)/n
	if (y_sum_average != -1){
		double x_square_sum = Squre_sum(y_line);//∑(Xi ^ 2)
		double x_multiply_y = X_Y_By(y_line, center_middle); //∑(Xi Yi)
		if ((x_square_sum - (IMAGE_HEIGHT - k_count) * x_sum_average*x_sum_average) == 0)
		{
			K = 0;
			R = 0;
		}
		else
		{
			K = (x_multiply_y - (IMAGE_HEIGHT - k_count)* x_sum_average * y_sum_average) / (x_square_sum - (IMAGE_HEIGHT - k_count) * x_sum_average*x_sum_average);
			R = y_sum_average - K * x_sum_average;
		}//printf("K = %f\tR = %f\n",K,R);
	}
	else {
		K = 0;
		R = 0;
	}

	double x_sum_average1 = Sum_Average(x_pixl_of_second_left_margin, x_pixl_of_second_left_margin);
	double y_sum_average1 = Sum_Average(y_line, x_pixl_of_second_left_margin);
	if (y_sum_average1 != -1){
		double x_square_sum1 = Squre_sum(x_pixl_of_second_left_margin);
		double x_multiply_y1 = X_Y_By(x_pixl_of_second_left_margin, y_line);

		if ((x_square_sum1 - (IMAGE_HEIGHT - k_count) * x_sum_average1*x_sum_average1) == 0)
		{
			K = 0;
			R = 0;
		}
		else
		{
			K1 = (x_multiply_y1 - (IMAGE_HEIGHT - k_count)* x_sum_average1 * y_sum_average1) / (x_square_sum1 - (IMAGE_HEIGHT - k_count) * x_sum_average1*x_sum_average1);
			R1 = y_sum_average1 - K1 * x_sum_average1;
		}
	}
	else {
		K1 = 0;
		R1 = 0;
	}
	if (K == 0 && R == 0 && K1 == 0 && R1 == 0)  {
		K_line[0] = 0;
		K_line[1] = 0;
		return K_line;
	}
	else if (K == 0 && R == 0) { K_line[0] = 1 / K1; K_line[1] = R1; }
	else if (K1 == 0 && R1 == 0) { K_line[0] = K; K_line[1] = R; }
	else
	{
		//计算两条直线的累积误差,按照误差小的决定控制点
		double sum_error_line = 0;
		double sum_error_line1 = 0;
		for ( i = 0; i < 240; i++)
		if (center_middle[i] >= 0.0)
		{
			sum_error_line += int_abs((int)(K*i + R - center_middle[i]));//拟合出的直线的累积误差，横着拟合
			sum_error_line1 += int_abs((int)((i - R1) / K1 - x_pixl_of_second_left_margin[i]));//拟合出的直线1的累积误差，竖着拟合
		}
		if (sum_error_line1 <= sum_error_line)//哪个的误差小，就用哪个的中心点来计算v_scbrob
		{
			K_line[0] = 1 / K1;
			K_line[1] = -R1 / K1;
		}
		else
		{
			K_line[0] = K;
			K_line[1] = R;
		}
	}
	return K_line;
}

double midFilter(double last_five_K[5]){
	double sorted[5];
	int i, j;
	for (i = 0; i < 5; i++)
	{
		sorted[i] = last_five_K[i];
	}
	for (i = 1; i < 5; i++){
		j = i;
		while (j >0 && sorted[j - 1]> sorted[j])
		{
			double temp = sorted[j - 1];
			sorted[j - 1] = sorted[j];
			sorted[j] = temp;
			j = j - 1;
		}
	}
	return sorted[2];
}


	

//线程里需要处理的函数
void* v4l2_video_Update_Handler(void* arg){
	struct timeval tpstart,tpend;
	double timeuse;
	int ret;
	double center_middle[IMAGE_HEIGHT];//中间边缘的中心点的x坐标
	double left_middle[IMAGE_HEIGHT];//左边缘的中心点的x坐标
	double right_middle[IMAGE_HEIGHT];//右边缘的中心点的x坐标
	double six_margin[7] = { -1 };
	double *K_R;//最小二乘法返回值
	double K1, K0, K2, R1, R0, R2;
	int max_x=IMAGE_WIDTH/2,last_max_x=IMAGE_WIDTH/2;
	double K_line=0, R_line=0,last_K_line=0,last_R_line=0;
	
	double last_five_K[5] = { 0, 0, 0, 0, 0 };
	double last_five_R[5] = { 0, 0, 0, 0, 0 };
	
	int light_br=0,Led_Br=0;
	static int redest_count=0;

	
	//写VIDEO_LOG，调试使用
	if(VIDEO_LOG){
		if (fp = fopen("VideoLog.txt", "wb+"));
		else   printf("fail to open\n");
	}
	unsigned char *black_white_frame = (unsigned char *)malloc(sizeof(char)*(IMAGE_WIDTH*IMAGE_HEIGHT));//第三个320*240内存空间的开始
	//unsigned char *record_frame = (unsigned char *)malloc(sizeof(char)*(IMAGE_WIDTH*IMAGE_HEIGHT));//第三个320*240内存空间的开始

	get_v4l2=(unsigned char *)malloc(sizeof(char)*IMAGE_WIDTH*IMAGE_HEIGHT*3);
	get_v4l2=buffers[buf.index].start;//指向帧的开始
	
	
	while(1)
	{	
		// printf("V4L2_update thread runing\n");
	    fd_set fds;
		struct timeval tv;
		int r;
		FD_ZERO (&fds);//初始化fd_set结构体变量-
		FD_SET (fd, &fds);//zai fd_set的结构体上注册新文件

		tv.tv_sec = 2; //限时2s
		tv.tv_usec = 0;

		r = select (fd + 1, &fds, NULL, NULL, &tv);//判断是否可读，即摄像头是否准备好，tv是定时

		if (-1 == r) {
			if (EINTR == errno)
			continue;
			errno_exit ("select");
			
		}

		if (0 == r) {    //选择时间到返回0，
			//fprintf (stderr, "select timeout\n");
			while(1) {LOG_E("select timeout\n");
			            sleep(1);
						 }
			exit (EXIT_FAILURE);
		}
		
		struct v4l2_buffer buf;
		unsigned int i;
		CLEAR (buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {         //将摄像头采集到的数据放到buf中，原始数据
			switch (errno) {
			case EAGAIN:
				return 0;
			case EIO:    
			default:
				errno_exit ("VIDIOC_DQBUF");
			}
		}
		assert (buf.index < n_buffers);
		assert (buf.field ==V4L2_FIELD_NONE);
		//if(VIDEO_SAVE) AVI_write_frame(avifile,buffers[buf.index].start,buf.bytesused,framecount++);
        
		//写VIDEO_LOG，调试使用
		if(VIDEO_LOG) {
			gettimeofday(&tpstart,NULL);
			fprintf(fp, "*********This is %d frame.*********  \n", frame);
		}
		frame++;
		//处理原图，得黑白图像，红色点为非0值	
		int x_pixl,y_pixl;
		unsigned max_red = 0;
		//int light_num=0;
		for(y_pixl = 0; y_pixl < IMAGE_HEIGHT; y_pixl ++)//从第一行开始
		{
			unsigned char *source_data = (unsigned char *)(get_v4l2 + IMAGE_WIDTH * 3 * y_pixl);//从第一行第一个像素开始，三通道转换,每行有3*320个数据
			unsigned char *gray_data = (unsigned char *)(black_white_frame + IMAGE_WIDTH*y_pixl);//指向black_white_frame内存的开始
			//unsigned char *record_data = (unsigned char *)(record_frame + IMAGE_WIDTH*y_pixl);//指向black_white_frame内存的开始
			for(x_pixl = 0; x_pixl < IMAGE_WIDTH; x_pixl++){//从第一列开始
				unsigned char r = (unsigned)source_data[3*x_pixl+2];
				unsigned char g = (unsigned)source_data[3*x_pixl+1];
				unsigned char b = (unsigned)source_data[3*x_pixl];//提取R、G、B的值,由unsigned char转换为unsigned int
				gray_data[x_pixl]=0;
				//record_data[x_pixl]=0;
				if (x_pixl<4 || x_pixl>=(IMAGE_WIDTH - 5) || y_pixl<1 || y_pixl>(IMAGE_HEIGHT - 2))//将边框像素全部置0
					continue;
				/*if ( (r >= 220 || g >= 220 || b >= 220))//x_pixl >= 20 && x_pixl <= 300 && y_pixl >= 20 && y_pixl <= 220 &&
				{
					record_data[x_pixl] = 100;
					light_num++;
				}*/
				if(r <=g||r<=b) //如果某点像素r<=g或者r<=b则认为该点非红色
					continue;
				else if (g >= b)
				{
					if (60 * ((double)(g - b) / (r - b))>HSV_RED)//原来40.0，将RGB转换到HSV空间，根据转换公式，得到H值，认为H值小于25.0的像素点非红色
					{
						continue;
					}
				}
				else if (g < b)
				{
					if (((360.0 + 60 * ((double)(g - b) / (r - b)))>HSV_RED) && ((360.0 + 60 * ((double)(g - b) / (r - b)))<360-HSV_RED))
					{//将RGB转换到HSV空间，根据转换公式，得到H值，认为H值小于335.0的像素点非红色
						continue;
					}
				}
				//ptr2[x_pixl]=(r*38+g*75+b*15)>>7;
				//printf("red: %d green: %d blue: %d \n",sizeof(r),g,b);
				unsigned r_v = (unsigned)(2*r-g-b)/2;//r>g,r>b,r_v可能超过255?
				gray_data[x_pixl] = (unsigned) r_v;
				if (r_v>max_red)
					max_red = r_v;
				if (r_v < (max_red / 2)||r_v<MIN_RED)
					gray_data[x_pixl] = 0;
			} 
		}
		if(VIDEO_LOG) fprintf(fp, "max_red: %d	\n", max_red);
		//fprintf(fp, "max_red: %d\t y_pixl: %d\n", max_red, y_pixl);
		if((max_red>0)&&(max_red<75))
			redest_count++;
		else
			redest_count=0;
		if(redest_count>20)
		{  
	        redest_count=0;
			Led_Br=500;
			
	       light_br=400;
		}
		light_br--;
		if(light_br<0)
		{
			 if(mWalkFlag==1)
		   {
			Led_Br=0;
			light_br=0; 
		   }
		   else
			   light_br=400;
		}
		//if(Led_Br<200)
		//	max_red=0;
		//if(Line==1)
		Led_Br=500;//灯光设置常亮
			
		Agv_Peripheral_Set_LED_Bright(Led_Br);	

		//写VIDEO_LOG，调试使用
		if(VIDEO_LOG) {
			gettimeofday(&tpend, NULL);
			timeuse = 1000 * 1000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;
			fprintf(fp, "turn to gray  :%f \n", timeuse);
		}
		double three_left_middle[240];//左边缘的中心点的x坐标
		double three_center_middle[240];//中间边缘的中心点的x坐标
		double three_right_middle[240];//右边缘的中心点的x坐标
		//获取边缘点
		int num_of_road[4] = { 0, 0, 0, 0 },road_num[IMAGE_HEIGHT];
		for ( y_pixl = 0; y_pixl < IMAGE_HEIGHT - 1; y_pixl++)
		{
			int num_of_margin = 0;//记录每一行的边缘点个数
			unsigned char *data = (unsigned char *)(black_white_frame + IMAGE_WIDTH*y_pixl);//从第一行第一个像素开始，三通道转换,每行有3*320个数据
			for ( x_pixl = 4; x_pixl < IMAGE_WIDTH - 5; x_pixl++)
			{//从第一列开始
				if (data[x_pixl - 4] == 0 && data[x_pixl - 3] == 0 && data[x_pixl - 2] == 0 && data[x_pixl - 1] == 0 &&
					data[x_pixl] != 0 && data[x_pixl + 1] != 0 && data[x_pixl + 2] != 0 && data[x_pixl + 3] != 0 &&
					data[x_pixl + 4] != 0&& (x_pixl + 4) <= (IMAGE_WIDTH - 1))//左1边缘应保证左边四个像素为0，自己和右边四个像素不为0
				{
					six_margin[num_of_margin] = x_pixl;//0 2 4，记录该边缘点的坐标
					num_of_margin++;//1 3 5，边缘点个数加1
					x_pixl+=5;//寻找到边缘点后，跳5格继续检测边缘
					if (num_of_margin > 6||num_of_margin%2!=1)//如果边缘点的个数大于6个或者寻找到左边缘后边缘点的个数为偶数，则认为该行边缘无效，舍弃该行
					{
						num_of_margin = -1;
						break;
					}
					continue;
				}
				if (data[x_pixl - 4] != 0 && data[x_pixl - 3] != 0 && data[x_pixl - 2] != 0 && data[x_pixl - 1] != 0 &&
					data[x_pixl] != 0 && data[x_pixl + 1] == 0 && data[x_pixl + 2] == 0 && data[x_pixl + 3] == 0 &&
					data[x_pixl + 4] == 0 && (x_pixl + 4) <= (IMAGE_WIDTH - 1))//右1边缘应保证左边四个像素不为0，自己和右边四个像素为0
				{
					six_margin[num_of_margin] = x_pixl;
					num_of_margin++;
					x_pixl+=5;
					if (num_of_margin > 6||num_of_margin%2!=0)//如果边缘点的个数大于6个或者寻找到右边缘后边缘点的个数为奇数，则认为该行边缘无效，舍弃该行
					{
						num_of_margin = -1;
						break;
					}
				}
			}
			switch (num_of_margin)
			{
			case 2://该行有两个边缘点，则认为有1条路，分给中间
				num_of_road[0]++;
				num_of_road[1]++;
				road_num[y_pixl] = 0;
				if ((six_margin[1] - six_margin[0])>=10 && (six_margin[1] - six_margin[0])<100)//如果两个边缘的距离合适，则记录其中心点
					center_middle[y_pixl] = (six_margin[0] + six_margin[1]) / 2;
				else center_middle[y_pixl] =-1;//否则，舍弃得到的边缘
				left_middle[y_pixl] = -1;
				right_middle[y_pixl] = -1;
				three_left_middle[y_pixl] = -1;
				three_right_middle[y_pixl] = -1;
				three_center_middle[y_pixl] = -1;
				break;
			case 4://该行有四个边缘点，则认为有两条路，分给左右
				num_of_road[0]++;
				num_of_road[2]++;
				road_num[y_pixl] = 2;
				if ((six_margin[1] - six_margin[0])>=10 && (six_margin[1] - six_margin[0])<100)//如果两个边缘的距离合适，则记录其中心点
					left_middle[y_pixl] = (six_margin[0] + six_margin[1]) / 2;
				else left_middle[y_pixl] = -1;
				if ((six_margin[3] - six_margin[2])>10 && (six_margin[3] - six_margin[2])<100)//如果两个边缘的距离合适，则记录其中心点
					right_middle[y_pixl] = (six_margin[2] + six_margin[3]) / 2;
				else right_middle[y_pixl] = -1;
				center_middle[y_pixl] = -1;
				three_left_middle[y_pixl] = -1;
				three_right_middle[y_pixl] = -1;
				three_center_middle[y_pixl] = -1;
				break;
			case 6://该行有6个边缘点，则认为有3条路，分给左中右
				num_of_road[0]++;
				num_of_road[3]++;
				road_num[y_pixl] = 3;
				if ((six_margin[3] - six_margin[2])>=10 && (six_margin[3] - six_margin[2])<100)
					center_middle[y_pixl] = (six_margin[2] + six_margin[3]) / 2;
				else	center_middle[y_pixl] = -1;
				if ((six_margin[1] - six_margin[0])>10 && (six_margin[1] - six_margin[0])<100)
					left_middle[y_pixl] = (six_margin[0] + six_margin[1]) / 2;
				else left_middle[y_pixl] = -1;
				if ((six_margin[5] - six_margin[4])>10 && (six_margin[5] - six_margin[4])<100)
					right_middle[y_pixl] = (six_margin[4] + six_margin[5]) / 2;
				else right_middle[y_pixl] = -1;
				three_left_middle[y_pixl] = (six_margin[0] + six_margin[1]) / 2;
				three_right_middle[y_pixl] = (six_margin[4] + six_margin[5]) / 2;
				three_center_middle[y_pixl] = (six_margin[2] + six_margin[3]) / 2;
				break;
			default://其他情况，全部置-1
				road_num[y_pixl] = 0;
				center_middle[y_pixl] = -1;
				left_middle[y_pixl] = -1;
				right_middle[y_pixl] = -1;
				three_left_middle[y_pixl] = -1;
				three_right_middle[y_pixl] = -1;
				three_center_middle[y_pixl] = -1;
				break;
			}
		}
		if(VIDEO_LOG) fprintf(fp, "road: %d\t One Road:%d\t Two Road:%d\t Three Road:%d\t  \n", num_of_road[0], num_of_road[1], num_of_road[2], num_of_road[3]);
		
		
		
		if(VIDEO_LOG) {
			gettimeofday(&tpend, NULL);
			timeuse = 1000 * 1000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;
			//printf("black_white_frame  timeuse 3 :%f \n", timeuse);
			fprintf(fp, "get margin and middle :%f \n", timeuse);
		}
		
		//找出起始点和终点，对中心点进行处理
		int startX0 = 0, startY0 = 0, endX0 = 0, endY0 = 0,
			startX1 = 0, startY1 = 0, endX1 = 0, endY1 = 0,
			startX2 = 0, startY2 = 0, endX2 = 0, endY2 = 0;
		for ( y_pixl = 0; y_pixl < IMAGE_HEIGHT; y_pixl++)
		{
			if (left_middle[y_pixl] != -1.0)
			{
				if (startY0 == 0) {
					startY0 = y_pixl;
					startX0 = (int)left_middle[y_pixl];
				}
				else
				{
					endY0 = y_pixl;
					endX0 = (int)left_middle[y_pixl];
				}
			}
			if (center_middle[y_pixl] != -1.0)
			{
				if (startY1 == 0)
				{
					startX1 = (int)center_middle[y_pixl];
					startY1 = y_pixl;
				}
				else {
					endX1 = (int)center_middle[y_pixl];
					endY1 = y_pixl;
				}
			}
			if (right_middle[y_pixl] != -1.0)
			{
				if (startY2 == 0) {
					startX2 = (int)right_middle[y_pixl];
					startY2 = y_pixl;
				}
				else {
					endY2 = y_pixl;
					endX2 = (int)right_middle[y_pixl];
				}
			}
		}
		//对中心点进行检查审视，或许增加并重分配
		///if(light_num>=900){
			int  breakY0 = 0, breakX0 = 0, breakX1 = 0, breakY1 = 0;
			for ( y_pixl = startY0 + 1; y_pixl <= endY0 - 1; y_pixl++)
			{
			if (left_middle[y_pixl] == -1.0) //左中心点
			{
				if (breakY0 == 0) {
					breakY0 = y_pixl;//起始缺断行
					breakX0 = (int)left_middle[y_pixl - 1];
				}
				else{
					breakY1 = y_pixl;//末尾缺断行
					breakX1 = (int)left_middle[y_pixl + 1];
					if (left_middle[y_pixl + 1] != -1.0 )//暂时只考虑是中间断裂		|| y_pixl>=endY0-2)
					{
						int i;
						for ( i = breakY0; i < breakY1; i++){
							if (breakX0 == -1 || breakX1 == -1)
								break;
							if (center_middle[i] != -1&&left_middle[i]==-1&&right_middle[i] ==-1)//重新分配给左右middle
							{
								//unsigned char *record_data = (unsigned char *)(record_frame + IMAGE_WIDTH*y_pixl);//指向black_white_frame内存的开始
								int x = breakX0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(breakX1 - breakX0));
								int x0 = (int)right_middle[breakY0 - 1]; int x1 = (int)right_middle[breakY1 + 1];
								int x2 = x0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(x1 - x0));//右填充直线
								if (abs(center_middle[i] - x2) <= 6){//左中心在反射区
								//if (record_data[x] ==100|| abs(center_middle[i] - x2) <= 8){//左中心在反射区
									//left_middle[i] = breakX0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(breakX1 - breakX0));//左填充直线
									right_middle[i] = center_middle[i];//中分配给右
									center_middle[i] = -1;
									road_num[i] = 2;
								}
								else {//左中心不在反射区
									if (x0 == -1 || x1 == -1)
										break;
									//if (record_data[x] == 100|| abs(center_middle[i] - x) <= 8){//右中心在反射区
									if ( abs(center_middle[i] - x) <= 6){//右中心在反射区
										//right_middle[i] = breakX0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(breakX1 - breakX0));//右填充直线
										left_middle[i] = center_middle[i];//中分配给左
										center_middle[i] = -1;
										road_num[i] = 2;
									}
								}
							}
						}
						breakY0 =0, breakX0 =0, breakX1 = 0, breakY1 = 0; 
					}
				}
			}
		}
			//对右边缘再进行一次
			breakY0 = 0, breakX0 = 0, breakX1 = 0, breakY1 = 0;
			for (y_pixl = startY2 + 1; y_pixl <= endY2 - 1; y_pixl++)
			{
				if (right_middle[y_pixl] == -1.0) //左中心点
				{
					if (breakY0 == 0) {
						breakY0 = y_pixl;//起始缺断行
						breakX0 = (int)right_middle[y_pixl - 1];
					}
					else{
						breakY1 = y_pixl;//末尾缺断行
						breakX1 = (int)right_middle[y_pixl + 1];
						if (right_middle[y_pixl + 1] != -1.0)//暂时只考虑是中间断裂		|| y_pixl>=endY0-2)
						{
							int i;
							for ( i = breakY0; i < breakY1; i++){
								if (breakX0 == -1 || breakX1 == -1)
									break;
								if (center_middle[i] != -1 && left_middle[i] == -1 && right_middle[i] == -1)//重新分配给左右middle
								{
									//unsigned char *record_data = (unsigned char *)(record_frame + IMAGE_WIDTH*i);
									int x = breakX0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(breakX1 - breakX0));
									int x0 = (int)left_middle[breakY0 - 1]; int x1 = (int)left_middle[breakY1 + 1];
									int x2 = x0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(x1 - x0));//右填充直线
									//if (record_data[x] == 100 || abs(center_middle[i] - x2) <= 8){//右中心在反射区,或中接近左
									if (abs(center_middle[i] - x2) <= 6){//右中心在反射区,或中接近左
										//right_middle[i] = x;//右填充直线
										left_middle[i] = center_middle[i];//中分配给左
										center_middle[i] = -1;
										road_num[i] = 2;
									}
									else {
										if (x0 == -1 || x1 == -1)
											break;
										//if (record_data[x2] == 100 || abs(center_middle[i] - x) <= 8){//左中心在反射区,或中接近右
										if ( abs(center_middle[i] - x) <= 6){//左中心在反射区,或中接近右
											//left_middle[i] = x2;//左填充直线
											right_middle[i] = center_middle[i];//中分配给右
											center_middle[i] = -1;
											road_num[i] = 2;
										}
									}
								}
							}
							breakY0 = 0, breakX0 = 0, breakX1 = 0, breakY1 = 0;
						}
					}
				}
			}
			//对中间边缘再进行一次
			breakY0 = 0, breakX0 = 0, breakX1 = 0, breakY1 = 0;
			for ( y_pixl = startY1 + 1; y_pixl <= endY1 - 1; y_pixl++)
			{
				if (center_middle[y_pixl] == -1.0) //左中心点
				{
					if (breakY0 == 0) {
						breakY0 = y_pixl;//起始缺断行
						breakX0 = (int)center_middle[y_pixl - 1];
					}
					else{
						breakY1 = y_pixl;//末尾缺断行
						breakX1 = (int)center_middle[y_pixl + 1];
						if (center_middle[y_pixl + 1] != -1.0)//暂时只考虑是中间断裂		|| y_pixl>=endY0-2)
						{
							int i;
							for ( i = breakY0; i < breakY1; i++){
								if (center_middle[i] == -1 && left_middle[i] != -1 && right_middle[i] != -1)//重新分配给左右middle
								{
									int x = breakX0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(breakX1 - breakX0));
									int x0 = (int)left_middle[breakY0 - 1]; int x1 = (int)left_middle[breakY1 + 1];
									int x2 = x0 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(x1 - x0));//左填充直线
									
									int x3 = (int)right_middle[breakY0 - 1]; int x4 = (int)right_middle[breakY1 + 1];
									int x5 = x3 + (double)(i - breakY0) / (double)(breakY1 - breakY0)*((double)(x4 - x3));//右填充直线
									if (abs(left_middle[i] - x2) <= 5 && abs(right_middle[i] - x) <= 8){//三条线时，左很左，右占中了
										if (breakX0 == -1 || breakX1 == -1 || x3 == -1 || x4 == -1)
											break;
										center_middle[i] = right_middle[i];//右将边缘还给中
										//right_middle[i] = x5;//右填充直线
										road_num[i] = 3;
									}
									else {
										if (abs(left_middle[i] - x) <= 5 && abs(right_middle[i] - x5) <= 8){//三条线时，右很右，左占中了
											if (breakX0 == -1 || breakX1 == -1 || x0 == -1 || x1 == -1)
												break;
											center_middle[i] = left_middle[i];//左将边缘还给中
											//left_middle[i] = x2;//左填充直线
											road_num[i] = 3;
										}
									}
								}
							
							}
							breakY0 = 0, breakX0 = 0, breakX1 = 0, breakY1 = 0;
						}
					}
				}
			}
		///}
		//显示处理后图像，调试使用
		short *dest_b = (short *)(fbp)+vinfo.yoffset * 1024 + vinfo.xoffset+680;
		for (y_pixl = 0; y_pixl < IMAGE_HEIGHT; y_pixl++)
		{
			unsigned char *data = (unsigned char *)(black_white_frame + IMAGE_WIDTH*y_pixl);
			for (x_pixl = 0; x_pixl < IMAGE_WIDTH; x_pixl++){
				if (x_pixl == IMAGE_WIDTH / 2||y_pixl==(IMAGE_HEIGHT/2-30))			dest_b[x_pixl] = 20000;
				else	dest_b[x_pixl] = (data[x_pixl] >> 3) | ((data[x_pixl] << 3) & 0x7e0) | ((data[x_pixl] << 8) & 0xf800);	
				if(left_middle[y_pixl] != -1)  		dest_b[(int)left_middle[y_pixl]]=(255 >> 3) | ((0 << 3) & 0x7e0) | ((0 << 8) & 0xf800);
				if(center_middle[y_pixl] != -1)  	dest_b[(int)center_middle[y_pixl]]=(0 >> 3) | ((255 << 3) & 0x7e0) | ((0 << 8) & 0xf800);
				if(right_middle[y_pixl] != -1)  	dest_b[(int)right_middle[y_pixl]]=(0 >> 3) | ((0 << 3) & 0x7e0) | ((255 << 8) & 0xf800);
				//#define RGB(r,g,b)  (((r << 8) & 0xF800) |((g << 3) & 0x7E0)  |  ((b >> 3)))
			}
			dest_b += 1024;
		}
		double pure_left_middle[240];//左边缘的中心点的x坐标
		//double pure_center_middle[240];//中间边缘的中心点的x坐标
		double pure_right_middle[240];//右边缘的中心点的x坐标
		//int road_count[240], road_type[4] = { 0, 0, 0, 0 };
		int one_to_two[2] = { 0, 0 }, down_points = 0;
		int two_to_one[2] = { 0, 0 }, up_points = 0, isX = 0;
		for ( y_pixl = IMAGE_HEIGHT - 4; y_pixl >4; y_pixl--){
			if (road_num[y_pixl + 1] == 1 && road_num[y_pixl + 2] == 1 && road_num[y_pixl + 3] == 1 && road_num[y_pixl] == 1
				&& road_num[y_pixl - 1] == 2 && road_num[y_pixl - 2] == 2 && road_num[y_pixl - 3] == 2 && road_num[y_pixl - 4] == 2){
				if (one_to_two[0] == 0)
					one_to_two[0] = y_pixl;
				else one_to_two[1] = y_pixl;
				y_pixl -= 4;
				continue;
			}
			if (road_num[y_pixl + 1] == 2 && road_num[y_pixl + 2] == 2 && road_num[y_pixl + 3] == 2 && road_num[y_pixl] == 2
				&& road_num[y_pixl - 1] == 1 && road_num[y_pixl - 2] == 1 && road_num[y_pixl - 3] == 1 && road_num[y_pixl - 4] == 1){
				if (two_to_one[0] == 0)
					two_to_one[0] = y_pixl;
				else two_to_one[1] = y_pixl;
				y_pixl -= 4;
			}
		}
		if ((one_to_two[0] >two_to_one[0] && two_to_one[0] >one_to_two[1] && one_to_two[1]>0) ||
			(two_to_one[0]>one_to_two[0] && one_to_two[0] >two_to_one[1] && two_to_one[1]>0)) {
			int row;
			for ( row = IMAGE_HEIGHT - 1; row > 0; row--){
				if (row >= two_to_one[0])
				{
					pure_left_middle[row] = left_middle[row];
					pure_right_middle[row] = right_middle[row];
					if (left_middle[row] != -1 && right_middle[row] != -1)
						down_points++;
					left_middle[row] = -1;
					right_middle[row] = -1;
				}
				else{
					pure_left_middle[row] = -1;
					pure_right_middle[row] = -1;
					if (left_middle[row] != -1 && right_middle[row] != -1)
						up_points++;
				}
			}
			//middle frame, 展示得到的中心点
			
			//out << "down_points ::" << down_points << "	up_points:	" << up_points << endl;
			if (down_points > up_points)
			{
				double *pure_K_R = leastSquare(pure_left_middle);
				K2 = pure_K_R[0];
				R2 = pure_K_R[1];
				//markLine(K2, R2, check_frame, Scalar(0, 0, 255));
				pure_K_R = leastSquare(pure_right_middle);
				K0 = pure_K_R[0];
				R0 = pure_K_R[1];
				//markLine(K0, R0, check_frame, Scalar(255, 0, 0));
				isX = 1;
			}
			else{
				double *pure_K_R = leastSquare(left_middle);
				K0 = pure_K_R[0];
				R0 = pure_K_R[1];
				//markLine(K0, R0, check_frame, Scalar(255, 0, 0));
				pure_K_R = leastSquare(right_middle);
				K2 = pure_K_R[0];
				R2 = pure_K_R[1];
				//markLine(K2, R2, check_frame, Scalar(0, 0, 255));
				isX = 1;
			}
		}
		if (num_of_road[0] <= 20)//点太少或者光太暗
		{
			K0 = 0; K1 = 0; K2 = 0;
			R0 = 0; R1 = 0; R2 = 0;
		}
		else{
			K_R = leastSquare(center_middle);
			K1 = K_R[0];
			R1 = K_R[1];
			int x1 = (int)(K_R[0] * startY1 + K_R[1]);
			int x2 = (int)(K_R[0] * endY1 + K_R[1]);
			
			if(isX==0){
				K_R = leastSquare(left_middle);//紫线
				K0 = K_R[0];
				R0 = K_R[1];
				x1 = (int)(K_R[0] * startY0 + K_R[1]);
				x2 = (int)(K_R[0] * endY0 + K_R[1]);
				
				K_R = leastSquare(right_middle);//黄线
				K2 = K_R[0];
				R2 = K_R[1];
				x1 = (int)(K_R[0] * startY2 + K_R[1]);
				x2 = (int)(K_R[0] * endY2 + K_R[1]);
			}
			if (num_of_road[3] >= 15){
				double *K_R = leastSquare(three_left_middle);//绿线
				K0 = K_R[0];
				R0 = K_R[1];
				
				K_R = leastSquare(three_center_middle);//绿线
				K1 = K_R[0];
				R1 = K_R[1];
				
				K_R = leastSquare(three_right_middle);//绿线
				K2 = K_R[0];
				R2 = K_R[1];
			}
		}
		
		if(VIDEO_LOG) fprintf(fp,"leastSquare  :%f %f %d\n",K0, R0,(int)(K0*120.0 + R0));
		if(VIDEO_LOG) fprintf(fp,"leastSquare  :%f %f %d\n",K1, R1,(int)(K1*120.0 + R1));
		if(VIDEO_LOG) fprintf(fp,"leastSquare  :%f %f %d\n",K2, R2,(int)(K2*120.0 + R2));
		
		int max_x_point[3]={0,0,0};//三条直线间的控制点
		int node01[2] = { -1,-1 }, node02[2] = { -1,-1 }, node12[2] = { -1 ,-1};//三条直线间的交点
		double theta01, theta12, theta02;//三条直线间的夹角衡量
		double distance01, distance12, distance02;//三条直线间的距离衡量，max_x间的距离
		max_x_point[0] = (int)(K0 * IMAGE_HEIGHT/2 + R0);
		max_x_point[1] = (int)(K1 * IMAGE_HEIGHT/2 + R1);
		max_x_point[2] = (int)(K2 * IMAGE_HEIGHT/2 + R2);
		if(K1*K0!=-1) theta01 = (K1 - K0) / (1 + K1*K0); else theta01=100;
		distance01 = max_x_point[0] - max_x_point[1]; 
		if(K2 * K0!=-1) theta02 = (K2 - K0) / (1 + K2 * K0);else theta02=100;
		distance02 = max_x_point[0] - max_x_point[2];
		if(K1 * K2!=-1) theta12 = (K1 - K2) / (1 + K1 * K2); else theta12=100;
		distance12 = max_x_point[1] - max_x_point[2];
		if (K0 != K1){
			node01[0] = (R0 - R1) / (K1 - K0);
			node01[1] = K0*node01[0] + R0;
		}
		if (K0 != K2){
			node02[0] = (R0 - R2) / (K2 - K0);
			node02[1] = K0*node02[0] + R0;
			
		}
		if (K1 != K2){
			node12[0] = (R1 - R2) / (K2 - K1);
			node12[1] = K1*node12[0] + R1;
		}
		
		if(VIDEO_LOG) fprintf(fp, "The last:	node02[0]: %d  node02[1]: %d\n", node02[0],node02[1]);
		if(node02[0]>=120&&node02[0]<=200&&node02[1]<300&&node02[1]>20)
				v_scbroblast[2] = 1;
		else v_scbroblast[2] = 0;
		if(VIDEO_LOG) fprintf(fp, "The last:	v_scbroblast[2]: %f\n", v_scbroblast[2]);
		
		int caseNum=0;
		//mWalkFlag = 0;
		switch (mWalkFlag)
		{
		case 0:
			if ((K0 == 0 && R0 == 0) || (K2 == 0 && R2 == 0))
			{
				K_line = K1;
				R_line = R1;
				caseNum = 0;
				break;
			}
			// if( ((abs(K0)>1.0&&abs(max_x_point[0]-last_max_x)>10) )||abs(K0)>1.5){
				// K0=last_K_line;
				// R0=last_R_line;
			// }
			// if((abs(K2)>1.0&&abs(max_x_point[2]-last_max_x)>10)||abs(K2)>1.5){
				// K2=last_K_line;
				// R2=last_R_line;
			// }
			/*
			for (int i = 0; i < IMAGE_HEIGHT; i++)
			{
				int p1 = (int)(int)(K2*i + R2);
				if (abs(middle[i][0] - p1) < 5)
					middle[i][0] = -1;
				if (abs(middle[i][1] - p1) < 5)
					middle[i][1] = -1;
				if (abs(middle[i][2] - p1) < 5)
					middle[i][2] = -1;
				if ((middle[i][0]>0 && middle[i][1]>0) || (middle[i][0]>0 && middle[i][2]>0) || (middle[i][2] > 0 && middle[i][1] > 0) || (middle[i][1]>0 && middle[i][0]<0&&middle[i][2]<0))
				{
					middle[i][0] = -1; middle[i][1] = -1; middle[i][2] = -1;
				}
				else num_of_middle++;
			}
			K_R = leastSquare(middle);
			if (num_of_middle <= 20){
				K3 = 0;
				R3 = 0;
			}
			else{
				K3 = K_R[0];
				R3 = K_R[1];
			}
			 x1 = (int)(K_R[0] * 60 + K_R[1]);
			 x2 = (int)(K_R[0] * 180 + K_R[1]);
			cv::line(marking_frame, cvPoint(x1, 60), cvPoint(x2, 180), Scalar(255, 255, 255));
			
			for (int y_pixl = 0; y_pixl < IMAGE_HEIGHT; y_pixl++)
			{
				markFrame(middle[y_pixl][0], y_pixl, marking_frame, 3, IMAGE_WIDTH, IMAGE_HEIGHT);// 
				markFrame(middle[y_pixl][1], y_pixl, marking_frame, 6, IMAGE_WIDTH, IMAGE_HEIGHT);// 
				markFrame(middle[y_pixl][2], y_pixl, marking_frame, 2, IMAGE_WIDTH, IMAGE_HEIGHT);
			}
			*/
			//abs(startY0 - node02[0])<abs(endY0 - node02[0])
			if (isX == 1){
				K_line = K0;
				R_line = R0;
				break;
			}
			if (node02[0]<(IMAGE_HEIGHT/2-30)&& node02[0]>-30&&node02[0]!=0)
			{
				double theta0 = (last_K_line - K0) / (1 + last_K_line*K0);//与上次的夹角
				double distance0 = max_x_point[0] - last_max_x;//与上次的距离
				double theta2 = (K2 - last_K_line) / (1 + K2 * last_K_line);
				double distance2 = max_x_point[2] - last_max_x;
				//out << "This  theta0:" << theta0 << "		distance0:" << distance0 << "	theta2:" << theta2 << "		distance2:" << distance2 << endl;
				if (abs(theta0)<0.1 || ( (abs(theta01)<0.08&&abs(distance01) < 5) &&   ( abs(theta0) < abs(theta2) ) ) &&!(max_x_point[0]<node02[1]&&node02[1]<max_x_point[2]) )//K0与上次接近或者 01十分接近且K2比K0与上次更接近
				{//选择与上次较接近的直线
					K_line = K1;
					R_line = R1;
					caseNum = 1;
				}
				else if (abs(theta2)<0.1 || (abs(theta0) > abs(theta2) &&   (    (abs(theta12)<0.08&&abs(distance12) < 5)   ) ) )//K2与上次接近 或者 12十分接近且K2比K0与上次更接近
				{
					K_line = K2;
					R_line = R2;
					caseNum = 2;
				}
				else {
					K_line = K0;
					R_line = R0;
					caseNum = 3;
				}

			}
			else if (node02[0]<IMAGE_HEIGHT && node02[0]>(IMAGE_HEIGHT/2-30) && node02[1] > 0 && node02[1] < IMAGE_WIDTH)
			{
				
				for ( y_pixl = node02[0]; y_pixl < IMAGE_HEIGHT; y_pixl++)
				{
						center_middle[y_pixl] = -1;
						left_middle[y_pixl] = -1;
						right_middle[y_pixl] = -1;
				}
				K_R = leastSquare(left_middle);
				if (K_R[0] == 0 && K_R[1] == 0)
					K_R = leastSquare(center_middle);
				if (K_R[0] == 0 && K_R[1] == 0){
					K_line = K1;
					R_line = R1;
				}
				else
				{
					K_line = K_R[0];
					R_line = K_R[1];
				}
					
				
				/*if (K_R[0] == 0 && K_R[1] == 0&&K2!=0&&R2!=0&&num_of_road[2]>=10)
				{
					K_line=hough_K_R[1];
					R_line=hough_K_R[0];
					
				}
				else if(K_R[0] == 0 && K_R[1] == 0)
				{
					K_R = leastSquare(center_middle);
					K_line = K_R[0];
					R_line = K_R[1];
				}
				else{
					K_line = K_R[0];
					R_line = K_R[1];
				}*/
				caseNum = 4;
			}
			else{
				K_line = K0;
				R_line = R0;
				caseNum = 5;
				/*K_R = leastSquare(left_middle);
				if (K_R[0] == 0 && K_R[1] == 0)
				K_R = leastSquare(center_middle);
				K_line = K_R[0];
				R_line = K_R[1];*/
			}
			break;
		case 1:
			K_line = K1;
			R_line = R1;
			caseNum = 10;
			if (K1 == 0 && R1 == 0)
			{
				if (num_of_road[2] >= 16 && num_of_road[3] <= 16)
				{
					int x1 = (int)(K0 * IMAGE_HEIGHT/2 + R0);
					double distance1 = ((K0 * IMAGE_HEIGHT/2 - IMAGE_WIDTH/2 + R0)*(K0 * IMAGE_HEIGHT/2 - IMAGE_WIDTH/2 + R0)) / (K0* K0 + 1);
					int x2 = (int)(K2 * IMAGE_HEIGHT/2 + R2);
					double distance2 = ((K2 * IMAGE_HEIGHT/2 - IMAGE_WIDTH/2 + R2)*(K2 * IMAGE_HEIGHT/2 - IMAGE_WIDTH/2 + R2)) / (K2 * K2 + 1);
					if (abs(distance1) > abs(distance2)){
						K_line = K2;
						R_line = R2;
						caseNum = 11;
						if (K_line == 0 && R_line == 0){
							K_line = 0;
							R_line = IMAGE_WIDTH/2;
							caseNum = 12;
						}
					}
					else {
						K_line = K0;
						R_line = R0;
						caseNum = 13;
						if (K_line == 0 && R_line == 0){
							K_line = 0;
							R_line = IMAGE_WIDTH/2;
							caseNum = 14;
						}
					}
					
				}
			}
			break;
		case 2:
			if ((K0 == 0 && R0 == 0) || (K2 == 0 && R2 == 0))
			{
				K_line = K1;
				R_line = R1;
				caseNum = 20;
				break;
			}
			//if (node02[0]<120 && node02[0]>0 && node02[1]>max_x_point[0] && node02[1] < max_x_point[2])//02间交点在前方中间，代表两线聚合
			// if( ((abs(K0)>1.0&&abs(max_x_point[0]-last_max_x)>10) )||abs(K0)>1.5){
				// K0=last_K_line;
				// R0=last_R_line;
			// }
			// if((abs(K2)>1.0&&abs(max_x_point[2]-last_max_x)>10)||abs(K2)>1.5){
				// K2=last_K_line;
				// R2=last_R_line;
			// }
			if (isX == 1){
				K_line = K2;
				R_line = R2;
				break;
			}
			if (node02[0]<(IMAGE_HEIGHT/2-30)&& node02[0]>-30&&node02[0]!=0)//02间交点在前方中间，代表两线聚合
			{
				double theta0 = (last_K_line - K0) / (1 + last_K_line*K0);
				double distance0 = max_x_point[0] - last_max_x;
				double theta2 = (K2 - last_K_line) / (1 + K2 * last_K_line);
				double distance2 = max_x_point[2] - last_max_x;
				if (abs(theta2)<0.1 || ( (abs(theta01)<0.08&&abs(distance01) < 5) &&   ( abs(theta0) > abs(theta2) ) ) 
					&&!(max_x_point[0]<node02[1]&&node02[1]<max_x_point[2]) )//K2与上次接近或者 01十分接近且K2比K0与上次更接近
				{//选择与上次较接近的直线
					K_line = K1;
					R_line = R1;
					caseNum = 21;
				}
				else if (abs(theta0)<0.1 || (abs(theta0) < abs(theta2) &&   (    (abs(theta12)<0.08&&abs(distance12) < 5)   ) ) )//K0与上次接近 或者 12十分接近且K0比K2与上次更接近
				{
					K_line = K0;
					R_line = R0;
					caseNum = 22;
				}
				/*if ((abs(theta2)<0.05&&distance2<8) || ((abs(theta01)<0.08&&abs(distance01) < 5) || (abs(theta12)<0.08&&abs(distance12) < 5)))
				{//选择与上次较接近的直线
					if(abs(theta12)<0.08&&distance12<5)
					{
						K_line = K0;
						R_line = R0;

					}
					else{
						K_line = K1;
						R_line = R1;
					}
					
					node02[0]
				}
				else if ((abs(theta0)<0.05&&distance0<8) || (abs(theta0) < abs(theta2) && ((abs(theta12)<0.08&&abs(distance12) < 5)))	)
				{
					K_line = K0;
					R_line = R0;
					caseNum = 22;
				}*/
				else
				{
					K_line = K2;
					R_line = R2;
					caseNum = 23;
				}
			}
			else if (node02[0]<IMAGE_HEIGHT && node02[0]>(IMAGE_HEIGHT/2-30) && node02[1] > 0 && node02[1] < IMAGE_WIDTH)
			{
							
					for ( y_pixl = node02[0]; y_pixl < IMAGE_HEIGHT; y_pixl++)
					{
						center_middle[y_pixl] = -1;
						left_middle[y_pixl] = -1;
						right_middle[y_pixl] = -1;
					}
					K_R = leastSquare(right_middle);
					if (K_R[0] == 0 && K_R[1] == 0)
						K_R = leastSquare(center_middle);
					if (K_R[0] == 0 && K_R[1] == 0){
						K_line = K1;
						R_line = R1;
					}
					else
					{
						K_line = K_R[0];
						R_line = K_R[1];
					}
					
				
				/*if (K_R[0] == 0 && K_R[1] == 0&&K0!=0&&R0!=0&&num_of_road[2]>=10)
				{
					K_line=hough_K_R[5];
					R_line=hough_K_R[4];
					
				}
				else if(K_R[0] == 0 && K_R[1] == 0)
				{
					K_R = leastSquare(center_middle);
					K_line = K_R[0];
					R_line = K_R[1];
				}
				else{
					K_line = K_R[0];
					R_line = K_R[1];
				}*/
				caseNum = 24;
			}
			else{
				K_line = K2;
				R_line = R2;
				caseNum = 25;
				/*K_R = leastSquare(right_middle);
				if (K_R[0] == 0 && K_R[1] == 0)
				K_R = leastSquare(center_middle);
				K_line = K_R[0];
				R_line = K_R[1];*/
			}
			break;
		}
		if(VIDEO_LOG) fprintf(fp, "This time :	K_line: %f\t R_line:%f\t max_x:%d\t caseNum:%d  \n", K_line, R_line, (int)(K_line*120.0+R_line),caseNum);
		markFrame(node02[1], node02[0], 2015);
		
		node02[1]=0;node02[0]=0;
		switch(caseNum%10){
			case 0:markFrame(10,10,2015);//左上
			break;
			case 1:markFrame(310,10,2015);//右上
			break;
			case 2:markFrame(10,120,2015);//左中
			break;
			case 3:markFrame(310,120,2015);//右中
			break;
			case 4:markFrame(10,210,2015);//左下
			break;
			case 5:markFrame(310,210,2015);//右下
			break;
			default:break;
		}
		//int i;//更新K、R
		for (i = 4; i >0; i--){
			last_five_K[i] = last_five_K[i - 1];
			last_five_R[i] = last_five_R[i - 1];
		}
		last_five_K[0] = K_line;
		last_five_R[0] = R_line;
		//对K中值滤波，并找到对应的R
		K_line = midFilter(last_five_K);
		for (i = 0; i < 5; i++)
		{
			if (K_line == last_five_K[i])
				R_line = last_five_R[i];
		}

		if (K_line == 0 && R_line == 0){
			K_line=last_K_line;
			R_line=last_R_line;
			if(frame<=5) max_x=IMAGE_WIDTH/2;
			else	max_x=-1;						//by ZIHAO:修改为 max_x=-1;原为：max_x=last_max_x
			for (y_pixl = 60; y_pixl < 120; y_pixl++){
				int x1 = (int)(K_line*y_pixl + R_line);
				markOnePoint(x1, y_pixl, 34);
			}
			markFrame(10, 120, 2016);
		}
		else{
			max_x = (int)(K_line*(IMAGE_HEIGHT/2-30) + R_line);
			for (y_pixl = 60; y_pixl < 180; y_pixl++){
				int x1 = (int)(K_line*y_pixl + R_line);
				markOnePoint(x1, y_pixl, 63488);
			}
			markFrame(max_x, IMAGE_HEIGHT/2, 2016);
		}

		max_x = max_x >IMAGE_WIDTH ? IMAGE_WIDTH : max_x;
		max_x = max_x <0? 0 : max_x;
		if(VIDEO_LOG) {
			fprintf(fp, "The last:	K_line: %f\t R_line:%f\t max_x:%d\t   \n", K_line, R_line, max_x);
			printf("The last:	K_line: %f\t R_line:%f\t max_x:%d\t   \n", K_line, R_line, max_x);
		}
		v_scbroblast[0] = K_line;					//by ZIHAO:原为：v_scbroblast[0] = (K_line+last_K_line)/2.0
		v_scbroblast[1] = max_x<=0?-1:max_x;		//by ZIHAO:原为：v_scbroblast[1] = max_x<=0?-1:((max_x+last_max_x)/2)
		

		//printf("v_scbroblast[0]%f\n",v_scbroblast[0]);

		last_max_x = max_x;
		last_K_line=K_line;
		last_R_line=R_line;
		
		if(VIDEO_LOG) {
			gettimeofday(&tpend, NULL); 
			timeuse = 1000 * 1000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;
			//printf("<<<black_white_total  timeuse   :%f >>>>\n", timeuse);
			fprintf(fp,"Total timeuse  :%f \n", timeuse);
		}
        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
			errno_exit ("VIDIOC_QBUF"); 
		ret=pthread_mutex_trylock(&mutex_v4l2);           //申请锁定
		if(ret==0)
		{
			V4L2_flag = 1;
			pthread_mutex_unlock(&mutex_v4l2);       //释放
		}
		
	
	}
	if(VIDEO_LOG) fclose(fp);
	
}
uint8_t v4l2_Get_flag(){    
   return  V4L2_flag;
    
}
uint8_t v4l2_Reset_flag(){
    
   pthread_mutex_lock(&mutex_v4l2);           //申请锁定
   V4L2_flag = 0;
   pthread_mutex_unlock(&mutex_v4l2);       //释放
   return  V4L2_flag;
}

/**
 * @brief 将摄像头的内容放到一个数组里然后返回一个指向数组的指针
 * @param 无
 * @retval 
 *TODO:定义具体错误号
 uint8_t v4l2_video_close(){

 stop_capturing();//停止采集
 uninit_device();//释放设备占用
 close_device();//关闭设备
 exit(EXIT_SUCCESS);
 }
 int main (int argc,char ** argv){
 v4l2_video_Init(argc,argv);
 printf("start thread");
 while(1)
 {
 sleep(1);
 }
 }

 */

uint8_t v4l2_video_close(){
	stop_capturing();//停止采集
	uninit_device();//释放设备占用
	close_device();//关闭设备
	exit(EXIT_SUCCESS);
}

double* v4l2_video_Get_BUFF_tag(int turn_direction){
	static int ti = 0;
	//fprintf(fp,"Get***********this is %d frame*************\n", frame);
	//printf("v4l2_video_Get_BUFF_tag:%d \n",ti++);
	mWalkFlag = turn_direction;
	return  v_scbroblast;
}

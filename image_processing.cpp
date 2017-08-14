#include <opencv2\highgui\highgui.hpp>
#include <opencv2\imgproc\imgproc.hpp>
#include <iostream>
#include <time.h>
#include <stdio.h>
#include <vector>
using namespace cv;
using namespace std;

#define PI 3.1415926535
int sum_d8(Mat src_frame, int row, int col);
int zero_one_mode_sum(Mat src_frame, int row, int col);
void thinning_operation(Mat& src_frame, Mat& dst_frame);
void thin(const Mat &src, Mat &dst, const int iterations);

int main()
{

	clock_t start, finish;					//计算程序用时
	VideoCapture capture("new4.h264");		//打开视频文件
	
	if (!capture.isOpened())				//打开失败错误捕捉
	{
		std::printf("failed to open video");
		return 1;
	}
	Mat src_frame;							//原图像

	Mat rgb_frame;							//rgb合成图像储存
	Mat r_frame, g_frame, b_frame;			//r、g、b三个通道的各自图像
	vector<Mat> rgb_channels;

	Mat lab_frame;
	Mat l2_frame, a2_frame, b2_frame;
	vector<Mat> lab_channels;

	Mat result_frame(240, 320, CV_8UC1);
	Mat equalize_frame;

	const int FPS = (int)capture.get(CV_CAP_PROP_FPS);
	int delay = 1000 / FPS;
	const int IMAGE_WIDTH = (int)capture.get(CV_CAP_PROP_FRAME_WIDTH);
	const int IMAGE_HEIGHT = (int)capture.get(CV_CAP_PROP_FRAME_HEIGHT);

	while (1)
	{
		start = clock();
		if (!capture.read(src_frame))
		{
			std::printf("failed to read frame");
			break;
		}

		


		split(src_frame, rgb_channels);
		b_frame = rgb_channels.at(0);
		g_frame = rgb_channels.at(1);
		r_frame = rgb_channels.at(2);
		cv::imshow("b_frame", b_frame);
		cv::imshow("g_frame", g_frame);
		cv::imshow("r_frame", r_frame);

		Mat _2r_g_b_frame;
		_2r_g_b_frame = 2 * r_frame - g_frame - b_frame;



		cvtColor(src_frame, lab_frame, COLOR_BGR2Lab);
		split(lab_frame, lab_channels);
		l2_frame = lab_channels.at(0);
		a2_frame = lab_channels.at(1);
		b2_frame = lab_channels.at(2);

		Mat gaussian_frame;
		gaussian_frame = a2_frame.clone();
		GaussianBlur(a2_frame, gaussian_frame, Size(3, 3), 0, 0);

		cv::imshow("gaussian_frame", gaussian_frame);
		cv::imshow("l2_frame", l2_frame);
		cv::imshow("a2_frame", a2_frame);
		cv::imshow("b2_frame", b2_frame);

		if (waitKey(delay) < 0)
			break;
		Mat hough_frame(240, 320, CV_8UC1);

		double sum = 0;
		double average = 0;
		for (int i = IMAGE_HEIGHT - 1; i >= 0; --i)
		{
			sum = 0;
			average = 0;
			int max = 0;
			for (int j = IMAGE_WIDTH - 1; j >= 0; --j)
			{
				sum += a2_frame.at<uchar>(i, j);
			}
			average = sum / IMAGE_WIDTH;

			for (int j = IMAGE_WIDTH - 1; j >= 0; --j)
			{
				if (a2_frame.at<uchar>(i, j) >= average*1.1 && r_frame.at<uchar>(i, j) >= 20)
					result_frame.at<uchar>(i, j) = 255;
				else
					result_frame.at<uchar>(i, j) = 0;

			}
		}
		cv::imshow("result_frame", result_frame);

		Mat dilate_frame;
		Mat erode_frame;
		Mat element;
		int element_size = 15;
		element = getStructuringElement(MORPH_ELLIPSE, Size(element_size, element_size));
		dilate(result_frame, dilate_frame, element);
		erode(dilate_frame, erode_frame, element);

		cv::imshow("dilate_frame", dilate_frame);
		cv::imshow("erode_frame", erode_frame);


		hough_frame = result_frame.clone();
		for (int i = IMAGE_HEIGHT - 1; i >= 0; --i)
		{
			int right_side = -1;
			int left_side = -1;
			int white_num = 0;
			for (int j = IMAGE_WIDTH - 1; j >= 0; --j)
			{
				hough_frame.at<uchar>(i, j) = 0;
				if (erode_frame.at<uchar>(i, j) == 0)
					continue;
				white_num += 1;
				if (right_side == -1)
				{
					right_side = j;
				}
				if ((erode_frame.at<uchar>(i, j - 1) == 0 && right_side != -1) || j == 0)
					left_side = j;
				else
					continue;
				if (white_num >= 15)
				{
					hough_frame.at<uchar>(i, (right_side + left_side) / 2) = 255;
				}
				right_side = -1;
				left_side = -1;
				white_num = 0;
			}
		}
		cv::imshow("hough_frame", hough_frame);
		Mat linefit_frame(240, 320, CV_8UC3, Scalar::all(0));
		vector<Point> line_start;			//每条线的开始点
		vector<Point> line_last;			//每条线的上一点
		vector<int> line_point_count;		//每条线已有点数
		vector<double> line_k;
		int line_count = 0;					//记录已有线数
		int point_count = 0;				//记录当前点数
		int if_belonging = 0;				//记录当前点是否已经归属
		int last_j = -1;
		for (int i = IMAGE_HEIGHT - 1; i >= 0; --i)
		{
			point_count = 0;
			for (int j = IMAGE_WIDTH - 1; j >= 0; --j)
			{
				if (hough_frame.at<uchar>(i, j) == 0)
					continue;


				if (hough_frame.at<uchar>(i, j - 1) == 0 && hough_frame.at<uchar>(i, j + 1) == 0 && hough_frame.at<uchar>(i - 1, j) == 0 && hough_frame.at<uchar>(i + 1, j) == 0 &&
					hough_frame.at<uchar>(i - 1, j - 1) == 0 && hough_frame.at<uchar>(i - 1, j + 1) == 0 && hough_frame.at<uchar>(i + 1, j - 1) == 0 && hough_frame.at<uchar>(i + 1, j + 1) == 0)
				{
					hough_frame.at<uchar>(i, j) = 0;
					continue;
				}



				point_count += 1;
				if_belonging = 0;							//该点设为未归属
				if (point_count == 1)
					last_j = j;
				if (point_count >= 2)
				{
					if (abs(j - last_j) <= 15)
					{
						int line_num;
						line_num = hough_frame.at<uchar>(i, last_j);
						line_last.at(line_num - 1).y = (j + last_j) / 2;
						hough_frame.at<uchar>(i, j) = 0;
						hough_frame.at<uchar>(i, last_j) = 0;
						hough_frame.at<uchar>(i, (j + last_j) / 2) = line_num;
						last_j = (j + last_j) / 2;
						continue;
					}
				}


				double min_k = 10000;
				int min_k_line = -1;
				int min_distance = 1000;
				int min_d_line = -1;
				double last_k = 0;
				double last_angle = 0;
				double now_k = 0;
				double now_angle = 0;
				double neighbor_k = 0;
				double neighbor_angle = 0;
				for (int m = 0; m < line_count; m++)		//斜率检测归属
				{
					int distance = 0;
					distance = abs(line_last.at(m).x - i) + abs(line_last.at(m).y - j);
					if (distance < min_distance)
					{
						min_distance = distance;
						min_d_line = m;
					}
					if (line_point_count.at(m) <= 5)
						continue;
					if (abs(i - line_last.at(m).x)>50)
						continue;
					if (abs(i - line_last.at(m).x) == 0)
						continue;
					last_k = (line_last.at(m).y - line_start.at(m).y) / (double)(line_last.at(m).x - line_start.at(m).x);
					last_angle = atan(last_k);
					now_k = (j - line_start.at(m).y) / (double)(i - line_start.at(m).x);
					now_angle = atan(now_k);
					if (abs(last_angle - now_angle)*180.0 / PI < min_k)
					{
						min_k = abs(last_angle - now_angle)*180.0 / PI;
						min_k_line = m;
					}
				}

				if (min_d_line == min_k_line && min_k_line >= 0 && min_k <= 5 && min_distance <= 8)
				{
					line_last.at(min_k_line).x = i;			//设置该线上一点x
					line_last.at(min_k_line).y = j;			//设置该线上一点y
					line_point_count.at(min_k_line) += 1;	//该线内点数目+1
					if_belonging = 1;						//该点已经归属
					hough_frame.at<uchar>(i, j) = min_k_line + 1;
				}
				else
				{

					if (min_k_line >= 0)
					{
						neighbor_k = (j - line_start.at(min_k_line).y) / (double)(i - line_start.at(min_k_line).x);
						neighbor_angle = atan(neighbor_k);
						if (min_k <= 5 && abs(neighbor_angle - now_angle) * 180 / PI <= 20)
						{
							line_last.at(min_k_line).x = i;			//设置该线上一点x
							line_last.at(min_k_line).y = j;			//设置该线上一点y
							line_point_count.at(min_k_line) += 1;	//该线内点数目+1
							if_belonging = 1;						//该点已经归属
							hough_frame.at<uchar>(i, j) = min_k_line + 1;
						}
					}
					if (if_belonging)							//若点已经归属
						continue;
					if (min_distance <= 8 && line_point_count.at(min_d_line) <= 10)
					{
						line_last.at(min_d_line).x = i;			//设置该线上一点x
						line_last.at(min_d_line).y = j;			//设置该线上一点y
						line_point_count.at(min_d_line) += 1;	//该线内点数目+1
						if_belonging = 1;				//该点已经归属
						hough_frame.at<uchar>(i, j) = min_d_line + 1;
					}

				}

				if (if_belonging)							//若点已经归属，则不新建一条线
					continue;

				//新建一条直线
				line_count += 1;						//帧内线数+1；
				line_start.push_back(Point(i, j));
				printf("start_point is x:%d\ty:%d\n", line_start.at(line_count - 1).x, line_start.at(line_count - 1).y);
				line_last.push_back(Point(i, j));
				line_point_count.push_back(1);
				line_k.push_back(-1);
				hough_frame.at<uchar>(i, j) = line_count;
			}
		}
		printf("line_count is:%d\n", line_count);


		for (int i = IMAGE_HEIGHT - 1; i >= 0; --i)
		{
			for (int j = IMAGE_WIDTH - 1; j >= 0; --j)
			{
				int num;
				num = hough_frame.at<uchar>(i, j);
				if (num <= 0)
					continue;
				if (line_point_count.at(num - 1) >= 25)
					linefit_frame.at<Vec3b>(i, j) = Vec3b(60 * num, 255 * (num % 2), 150 - 10 * num);		//该点置色；
			}
		}
		for (int i = 0; i < line_start.size(); ++i)
		{
			if (line_point_count.at(i) >= 25)
			{
				int temp = 0;;
				temp = line_last.at(i).x;
				line_last.at(i).x = line_last.at(i).y;
				line_last.at(i).y = temp;

				temp = line_start.at(i).x;
				line_start.at(i).x = line_start.at(i).y;
				line_start.at(i).y = temp;
				line(src_frame, line_last.at(i), line_start.at(i), Scalar(50 * (i + 1), 100 + 30 * (i + 1), 255 - 20 * (i + 1)), 1);
			}
		}


		cv::imshow("src_frame", src_frame);
		cv::imshow("linefit_frame", linefit_frame);

		//vector<Vec2f> lines;
		//cv::HoughLines(hough_frame, lines, 3.5, 3.1415926 / 20, 60, 0, 0);
		//std::printf("the line is : %d\n", lines.size());


		finish = clock();
		std::printf("the using time is:%d\n", finish - start);
	}

	system("pause");
	return 0;
}

int sum_d8(Mat src_frame, int row, int col)
{
	int sum = 0;
	sum += src_frame.at<uchar>(row - 1, col);		//P2
	sum += src_frame.at<uchar>(row - 1, col + 1);	//P3
	sum += src_frame.at<uchar>(row, col + 1);		//P4
	sum += src_frame.at<uchar>(row + 1, col + 1);	//P5
	sum += src_frame.at<uchar>(row + 1, col);		//P6
	sum += src_frame.at<uchar>(row + 1, col - 1);	//P7
	sum += src_frame.at<uchar>(row, col - 1);		//P8
	sum += src_frame.at<uchar>(row - 1, col - 1);	//P9
	return sum / 255;
}

int zero_one_mode_sum(Mat src_frame, int row, int col)
{
	int count = 0;
	if (src_frame.at<uchar>(row - 1, col) == 0 && src_frame.at<uchar>(row - 1, col + 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row - 1, col + 1) == 0 && src_frame.at<uchar>(row, col + 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row, col + 1) == 0 && src_frame.at<uchar>(row + 1, col + 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row + 1, col + 1) == 0 && src_frame.at<uchar>(row + 1, col) == 255)
		count += 1;
	if (src_frame.at<uchar>(row + 1, col) == 0 && src_frame.at<uchar>(row + 1, col - 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row + 1, col - 1) == 0 && src_frame.at<uchar>(row, col - 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row, col - 1) == 0 && src_frame.at<uchar>(row - 1, col - 1) == 255)
		count += 1;
	if (src_frame.at<uchar>(row - 1, col - 1) == 0 && src_frame.at<uchar>(row - 1, col) == 255)
		count += 1;
	return count;
}

void thinning_operation(Mat& src_frame, Mat& dst_frame)
{


	Mat temp_frame;	//临时图像
	src_frame.copyTo(dst_frame);
	int image_height, image_width;
	image_height = src_frame.rows;
	image_width = src_frame.cols;
	int i, j;
	for (i = 0; i < image_height; ++i)
	{
		for (j = 0; j < image_width; ++j)
		{
			if (i == 0 || j == 0 || i == (image_height - 1) || j == (image_width - 1))
				dst_frame.at<uchar>(i, j) = 0;
		}
	}
	int integration_bool = true;
	int count = 0;
	int integration = 20;
	while (integration_bool && (integration--))
	{
		count++;
		printf("%d\t%d\n", integration_bool,count);
		integration_bool = false;
		dst_frame.copyTo(temp_frame);
		for (i = 1; i < image_height - 1; ++i)
		{
			for (j = 1; j < image_width - 1; ++j)
			{
				if (sum_d8(temp_frame, i, j) < 2 || sum_d8(temp_frame, i, j) > 6)		//不满足删除条件1
					continue;
				if (zero_one_mode_sum(temp_frame, i, j) != 1)	//不满足删除条件2
					continue;
				if (temp_frame.at<uchar>(i - 1, j)*temp_frame.at<uchar>(i, j + 1)*temp_frame.at<uchar>(i + 1, j) != 0)
					continue;
				if (temp_frame.at<uchar>(i, j + 1)*temp_frame.at<uchar>(i + 1, j)*temp_frame.at<uchar>(i, j - 1) != 0)
					continue;
				dst_frame.at<uchar>(i, j) = 120;
				integration_bool = true;
			}
		}
		for (i = 1; i < image_height - 1; ++i)
		{
			for (j = 1; j < image_width - 1; ++j)
			{
				if (dst_frame.at<uchar>(i, j) == 120)
					dst_frame.at<uchar>(i, j) = 0;
			}
		}
		dst_frame.copyTo(temp_frame);
		for (i = 1; i < image_height - 1; ++i)
		{
			for (j = 1; j < image_width - 1; ++j)
			{
				if (sum_d8(temp_frame, i, j) < 2 || sum_d8(temp_frame, i, j) > 6)		//不满足删除条件1
					continue;
				if (zero_one_mode_sum(temp_frame, i, j) != 1)	//不满足删除条件2
					continue;
				if (temp_frame.at<uchar>(i - 1, j)*temp_frame.at<uchar>(i, j + 1)*temp_frame.at<uchar>(i, j - 1) != 0)
					continue;
				if (temp_frame.at<uchar>(i - 1, j)*temp_frame.at<uchar>(i + 1, j)*temp_frame.at<uchar>(i, j - 1) != 0)
					continue;
				dst_frame.at<uchar>(i, j) = 120;
				integration_bool = true;
			}
		}
		for (i = 1; i < image_height - 1; ++i)
		{
			for (j = 1; j < image_width - 1; ++j)
			{
				if (dst_frame.at<uchar>(i, j) == 120)
					dst_frame.at<uchar>(i, j) = 0;
			}
		}
		//cv::imshow("dst_frame", dst_frame);
		//waitKey(1);
	}

}


void thin(const Mat &src, Mat &dst, const int iterations){
	const int height = src.rows - 1;
	const int width = src.cols - 1;
	//拷贝一个数组给另一个数组
	if (src.data != dst.data)
		src.copyTo(dst);
	int n = 0, i = 0, j = 0;
	Mat tmpImg;
	uchar *pU, *pC, *pD;
	bool isFinished = false;
	for (n = 0; n<iterations; n++){
		dst.copyTo(tmpImg);
		isFinished = false;   //一次 先行后列扫描 开始
		//扫描过程一 开始
		for (i = 1; i<height; i++) {
			pU = tmpImg.ptr<uchar>(i - 1);
			pC = tmpImg.ptr<uchar>(i);
			pD = tmpImg.ptr<uchar>(i + 1);
			for (int j = 1; j<width; j++){
				if (pC[j] > 0){
					int ap = 0;
					int p2 = (pU[j] >0);
					int p3 = (pU[j + 1] >0);
					if (p2 == 0 && p3 == 1)
						ap++;
					int p4 = (pC[j + 1] >0);
					if (p3 == 0 && p4 == 1)
						ap++;
					int p5 = (pD[j + 1] >0);
					if (p4 == 0 && p5 == 1)
						ap++;
					int p6 = (pD[j] >0);
					if (p5 == 0 && p6 == 1)
						ap++;
					int p7 = (pD[j - 1] >0);
					if (p6 == 0 && p7 == 1)
						ap++;
					int p8 = (pC[j - 1] >0);
					if (p7 == 0 && p8 == 1)
						ap++;
					int p9 = (pU[j - 1] >0);
					if (p8 == 0 && p9 == 1)
						ap++;
					if (p9 == 0 && p2 == 1)
						ap++;
					if ((p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9)>1 && (p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9)<7){
						if (ap == 1){
							if ((p2*p4*p6 == 0) && (p4*p6*p8 == 0)){
								dst.ptr<uchar>(i)[j] = 0;
								isFinished = true;
							}
						}
					}
				}

			} //扫描过程一 结束
			dst.copyTo(tmpImg);
			//扫描过程二 开始
			for (i = 1; i<height; i++){
				pU = tmpImg.ptr<uchar>(i - 1);
				pC = tmpImg.ptr<uchar>(i);
				pD = tmpImg.ptr<uchar>(i + 1);
				for (int j = 1; j<width; j++){
					if (pC[j] > 0){
						int ap = 0;
						int p2 = (pU[j] >0);
						int p3 = (pU[j + 1] >0);
						if (p2 == 0 && p3 == 1)
							ap++;
						int p4 = (pC[j + 1] >0);
						if (p3 == 0 && p4 == 1)
							ap++;
						int p5 = (pD[j + 1] >0);
						if (p4 == 0 && p5 == 1)
							ap++;
						int p6 = (pD[j] >0);
						if (p5 == 0 && p6 == 1)
							ap++;
						int p7 = (pD[j - 1] >0);
						if (p6 == 0 && p7 == 1)
							ap++;
						int p8 = (pC[j - 1] >0);
						if (p7 == 0 && p8 == 1)
							ap++;
						int p9 = (pU[j - 1] >0);
						if (p8 == 0 && p9 == 1)
							ap++;
						if (p9 == 0 && p2 == 1)
							ap++;
						if ((p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9)>1 && (p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9)<7){
							if (ap == 1){
								if ((p2*p4*p8 == 0) && (p2*p6*p8 == 0)){
									dst.ptr<uchar>(i)[j] = 0;
									isFinished = true;
								}
							}
						}
					}
				}
			} //一次 先行后列扫描完成          
			//如果在扫描过程中没有删除点，则提前退出
			if (isFinished == false)
				break;
		}
	}
}


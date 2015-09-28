#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iterator>
#include <opencv2/rgbd/rgbd.hpp>
#include <geometry_msgs/Point32.h>
#include <tf/transform_listener.h>
#include <math.h>

static const std::string OPENCV_WINDOW = "Image window";

struct MyPoint
{
	int x,y;
};

class ImageConverter
{
	ros::NodeHandle nh_;
	image_transport::ImageTransport it_;
	image_transport::Subscriber image_sub_;
	image_transport::Publisher image_pub_;
	image_transport::Publisher image_pub_dgod_;
	image_transport::Publisher image_pub_cluster_;
	image_transport::Publisher image_pub_drawing_;
	image_transport::Publisher image_pub_gripper_cluster;
	ros::Publisher cluster_points;

	public:
	ImageConverter()
		: it_(nh_)
	{
		// depth/image is distance in meters, is F32C1
		// (depth/image_raw is mm distance, in U16C1)
		image_sub_ = it_.subscribe("/head_camera/depth/image",1,
				&ImageConverter::imageCb, this);
		image_pub_ = it_.advertise("/image_converter/output_video", 1);
		image_pub_dgod_ = it_.advertise("/image_converter_dgod/output_video", 1);
		image_pub_cluster_ = it_.advertise("/image_converter_cluster/output_video", 1);
		image_pub_drawing_ = it_.advertise("/image_converter_drawing/output_video", 1);
		// cluster_points = it.advertise<sensor_msgs::PointCloud>("points3d_", 1);
		image_pub_gripper_cluster = it_.advertise("final_cluster", 1);

	}

	~ImageConverter()
	{
	}

	void imageCb(const sensor_msgs::ImageConstPtr& msg)
	{
		cv_bridge::CvImagePtr cv_ptr;
		cv_bridge::CvImagePtr crap;	
		cv_bridge::CvImagePtr mgod;
		cv_bridge::CvImagePtr dgod;
		cv_bridge::CvImagePtr cluster;
		try
		{
			crap = cv_bridge::toCvCopy(msg, "32FC1");
			cv_ptr = cv_bridge::toCvCopy(msg, "32FC1");
			mgod = cv_bridge::toCvCopy(msg, "32FC1");
			dgod = cv_bridge::toCvCopy(msg, "32FC1");
			cluster = cv_bridge::toCvCopy(msg, "32FC1");

		}
		catch (cv_bridge::Exception& e)
		{
			ROS_ERROR("cv_bridge exception: %s", e.what());
			return;
		}


		cv::erode(cv_ptr->image, cv_ptr->image, cv::Mat(), cv::Point(-1, -1), 10);


		float dx;
		float dy;

		for( size_t i=1 ;i<(cv_ptr->image.rows - 1);i++)
		{
			for(size_t j=1; j<(cv_ptr->image.cols - 1); j++)
			{ 
				mgod->image.at<float>(i, j) = sqrt(((cv_ptr->image.at<float>(i+1,j)-cv_ptr->image.at<float>(i-1,j))* (cv_ptr->image.at<float>(i+1,j)-cv_ptr->image.at<float>(i-1,j))) + ((cv_ptr->image.at<float>(i,j+1)- cv_ptr->image.at<float>(i,j-1))*( cv_ptr->image.at<float>(i,j+1)- cv_ptr->image.at<float>(i,j-1))));
				// float val1 = cv_ptr->image.at<float>(i,j+1);
				// float val2 = cv_ptr->image.at<float>(i,j-1);
				// float val3 = cv_ptr->image.at<float>(i+1,j);
				// float val4 = cv_ptr->image.at<float>(i-1,j);  

				// if (isnan(val1) || isnan(val2) || isnan(val3) || isnan(val4) || val1 == FLT_MAX || val2 == FLT_MAX || val3 == FLT_MAX || val4 == FLT_MAX)
				//			{
				// float val = cv_ptr->image.at<float>(i,j);
				// if ((fabsf(val) > 1e10))
				// {
				//  std::cout<< "##" << (i) << ',' << (j) << ',' << val << std::endl;
				// }

				//float result = 300;
				//if (fabsf(val) > 1e10)
				// {
				//   result = 0;
				// }
				//			  dgod->image.at<float>(i,j) = FLT_MAX ;
				//			}
				//			else 
				{
					dx = cv_ptr->image.at<float>(i,j+1) - cv_ptr->image.at<float>(i,j-1); 
					dy = cv_ptr->image.at<float>(i+1,j) - cv_ptr->image.at<float>(i-1,j);

					if (dy == 0 && dx == 0)
					{
						for (size_t iter = 2 ; iter <= 5; iter++)
						{               
							dx = cv_ptr->image.at<float>(i,j+iter) - cv_ptr->image.at<float>(i,j-iter);
							dy = cv_ptr->image.at<float>(i+iter,j) - cv_ptr->image.at<float>(i-iter,j);

							if (dy == 0 && dx == 0) 
							{continue;}
							else
							{break; }      

							//           dgod->image.at<float>(i,j) = 0;
						}
						dgod->image.at<float>(i,j) = 0;

					}
					if (dx == 0 && dy ==0)
					{
						dgod->image.at<float>(i,j) = 0;
					}
					else if ( dx < 0 && dy == 0)
					{
						dgod->image.at<float>(i,j) = 180;
					}
					else if ( dx > 0 && dy == 0)
					{
						dgod->image.at<float>(i,j) = 360;
					}
					else 
					{
						dgod->image.at<float>(i,j) =  atan2(dy,dx) * 180/M_PI;
						if (dgod->image.at<float>(i,j) <0)
						{
							dgod->image.at<float>(i,j) = 360+ dgod->image.at<float>(i,j);

						}
					}    
				}
			}
		}
		for (size_t i=3; i<(cv_ptr->image.rows-3); i++)
		{
			for(size_t j=3; j<(cv_ptr->image.cols-3); j++)
			{
				int val[2][8];
				int iter = 0;
				for (int k= 0; k<9  ; k++)
				{
					val[0][k] = iter;
					val[1][k] = 0;
					iter += 45;

				} val[1][0] = 0;

				for (int x=-3; x<4; x++)
				{  
					for(int y=-3; y<4; y++)   
					{   

						float pixel =  dgod->image.at<float>(i+x,j+y);
						if (pixel<= 22.5)
							val[1][0]++;
						else if (pixel>22.5 && pixel<= 67.5)
							val[1][1]++;
						else if (pixel>67.5 && pixel<= 112.5)
							val[1][2]++;
						else if (pixel>112.5 && pixel<= 157.5)
							val[1][3]++;
						else if (pixel>157.5 && pixel<= 202.5)
							val[1][4]++;
						else if (pixel>202.5 && pixel<= 247.5)
							val[1][5]++;
						else if (pixel>247.5 && pixel<= 292.5)
							val[1][6]++;
						else if (pixel>292.5 && pixel<= 337.5)
							val[1][7]++;
						else if (pixel>337.5 && pixel<= 360)
							val[1][8]++;
					}
				}
				int highest_count = val[1][0];  
				float result = val[0][0];
				for ( int k = 1; k < 9; k++)
				{  
					if (val[1][k] > highest_count)
					{
						highest_count = val[1][k];
						result = val[0][k];

					}
				}

				cluster->image.at<float>(i,j) = result;

			}
		} 

		// Magnitude gradient of depth
		double min, max;
		cv::minMaxLoc(mgod->image , &min, &max );

		for( size_t i=1 ;i<(cv_ptr->image.rows - 1);i++)
		{
			for(size_t j=1; j<(cv_ptr->image.cols - 1); j++)
			{ 
				mgod->image.at<float>(i, j) = (mgod->image.at<float>(i, j) - min) /(max-min);
			}
		}



		int total_size = cv_ptr->image.rows * cv_ptr->image.cols;
		std::vector<bool> checked (total_size , false);

		std::vector< std::vector<size_t> >clusters;
		for (size_t i=1 ; i<cv_ptr->image.rows-1; i++)
		{
			for(size_t j=1; j< cv_ptr->image.cols-1; j++)
			{


				if(dgod->image.at<float>(i,j) == FLT_MAX)
				{
					continue;
				}         

				std::vector<size_t> seed_q;
				size_t index = i * cv_ptr->image.cols + j;

				if(checked[index])
					continue;

				seed_q.push_back(index);

				size_t seed_index=0;

				checked[index] = true;


				while (seed_index < static_cast<int> (seed_q.size ()))
				{
					// Search for sq_idx
					int m;
					int n;
					m = seed_q[seed_index]/cv_ptr->image.cols;            
					n = seed_q[seed_index] % cv_ptr->image.cols;
					for (int x=-1; x<2; x++)
					{  
						for(int y=-1; y<2; y++)
						{
							size_t k = (m+x) * cv_ptr->image.cols + (n+y);
							if ((m+x)<0 || (n+y)<0 || (m+x)>=cv_ptr->image.rows ||(n+y)>=cv_ptr->image.cols)
								continue;                     

							if (checked[k])                         
								continue;

							if (cluster->image.at<float>(m,n) == cluster->image.at<float>(m+x,n+y)   )
							{							
								checked[k] = true;
								seed_q.push_back (k);
							}


						}
					}
					seed_index++; }

				if (seed_q.size () >= 500 && seed_q.size () <= 50000)
				{ 
					clusters.push_back(seed_q );
				}

			}
		}

		cv::Mat clustered= cv::Mat::zeros( cv_ptr->image.size(), CV_8UC3);
		cv::Mat individual_clusters = cv::Mat::zeros(cv_ptr->image.size(), CV_32FC1);

		cv::RNG rng(12345);

		for (int i = 0; i< clusters.size(); i++)
		{ 
			cv::Scalar color = cv::Scalar( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );

			for (int j=0; j<clusters[i].size();j++)
			{
				int  m = clusters[i][j] /cv_ptr->image.cols;
				int  n = clusters[i][j] % cv_ptr->image.cols;


				clustered.at<cv::Vec3b>(m,n)[0] = color[0];
				clustered.at<cv::Vec3b>(m,n)[1] = color[1];
				clustered.at<cv::Vec3b>(m,n)[2] = color[2];
				individual_clusters.at<cv::Vec2f>(m,n)[0] = i;
				individual_clusters.at<cv::Vec2f>(m,n)[1] = cv_ptr->image.at<float>(m,n);     



			}}

		cv::Mat K_ = (cv::Mat_<double>(3, 3) <<
				557.7233725919907, 0, 319.6929001020021, 0, 550.2601417321275, 225.9973984128593,
				0.0, 0.0, 1.0);
		cv::Mat points3d;
		//cv::depthTo3d(individual_clusters, K_, points3d);
		cv::depthTo3d(cv_ptr->image , K_, points3d);

		cv::Mat channels[3];
		cv::split(points3d, channels);

		cv::Mat centroids;

		int count = 0;
		for(size_t i= 0 ; i< clusters.size(); i++)
		{ 
			double x = 0;
			double y = 0;
			double z = 0;
			for(size_t j =0; j<clusters[i].size(); j++)
			{

				int m = clusters[i][j] /cv_ptr->image.cols;
				int n = clusters[i][j] % cv_ptr->image.cols;

				if(cv_ptr->image.at<float>(m,n) == FLT_MAX ||  isnan(channels[0].at<float>(m,n))  || isnan(channels[1].at<float>(m,n)) || isnan(channels[2].at<float>(m,n)))
				{} 
				else 
				{ count++ ;

					x += channels[0].at<float>(m,n);
					y += channels[1].at<float>(m,n);
					z += channels[2].at<float>(m,n);
				}

			}


			cv::Vec3f centroid_point(x/count, y/count, z/count); 

			std::cout << i <<"\t" << count <<"\t" << x/count << "\t" << y/count <<"\t" << z/count <<std::endl;


			centroids.push_back(centroid_point); 
		}

		//for(size_t i = 0;i<centroids.size(); i++)
		//{
		//std::cout << centroids[i].at<float>(0,0) <<std::endl ;
		//}

		//for( int i = 0; i< centroids.rows; i++)
		//{
		// std::cout <<  centroids.at<cv::Vec3f>(i,0)[0] << std::endl;

		//}

		tf::Stamped<tf::Point> gripper_centroid;
		gripper_centroid.setX(0.06);
		gripper_centroid.setY(0.00);
		gripper_centroid.setZ(0.035);
		gripper_centroid.frame_id_ = msg->header.frame_id;
		tf::Stamped<tf::Point> gripper_centroid_transformed;
		tf::TransformListener listener;
		try{
			listener.waitForTransform(  "/head_camera_depth_optical_frame", "wrist_roll_link",
					ros::Time(0), ros::Duration(3.0)); 
			listener.transformPoint("/head_camera_depth_optical_frame", 
					ros::Time(0), gripper_centroid , "/wrist_roll_link", gripper_centroid_transformed);
		}
		catch (tf::TransformException ex){
			ROS_ERROR("%s",ex.what());
			ros::Duration(1.0).sleep();
		}
		cv::Vec3f gripper_centroid_transform;
		gripper_centroid_transform[0] = gripper_centroid_transformed.x();
		gripper_centroid_transform[1] = gripper_centroid_transformed.y();
		gripper_centroid_transform[2] = gripper_centroid_transformed.z();

		float min_distance = 1000;
		int closest_centroid= 1000;
		//std::cout << centroids.rows <<std::endl;
		for( int i = 0; i< centroids.rows; i++)
		{
			float distance = pow((gripper_centroid_transform[0]-centroids.at<cv::Vec3f>(i,0)[0]) ,2) + pow((gripper_centroid_transform[1]-centroids.at<cv::Vec3f>(i,0)[1]) ,2) + pow((gripper_centroid_transform[2]-centroids.at<cv::Vec3f>(i,0)[2]) ,2);
			distance = sqrt(distance);
			if (distance < min_distance) 
			{
				min_distance = distance ;
				closest_centroid = i;

			}
		}

		std::cout << centroids.at<cv::Vec3f>(closest_centroid,0)[0] << "\t" << centroids.at<cv::Vec3f>(closest_centroid,0)[1] << "\t" << centroids.at<cv::Vec3f>(closest_centroid,0)[2] << std::endl;

		cv::Scalar color = cv::Scalar( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );
		cv::Mat gripper_cluster = cv::Mat::zeros( cv_ptr->image.size(), CV_8UC3);

		for (int j=0; j<clusters[closest_centroid].size();j++)
		{
			int m = clusters[closest_centroid][j] /cv_ptr->image.cols;
			int n = clusters[closest_centroid][j] % cv_ptr->image.cols;

			gripper_cluster.at<cv::Vec3b>(m,n)[0] = color[0];
			gripper_cluster.at<cv::Vec3b>(m,n)[1] = color[1];
			gripper_cluster.at<cv::Vec3b>(m,n)[2] = color[2];

		}

		cv::vector<cv::vector<cv::Point> >  contours_mgod;
		cv::Mat type_mat;
		cluster->image.convertTo(type_mat , CV_8UC1);

		cv::findContours(type_mat, contours_mgod,CV_RETR_TREE , CV_CHAIN_APPROX_SIMPLE);

		cv::Mat drawing= cv::Mat::zeros( type_mat.size(), CV_8UC3 );
		ros::Time time = ros::Time::now();		

		// convert OpenCV image to ROS message
		cv_bridge::CvImage cvi;			
		cvi.header.stamp = time;
		cvi.header.frame_id = "camera";
		cvi.encoding = "bgr8";
		cvi.image =  clustered;


		cv_bridge::CvImage cvii;
		cvii.header.stamp = time;
		cvii.header.frame_id = "camera";
		cvii.encoding = "bgr8";
		cvii.image =  gripper_cluster;


		image_pub_.publish(mgod->toImageMsg());
		image_pub_dgod_.publish(dgod->toImageMsg());
		image_pub_cluster_.publish(cluster->toImageMsg());
		image_pub_drawing_.publish( cvi.toImageMsg());
		image_pub_gripper_cluster.publish( cvii.toImageMsg());
	}
};

int main(int argc, char** argv)
{
	ros::init(argc, argv, "image_converter");
	ImageConverter ic;
	ros::spin();
	return 0;
}
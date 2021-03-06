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
  //cv::Ptr<cv::RgbdNormals> normals_estimator_;
  //cv::Ptr<cv::RgbdPlane> plane_estimator_;

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

//  cv::Ptr<cv::RgbdNormals> normals_estimator_;
//  cv::Ptr<cv::RgbdPlane> plane_estimator_;
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

    // erode the image to break the connection between the gripper fingers and the gripper plane
    cv::erode(cv_ptr->image, cv_ptr->image, cv::Mat(), cv::Point(-1, -1), 10);

    float dx;
    float dy;
    
    // Calculate magnitude gradient of depth (mgod) 
    // and diffrential gradient of depth (dgod) for each pixel

    for(size_t i = 1 ;i < (cv_ptr->image.rows - 1); i++)
    {
      for(size_t j = 1; j < (cv_ptr->image.cols - 1); j++)
      { 
        mgod->image.at<float>(i, j) = sqrt(((cv_ptr->image.at<float>(i+1,j)-cv_ptr->image.at<float>(i-1,j))* (cv_ptr->image.at<float>(i+1,j)-cv_ptr->image.at<float>(i-1,j))) 
             + ((cv_ptr->image.at<float>(i,j+1)- cv_ptr->image.at<float>(i,j-1))*( cv_ptr->image.at<float>(i,j+1)- cv_ptr->image.at<float>(i,j-1))));
          
        
        dx = cv_ptr->image.at<float>(i,j+1) - cv_ptr->image.at<float>(i,j-1); 
        dy = cv_ptr->image.at<float>(i+1,j) - cv_ptr->image.at<float>(i-1,j);

        if (dy == 0 && dx == 0)
        {
          for (size_t iter = 2 ; iter <= 5; iter++)
          {               
            dx = cv_ptr->image.at<float>(i,j+iter) - cv_ptr->image.at<float>(i,j-iter);
            dy = cv_ptr->image.at<float>(i+iter,j) - cv_ptr->image.at<float>(i-iter,j);
 
            if (dy == 0 && dx == 0)
            {
              continue;
            }
            else
            {
              break; 
            }
          } 
          dgod->image.at<float>(i,j) = 0;
        }

        if (dx == 0 && dy == 0)
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
            dgod->image.at<float>(i,j) = 360 + dgod->image.at<float>(i,j);
          }
        }    
      }
    }
 
    // Cluster dgod into 9 different bins based on voting 
    for(size_t i = 3; i < (cv_ptr->image.rows - 3); i++)
    {
      for(size_t j = 3; j < (cv_ptr->image.cols - 3); j++)
      {
        int val[2][8];
        int iter = 0;
        for (int k = 0; k < 9 ; k++)
        {
          val[0][k] = iter;
          val[1][k] = 0;
          iter += 45;

        } val[1][0] = 0;

        for (int x =- 3; x < 4; x++)
        {  
          for(int y =- 3; y < 4; y++)   
          {   

            float pixel = dgod->image.at<float>(i+x,j+y);
            if (pixel <= 22.5)
              val[1][0]++;
            else if (pixel > 22.5 && pixel <= 67.5)
              val[1][1]++;
            else if (pixel > 67.5 && pixel <= 112.5)
              val[1][2]++;
            else if (pixel > 112.5 && pixel <= 157.5)
              val[1][3]++;
            else if (pixel > 157.5 && pixel <= 202.5)
              val[1][4]++;
            else if (pixel > 202.5 && pixel <= 247.5)
              val[1][5]++;
            else if (pixel > 247.5 && pixel <= 292.5)
              val[1][6]++;
            else if (pixel > 292.5 && pixel <= 337.5)
              val[1][7]++;
            else if (pixel > 337.5 && pixel <= 360)
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

    // Normalize magnitude gradient of depth
    double min, max;
    cv::minMaxLoc(mgod->image , &min, &max );

    for(size_t i = 1 ; i < (cv_ptr->image.rows - 1); i++)
    {
      for(size_t j = 1; j < (cv_ptr->image.cols - 1); j++)
      { 
        mgod->image.at<float>(i, j) = (mgod->image.at<float>(i, j) - min) / (max-min);
      }
    }

   // Clustering

    int total_size = cv_ptr->image.rows * cv_ptr->image.cols;
    std::vector<bool> checked (total_size , false);

    std::vector< std::vector<size_t> >clusters;
    for (size_t i = 1 ; i < cv_ptr->image.rows - 1; i++)
    {
      for(size_t j = 1; j < cv_ptr->image.cols - 1; j++)
      {
        if(dgod->image.at<float>(i,j) == FLT_MAX)
        {
          continue;
        }         

        std::vector<size_t> seed_q;
        size_t index = i * cv_ptr->image.cols + j;

        //if checked the pixel already then continue
        if(checked[index])
          continue;

        //create a seed queue for clustering
        seed_q.push_back(index);

        size_t seed_index=0;

        checked[index] = true;

        while (seed_index < static_cast<int> (seed_q.size ()))
        {
          // Search for seed queue index
          int m;
          int n;
          m = seed_q[seed_index]/cv_ptr->image.cols;            
          n = seed_q[seed_index] % cv_ptr->image.cols;
          for (int x =- 1; x < 2; x++)
          {  
            for(int y =- 1; y < 2; y++)
            {
              size_t k = (m+x) * cv_ptr->image.cols + (n+y);
              //ensure that region growing doesn't go outside the image
              if ((m+x) < 0 || (n+y) < 0 || (m+x)>=cv_ptr->image.rows ||(n+y)>=cv_ptr->image.cols)
                continue;                     

              if (checked[k])                         
                continue;

              // if the pixels belong to the same bin then add them to the queue
              if (cluster->image.at<float>(m,n) == cluster->image.at<float>(m+x,n+y)   )
              {             
                checked[k] = true;
                seed_q.push_back (k);
              }
            }
          }
          seed_index++; 
        }

        // if the queue size is within acceptable gripper size
        if (seed_q.size () >= 1000 && seed_q.size () <= 50000)
        { 
          clusters.push_back(seed_q );
        }
      }
    }

    cv::Mat clustered= cv::Mat::zeros( cv_ptr->image.size(), CV_8UC3);
    cv::Mat individual_clusters = cv::Mat::zeros(cv_ptr->image.size(), CV_32FC1);

    // color all the clusters differntly for better visualization
    cv::RNG rng(12345);

    for (int i = 0; i < clusters.size(); i++)
    { 
      cv::Scalar color = cv::Scalar( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );

      for (int j = 0; j < clusters[i].size(); j++)
      {
        int  m = clusters[i][j] / cv_ptr->image.cols;
        int  n = clusters[i][j] % cv_ptr->image.cols;

        clustered.at<cv::Vec3b>(m,n)[0] = color[0];
        clustered.at<cv::Vec3b>(m,n)[1] = color[1];
        clustered.at<cv::Vec3b>(m,n)[2] = color[2];
        individual_clusters.at<cv::Vec2f>(m,n)[0] = i;
        individual_clusters.at<cv::Vec2f>(m,n)[1] = cv_ptr->image.at<float>(m,n);     

      }
    }

    // convert the image into 3d points
    cv::Mat K_ = (cv::Mat_<double>(3, 3) <<
        557.7233725919907, 0, 319.6929001020021, 0, 550.2601417321275, 225.9973984128593,
        0.0, 0.0, 1.0);
    cv::Mat points3d;
    
    cv::depthTo3d(cv_ptr->image , K_, points3d);

    cv::Mat channels[3];
    cv::split(points3d, channels);

    cv::Mat centroids;

    // calculate centroids for all the clusters
    for(size_t i = 0 ; i < clusters.size(); i++)
    {
      int count = 0; 
      double x = 0;
      double y = 0;
      double z = 0;
      for(size_t j = 0; j < clusters[i].size(); j++)
      {

        int m = clusters[i][j] /cv_ptr->image.cols;
        int n = clusters[i][j] % cv_ptr->image.cols;

        if(cv_ptr->image.at<float>(m,n) == FLT_MAX ||  isnan(channels[0].at<float>(m,n))  || isnan(channels[1].at<float>(m,n)) || isnan(channels[2].at<float>(m,n)))
        {} 
        else 
        { 
          count++ ;
          x += channels[0].at<float>(m,n);
          y += channels[1].at<float>(m,n);
          z += channels[2].at<float>(m,n);
        }
      }

      cv::Vec3f centroid_point(x/count, y/count, z/count); 
//      std::cout << i <<"\t" << count <<"\t" << x/count << "\t" << y/count <<"\t" << z/count <<std::endl;
      centroids.push_back(centroid_point); 
    }

    // the expected gripper centroid transformed to the camera frame
    tf::Stamped<tf::Point> gripper_centroid;
    gripper_centroid.setX(0.06);
    gripper_centroid.setY(0.00);
    gripper_centroid.setZ(0.035);
    gripper_centroid.frame_id_ = "/wrist_roll_link";
    tf::Stamped<tf::Point> gripper_centroid_transformed;
    tf::TransformListener listener;
    try
    {
      listener.waitForTransform( msg->header.frame_id, gripper_centroid.frame_id_,
          ros::Time(0), ros::Duration(3.0)); 
      listener.transformPoint( msg->header.frame_id, 
          ros::Time(0),  gripper_centroid , gripper_centroid.frame_id_, gripper_centroid_transformed);
    }
    catch (tf::TransformException ex)
    {
      ROS_ERROR("%s",ex.what());
      ros::Duration(1.0).sleep();
    }
    cv::Vec3f gripper_centroid_transform;
    gripper_centroid_transform[0] = gripper_centroid_transformed.x();
    gripper_centroid_transform[1] = gripper_centroid_transformed.y();
    gripper_centroid_transform[2] = gripper_centroid_transformed.z();

  cv::Vec3f point_on_plane_1( 0.06, 0 , 0.035);
  cv::Vec3f point_on_plane_2( 0.04, -0.01, 0.035);
  cv::Vec3f point_on_plane_3( 0.07, 0.01, 0.035);
 
  cv::Mat points_on_plane;
  points_on_plane.push_back(point_on_plane_1);
  points_on_plane.push_back(point_on_plane_2);
  points_on_plane.push_back(point_on_plane_3);
  
  // transform them to the base frame
  cv::Mat points_on_plane_transformed;
  tf::StampedTransform transform;

  try
  {
    for (size_t i = 0; i < points_on_plane.rows; i++)
    { 
      tf::Stamped<tf::Point> point;
      point.frame_id_ = "/wrist_roll_link";//msg->header.frame_id;
      point.setX( points_on_plane.at<cv::Vec3f>(i,0)[0]);
      point.setY( points_on_plane.at<cv::Vec3f>(i,0)[1]); 
      point.setZ( points_on_plane.at<cv::Vec3f>(i,0)[2]);  
      tf::Stamped<tf::Point> point_transformed;
      listener.waitForTransform( msg->header.frame_id, point.frame_id_,// "/base_link", "/head_camera_depth_optical_frame",
                              ros::Time(0), ros::Duration(3.0));
   //listener.transformPoint("base_link", point , point_transformed);
      listener.transformPoint( msg->header.frame_id,
          ros::Time(0),  point , point.frame_id_, point_transformed);

      cv::Vec3f point_transform;
      point_transform[0] = point_transformed.x();
      point_transform[1] = point_transformed.y();
      point_transform[2] = point_transformed.z();

      points_on_plane_transformed.push_back(point_transform);
    }
  }
  catch(tf::TransformException &ex) 
  {
    ROS_ERROR("%s",ex.what());
    ros::Duration(1.0).sleep();
  }

  //find equation of the plane in the base frame
  cv::Vec3f V0 = points_on_plane_transformed.at<cv::Vec3f>(0,0);
  cv::Vec3f V1 = points_on_plane_transformed.at<cv::Vec3f>(1,0);
  cv::Vec3f V2 = points_on_plane_transformed.at<cv::Vec3f>(2,0);

  cv::Vec3f normal = (V2-V0).cross(V1-V0);
  cv::Vec4f plane_transformed;
  float distance = sqrt (normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
  plane_transformed[0] = normal[0] / distance;
  plane_transformed[1] = normal[1] / distance;
  plane_transformed[2] = normal[2] /distance;
  plane_transformed[3] = - V0.dot(normal/distance);

std::cout << plane_transformed[0] << "\t" << plane_transformed[1] << "\t" << plane_transformed[2] << "\t" << plane_transformed[3] << std::endl;
    // find the closest cluster centroid to the gripper centroid
    float min_distance = 1000;
    int closest_centroid= 1000;
    
    for( int i = 0; i < centroids.rows; i++)
    {
      float distance = pow((gripper_centroid_transform[0]-centroids.at<cv::Vec3f>(i,0)[0]) ,2) +
            pow((gripper_centroid_transform[1]-centroids.at<cv::Vec3f>(i,0)[1]) ,2) + 
            pow((gripper_centroid_transform[2]-centroids.at<cv::Vec3f>(i,0)[2]) ,2);
      
      distance = sqrt(distance);
      if (distance < min_distance) 
      {
        min_distance = distance ;
        closest_centroid = i;
      }
    }
    int count = 0;
    float var_x = 0, var_y = 0;
    for (int j = 0; j < clusters[closest_centroid].size(); j++)
    {
    
      int m = clusters[closest_centroid][j] /cv_ptr->image.cols;
      int n = clusters[closest_centroid][j] % cv_ptr->image.cols;

      if(cv_ptr->image.at<float>(m,n) == FLT_MAX ||  isnan(channels[0].at<float>(m,n))  || isnan(channels[1].at<float>(m,n)) || isnan(channels[2].at<float>(m,n)))
      {} 
      else 
      { 
        if(fabsf(channels[0].at<float>(m,n) - centroids.at<cv::Vec3f>(closest_centroid,0)[0]) > var_x)
        {
          var_x = fabsf(channels[0].at<float>(m,n) - centroids.at<cv::Vec3f>(closest_centroid,0)[0]);
        } 

        if(fabsf(channels[1].at<float>(m,n) - centroids.at<cv::Vec3f>(closest_centroid,0)[1]) > var_y)
        {
          var_y = fabsf(channels[1].at<float>(m,n) - centroids.at<cv::Vec3f>(closest_centroid,0)[1]);
        }
      }
    }
    std::cout << var_x <<"\t" << var_y << std::endl;

    if (var_x > 0.09 || var_y > 0.09)
    { 
      std::cout << "invalid cluster" << std::endl;
    } 

    //  std::cout << centroids.at<cv::Vec3f>(closest_centroid,0)[0] << "\t" << centroids.at<cv::Vec3f>(closest_centroid,0)[1] << "\t" << centroids.at<cv::Vec3f>(closest_centroid,0)[2] << std::endl;
    //  std::cout << gripper_centroid_transform[0] <<"\t" << gripper_centroid_transform[1] << "\t" << gripper_centroid_transform[2] << std::endl;

    cv::Scalar color = cv::Scalar( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );
    cv::Mat gripper_cluster = cv::Mat::zeros( cv_ptr->image.size(), CV_8UC3);
    cv::Mat gripper_cluster_ = cv::Mat::zeros( cv_ptr->image.size(), CV_32FC1);

    cv::Mat some_1 = cv::Mat::zeros( cv_ptr->image.size(), CV_32FC1); 
    cv::Mat some_2 = cv::Mat::zeros( cv_ptr->image.size(), CV_32FC1);
    cv::Mat some_3 = cv::Mat::zeros( cv_ptr->image.size(), CV_32FC1);
    
    //display the closest cluster
    for (int j = 0; j < clusters[closest_centroid].size(); j++)
    {
      int m = clusters[closest_centroid][j] / cv_ptr->image.cols;
      int n = clusters[closest_centroid][j] % cv_ptr->image.cols;
   
      //some[0].push_back(channels[0].at<float>(m,n));
      //some[1].push_back(channels[1].at<float>(m,n));
      //some[2].push_back(channels[2].at<float>(m,n));
      if(!isnan(channels[0].at<float>(m,n)) && !isnan(channels[1].at<float>(m,n)) && !isnan(channels[2].at<float>(m,n)))
      {
        some_1.at<float>(m,n) = channels[0].at<float>(m,n);
        some_2.at<float>(m,n) = channels[1].at<float>(m,n);
        some_3.at<float>(m,n) = channels[2].at<float>(m,n);
      }
      //std::cout << some_3.at<float>(m,n) << std::endl;
      gripper_cluster_.at<float>(m,n) = cv_ptr->image.at<float>(m,n);
      gripper_cluster.at<cv::Vec3b>(m,n)[0] = color[0];
      gripper_cluster.at<cv::Vec3b>(m,n)[1] = color[1];
      gripper_cluster.at<cv::Vec3b>(m,n)[2] = color[2];

    }
  
    cv::Mat some[3];
    for (int j = 0; j < clusters[closest_centroid].size();j++)
    {
      int m = clusters[closest_centroid][j] /cv_ptr->image.cols;
      int n = clusters[closest_centroid][j] % cv_ptr->image.cols;

      if(!isnan(channels[0].at<float>(m,n)) && !isnan(channels[1].at<float>(m,n)) && !isnan(channels[2].at<float>(m,n)))
      {
        some[0].push_back( channels[0].at<float>(m,n));
        some[1].push_back(channels[1].at<float>(m,n));
        some[2].push_back( channels[2].at<float>(m,n));
      }
    } 


    cv::Ptr<cv::RgbdNormals> normals_estimator_;
    cv::Ptr<cv::RgbdPlane> plane_estimator_;

    std::vector<cv::Mat> gripper_points;
    //gripper_points.push_back(some_1);
    //  gripper_points.push_back(some_2);
    //  gripper_points.push_back(some_3);
    gripper_points.push_back(some[0]);
    gripper_points.push_back(some[1]);
    gripper_points.push_back(some[2]);
 
    cv::Mat gripper_points_;
    cv::merge(gripper_points, gripper_points_);
 
    //std::cout << some[0].rows << std::endl;
    //std::cout << gripper_points.cols << std::endl; 
    //std::cout << gripper_cluster.rows <<std::endl;
   /*  if (normals_estimator_.empty())
    {
      normals_estimator_ = new cv::RgbdNormals( some[0].rows, some[0].cols,//cv_ptr->image.rows, cv_ptr->image.cols,//gripper_cluster.rows,
                                             //gripper_cluster.cols,
                                              some[2].depth(), //cv_ptr->image.depth(),
                                             K_);
    }
  cv::Mat normals;
  (*normals_estimator_)(gripper_points_, normals);
*/
/*
    for(int i = 0; i< normals.rows; i++)
    { 
      for(int j =0; j< normals.cols; j++)
      {
        std::cout << normals.at<float>(i,j) << std::endl;
      }
    }
*/
    // Find plane(s)
    if (plane_estimator_.empty())
    {
      plane_estimator_ = cv::Algorithm::create<cv::RgbdPlane>("RGBD.RgbdPlane");
      // Model parameters are based on notes in opencv_candidate
      plane_estimator_->set("sensor_error_a", 0.0);
      plane_estimator_->set("sensor_error_b", 0.0);
      plane_estimator_->set("sensor_error_c", 0.0);
      // Image/cloud height/width must be multiple of block size
      plane_estimator_->set("block_size", 1);
      // Distance a point can be from plane and still be part of it
      plane_estimator_->set("threshold", 0.002);//observations_threshold_);
      // Minimum cluster size to be a plane
      plane_estimator_->set("min_size", some[0].rows/2);
    }
    cv::Mat planes_mask;
    std::vector<cv::Vec4f> plane_coefficients;
    (*plane_estimator_)(gripper_points_,  planes_mask, plane_coefficients);

    std::cout << "number of planes" <<plane_coefficients.size() << std::endl;
  
    for (size_t i = 0; i < plane_coefficients.size(); i++)
    {
      std::cout << plane_coefficients[i][0] << std::endl;
      std::cout << plane_coefficients[i][1] << std::endl;
      std::cout << plane_coefficients[i][2] << std::endl;
      std::cout << plane_coefficients[i][3] << std::endl;
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

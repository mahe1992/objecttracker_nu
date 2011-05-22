// nu_objecttracker.cpp
// Jake Ware and Jarvis Schultz
// Winter 2011

//---------------------------------------------------------------------------
// Notes
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include <iostream>

#include <ros/ros.h>
#include <Eigen/Dense>
#include <tf/transform_broadcaster.h>
#include <geometry_msgs/Point.h>
#include <puppeteer_msgs/PointPlus.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/features/feature.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/features/normal_3d.h>

#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/point_cloud_conversion.h>


//---------------------------------------------------------------------------
// Global Variables
//---------------------------------------------------------------------------

typedef pcl::PointXYZ PointT;

//---------------------------------------------------------------------------
// Objects and Functions
//---------------------------------------------------------------------------

class ObjectTracker {

private:
  ros::NodeHandle n_;
  ros::Subscriber cloud_sub;
  ros::Publisher cloud_pub[2];
  ros::Publisher point_pub;
  float xpos_last;
  float ypos_last;
  float zpos_last;
  bool locate;


public:
  ObjectTracker() {
    cloud_sub = n_.subscribe("/camera/depth/points", 1, &ObjectTracker::cloudcb, this);
    point_pub = n_.advertise<puppeteer_msgs::PointPlus> ("object1_position", 100);
    cloud_pub[0] = n_.advertise<sensor_msgs::PointCloud2> ("object1_cloud", 1);
    cloud_pub[1] = n_.advertise<sensor_msgs::PointCloud2> ("object2_cloud", 1);
  
    xpos_last = 0.0;
    ypos_last = 0.0;
    zpos_last = 0.0;
    locate = true;
  }

  // this function gets called every time new pcl data comes in
  void cloudcb(const sensor_msgs::PointCloud2ConstPtr &scan) {
    sensor_msgs::PointCloud2::Ptr
      object1_cloud (new sensor_msgs::PointCloud2 ()),
      object2_cloud (new sensor_msgs::PointCloud2 ());    

    pcl::PointCloud<pcl::PointXYZ>::Ptr
      cloud (new pcl::PointCloud<pcl::PointXYZ> ()),
      cloud_filtered_x (new pcl::PointCloud<pcl::PointXYZ> ()),
      cloud_filtered_y (new pcl::PointCloud<pcl::PointXYZ> ()),
      cloud_filtered_z (new pcl::PointCloud<pcl::PointXYZ> ());
		    

    // convert pcl2 message to pcl object
    pcl::fromROSMsg(*scan,*cloud);
	
    Eigen::Vector4f centroid;
    float xpos = 0.0;
    float ypos = 0.0;
    float zpos = 0.0;
    float D_sphere = 0.05; //meters
    float R_search = 2.0*D_sphere;

    puppeteer_msgs::PointPlus point;

    // pass through filter
    pcl::PassThrough<pcl::PointXYZ> pass;

    // Now 
    if (locate == true)
      {
	pass.setInputCloud(cloud);
	pass.setFilterFieldName("x");
	pass.setFilterLimits(-2.0, 2.0);
	pass.filter(*cloud_filtered_x);

	pass.setInputCloud(cloud_filtered_x);
	pass.setFilterFieldName("y");
	pass.setFilterLimits(-0.5, 0.98);
	pass.filter(*cloud_filtered_y);

	pass.setInputCloud(cloud_filtered_y);
	pass.setFilterFieldName("z");
	pass.setFilterLimits(.75, 3.0);
	pass.filter(*cloud_filtered_z);

	pcl::compute3DCentroid(*cloud_filtered_z, centroid);
	xpos = centroid(0);
	ypos = centroid(1);
	zpos = centroid(2);

	// Publish cloud:
	pcl::toROSMsg(*cloud_filtered_z, *object2_cloud);
	cloud_pub[1].publish(object2_cloud);
	
	if(cloud_filtered_z->points.size() > 10) {
	  locate = false;  // We have re-found the object!
	  
	  // set values to publish
	  point.x = xpos;
	  point.y = ypos;
	  point.z = zpos;
	  point.error = false;
	  point_pub.publish(point);	    
	}
	else {
	  // set values to publish
	  point.x = 0.0;
	  point.y = 0.0;
	  point.z = 0.0;
	  point.error = true;
	  point_pub.publish(point);	    
	}
      }
    else
      {
	pass.setInputCloud(cloud);
	pass.setFilterFieldName("x");
	pass.setFilterLimits(xpos_last-2.0*R_search, xpos_last+2.0*R_search);
	pass.filter(*cloud_filtered_x);

	pass.setInputCloud(cloud_filtered_x);
	pass.setFilterFieldName("y");
	pass.setFilterLimits(ypos_last-R_search, ypos_last+R_search);
	pass.filter(*cloud_filtered_y);

	pass.setInputCloud(cloud_filtered_y);
	pass.setFilterFieldName("z");
	pass.setFilterLimits(zpos_last-R_search, zpos_last+R_search);
	pass.filter(*cloud_filtered_z);

	if(cloud_filtered_z->points.size() < 10) {
	  locate = true;
	  	  
	  ROS_WARN("Lost Object at: x = %f  y = %f  z = %f\n",xpos_last,ypos_last,zpos_last);
	  
	  point.x = 0.0;
	  point.y = 0.0;
	  point.z = 0.0;
	  point.error = true;
	  point_pub.publish(point);
	  	  
	  return;
	}
	
	pcl::compute3DCentroid(*cloud_filtered_z, centroid);
	xpos = centroid(0);
	ypos = centroid(1);
	zpos = centroid(2);
		
	// Publish cloud:
	pcl::toROSMsg(*cloud_filtered_z, *object1_cloud);
	cloud_pub[0].publish(object1_cloud);

	tf::Transform transform;
	transform.setOrigin(tf::Vector3(xpos, ypos, zpos));
	transform.setRotation(tf::Quaternion(0, 0, 0, 1));

	static tf::TransformBroadcaster br;
	br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "openni_depth_optical_frame", "object1"));

	// set point message values and publish
	point.x = xpos;
	point.y = ypos;
	point.z = zpos;
	point.error = false;

	ros::Time tstamp = ros::Time::now();
	point_pub.publish(point);
      }

    xpos_last = xpos;
    ypos_last = ypos;
    zpos_last = zpos;

   
    /*
    // write point clouds out to file
    pcl::io::savePCDFileASCII ("test1_cloud.pcd", *cloud);
    */
  }
};


//---------------------------------------------------------------------------
// Main
//---------------------------------------------------------------------------

int main(int argc, char **argv)
{
  ros::init(argc, argv, "object_tracker");
  ros::NodeHandle n;

  ROS_INFO("Starting Object Tracker...\n");
  ObjectTracker tracker;
  
  ros::spin();
  
  return 0;
}

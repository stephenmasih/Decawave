#include <cstdio>
#include <iostream>

#include "geometry_msgs/Point.h"
#include "ros/ros.h"

void sendFakeGPS(const geometry_msgs::Point& point)
{
    ROS_INFO("x: %f", point->x);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "fakeGPS");
    ros::NodeHandle n;
    ros::Subscriber sub = n.subscribe("decaPos", 1, sendFakeGPS);
    ros::spin();
    return 0;
}
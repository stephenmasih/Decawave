#include <cstdio>
#include <iostream>
#include <cmath>
#include <thread>
#include <ctime>
#include <cstdbool>
#include <fstream>


#include "ros/ros.h"
#include <std_msgs/String.h>
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PointStamped.h"
#include <ackermann_msgs/AckermannDriveStamped.h>
#include "ros/package.h"

#define WP_RATE 100 //Hz
#define PRINT_RATE 100 //Hz

#define DELTA_DIRECTION  0.01
#define DELTA_SPEED      0.25
#define EPSILON_RADIUS   0.25
#define EPSILON_ANGLE    0.1

bool starl_flag = false;
bool isDriving = false;
bool gotWP = false;

double speed = 0, direction = 0;
geometry_msgs::Point prev_loc, curr_loc;

std::string bot_num, vicon_obj;


ros::Publisher drive_pub;
ros::Publisher reached_pub;

geometry_msgs::Point deca_position, vicon_position;
geometry_msgs::Point current_waypoint;  // VICON coords

std::string dir_path;
char time_buffer[80];
std::thread gps_thread, print_thread;

void getDecaPosition(const geometry_msgs::Point& point)
{
    deca_position = point;
}

void getViconPosition(const geometry_msgs::PoseStamped& pose)
{
    vicon_position = pose.pose.position;
}

double get_angle_between_3_pts(geometry_msgs::Point center, geometry_msgs::Point waypoint, geometry_msgs::Point next_pos)
{
    double n_waypoint[2], n_next_pos[2];
    n_waypoint[0] = waypoint.x - center.x;
    n_waypoint[1] = waypoint.y - center.y;
    n_next_pos[0] = next_pos.x - center.x;
    n_next_pos[1] = next_pos.y - center.y;
    return atan2(n_waypoint[1], n_waypoint[0]) - atan2(n_next_pos[1], n_next_pos[0]);
}

void drive()
{
    ros::Rate r(WP_RATE);

    while(ros::ok())
    {
        
        prev_loc = curr_loc;
        curr_loc = vicon_position;
        double a_error = 0;
        
        // Acknowledge that we reached the desired waypoint
        if (starl_flag)
        {
            if (sqrt(pow(curr_loc.x - current_waypoint.x,2) + pow(curr_loc.y - current_waypoint.y,2)) < 0.3)
                // tell STARL if waypoint is reached
            {
                std_msgs::String wp_reached;
                wp_reached.data = "TRUE";
                starl_flag = false;
                reached_pub.publish(wp_reached);
                
                gotWP = false;
                speed = 0;
                direction = 0;
            }
        }

        //ROS_INFO("x: %f, y: %f, z: %f\n", curr_loc.x, curr_loc.y, curr_loc.z);

        if (gotWP)
        {
            a_error = get_angle_between_3_pts(prev_loc, current_waypoint, curr_loc);
            if (a_error > M_PI) a_error -= 2*M_PI;
            if (a_error < -M_PI) a_error += 2*M_PI;
            
            //d_target = sqrt(pow(curr_loc.x - current_waypoint.x,2) + pow(curr_loc.y - current_waypoint.y,2));
            
            speed += DELTA_SPEED;
            if ( a_error < EPSILON_ANGLE)
            {
                direction -= DELTA_DIRECTION;
            }
            else if (a_error > EPSILON_ANGLE)
            {
                direction += DELTA_DIRECTION;
            }
            
            speed = fmax(fmin(speed, 2), -2);
            direction = fmax(fmin(direction, 0.35), -0.35);
        }
        
        ackermann_msgs::AckermannDriveStamped drive_msg;
        drive_msg.drive.speed = speed;
        drive_msg.drive.steering_angle = direction;
        drive_pub.publish(drive_msg);
        ROS_INFO("speed: %f, steering: %f, a_error: %f", speed, direction, a_error);
        r.sleep();
    }
    
    ackermann_msgs::AckermannDriveStamped drive_msg;
    drive_msg.drive.speed = 0;
    drive_msg.drive.steering_angle = 0;
    drive_pub.publish(drive_msg);
}

void printToFile()
{
    std::ofstream positionFile;
    positionFile.open (dir_path+"/posData_"+time_buffer+".txt", std::ios::app);
    
    // Sleep and don't print anything while we are not flying
    while(ros::ok() && !isDriving)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Once we send the takeoff command, start printing
    ros::Rate printrate(PRINT_RATE);
    ros::Time time_start = ros::Time::now();
    while(ros::ok() && isDriving)
    {
        ros::Duration time_since_start = ros::Time::now() - time_start;
        positionFile << time_since_start.toNSec() / 1000 << ", "; //Print time in useconds
        positionFile << vicon_position.x << ", " << vicon_position.y << ", " << vicon_position.z << ", ";
        positionFile << deca_position.x << ", " << deca_position.y << ", " << deca_position.z << "\r\n";
        
        printrate.sleep();
    }
    positionFile.close();
}

void getWP(const geometry_msgs::PointStamped& stamped_point)
{
    geometry_msgs::Point point = stamped_point.point;
    
    current_waypoint.x = point.x;
    current_waypoint.y = point.y;
    current_waypoint.z = point.z;
    
    gotWP = true;
    starl_flag = true;
    
    if(!isDriving)
    {
        isDriving = true;
    }
}

int main(int argc, char **argv)
{
    current_waypoint.x = current_waypoint.y = current_waypoint.z = 0;
    ros::init(argc, argv, "waypoint");
    ros::NodeHandle n("~");
    
    n.param<std::string>("vicon_obj", vicon_obj, "f1car");
    n.param<std::string>("bot_num", bot_num, "bot0");

    reached_pub = n.advertise<std_msgs::String>("/Reached", 1);
    drive_pub = n.advertise<ackermann_msgs::AckermannDriveStamped>("/ackermann_cmd", 1);

    ros::Subscriber deca_pos = n.subscribe("/decaPos", 1, getDecaPosition);
    ros::Subscriber sub = n.subscribe("/vrpn_client_node/"+vicon_obj+"/pose", 1, getViconPosition);
    ros::Subscriber waypoint = n.subscribe("/Waypoint_"+bot_num, 1, getWP);  // second parameter is num of buffered messages

    dir_path = ros::package::getPath("cyphy_car");
    
    // Gets the current time so we can add to data output
    time_t rawtime;
    time(&rawtime);
    struct tm * timeinfo;
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, 80, "%G%m%dT%H%M%S", timeinfo);

    prev_loc.x = 0;
    prev_loc.y = 0;
    curr_loc.x = 0;
    curr_loc.y = 0;
    
    gps_thread = std::thread(drive);
    //print_thread = std::thread(printToFile);

    ros::spin();
    
    gps_thread.join();
    //print_thread.join();
    
    ackermann_msgs::AckermannDriveStamped drive_msg;
    drive_msg.drive.speed = 0;
    drive_msg.drive.steering_angle = 0;
    drive_pub.publish(drive_msg);
    
    return 0;
}

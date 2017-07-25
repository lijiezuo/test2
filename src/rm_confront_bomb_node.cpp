#define ZBY_PC 1
#define MANIFOLD 2
//#define CURRENT_COMPUTER ZBY_PC
 #define CURRENT_COMPUTER MANIFOLD

/**RC channel define*/
#define RC_F_UP 0
#define RC_F_DOWN 1
#define RC_P_UP 2
#define RC_P_DOWN 3
#define RC_A_UP 4
#define RC_A_DOWN 5

/*uav parameters*/
#define MAX_VELOCITY 0.35

#include <geometry_msgs/Vector3Stamped.h>
#include <ros/assert.h>
#include <ros/ros.h>
#include <std_msgs/Float32.h>
#include "std_msgs/UInt8.h"
#include <sstream>
#include "std_msgs/String.h"
// C++标准库
#include <math.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
// boost asio
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#if CURRENT_COMPUTER == MANIFOLD
#include <dji_sdk/dji_drone.h>
using namespace DJI::onboardSDK;
#endif

using namespace std;
using namespace boost::asio;

#if CURRENT_COMPUTER == MANIFOLD
dji_sdk::RCChannels g_rc_channels;
void rc_channels_callback(const dji_sdk::RCChannels rc_channels);
#endif
void uav_state_callback(const std_msgs::UInt8::ConstPtr &msg);
void vision_base_callback(const std_msgs::String::ConstPtr &msg);
/**timer callback, control uav in a finite state machine*/
void timer_callback(const ros::TimerEvent &evt);

/**
*global variables
*/
int g_RC_channel= RC_P_UP;
int g_current_channel= -1;
/**control variables*/
float g_vx= 0;
float g_vy= 0;
bool g_can_bomb= false;
bool g_discover_base= false;
/**serial port*/
boost::asio::serial_port *g_serial_port;
boost::system::error_code g_err_code;
boost::asio::io_service g_io_service;
/**dji sdk*/
#if CURRENT_COMPUTER == MANIFOLD
DJIDrone *m_drone;
#endif
/**
*global functions
*/
void informGraspperChange(std::string state);
void controlDroneVelocity(float x, float y, float z, float yaw);
void initilizeSerialPort();
void bombBase();

int main(int argc, char **argv)
{
  ros::init(argc, argv, "rm_uav_challenge");
  ros::NodeHandle node;
/*subscriber from dji node*/
#if CURRENT_COMPUTER == MANIFOLD
  ros::Subscriber rc_channels_sub=
      node.subscribe("dji_sdk/rc_channels", 1, rc_channels_callback);
#endif
  ros::Subscriber uav_state_sub=
      node.subscribe("/dji_sdk/flight_status", 1, uav_state_callback);

  ros::Subscriber vision_base_sub=
      node.subscribe("tpp/bomber", 1, vision_base_callback);

  ros::Timer timer= node.createTimer(ros::Duration(1.0 / 50.0), timer_callback);

  initilizeSerialPort();

  m_drone = new DJIDrone(node);

  ros::spin();
  return 0;
}

#if CURRENT_COMPUTER == MANIFOLD
void rc_channels_callback(const dji_sdk::RCChannels rc_channels)
{
  /*receive rc channel here and set flags*/
  g_rc_channels= rc_channels;
  if(fabs(rc_channels.mode - 8000) < 0.000001)
  {
    if(fabs(rc_channels.gear + 10000) < 0.000001)
    {
      g_RC_channel= RC_F_UP;
      ROS_INFO_STREAM("RC channel is: F up");
    }
    else if(fabs(rc_channels.gear + 4545) < 0.000001)
    {
      g_RC_channel= RC_F_DOWN;
      ROS_INFO_STREAM("RC channel is: F down");
    }
  }
  else if(fabs(rc_channels.mode + 8000) < 0.000001)
  {
    if(fabs(rc_channels.gear + 10000) < 0.000001)
    {
      g_RC_channel= RC_P_UP;
      ROS_INFO_STREAM("RC channel is: P up");
    }
    else if(fabs(rc_channels.gear + 4545) < 0.000001)
    {
      g_RC_channel= RC_P_DOWN;
      ROS_INFO_STREAM("RC channel is: P down");
    }
  }
  else if(fabs(rc_channels.mode - 0.0) < 0.000001)
  {
    if(fabs(rc_channels.gear + 10000) < 0.000001)
    {
      g_RC_channel= RC_A_UP;
      ROS_INFO_STREAM("RC channel is: A up");
    }
    else if(fabs(rc_channels.gear + 4545) < 0.000001)
    {
      g_RC_channel= RC_A_DOWN;
      ROS_INFO_STREAM("RC channel is: A down");
    }
  }
}
#endif

void uav_state_callback(const std_msgs::UInt8::ConstPtr &msg)
{
  int flight_status= msg->data;
}

void timer_callback(const ros::TimerEvent &evt)
{
  ROS_INFO_STREAM("loop:");
  /*do different task according to mode*/
  switch(g_RC_channel)
  {
    case RC_P_UP:
    {
      /*release grabber*/
      informGraspperChange("open");
      break;
    }
    case RC_P_DOWN:
    {
      /*close grabber*/
      informGraspperChange("close");
      break;
    }
    case RC_F_UP:
    {
      bombBase();
      break;
    }
    case RC_F_DOWN:
    {
      break;
    }
    case RC_A_UP:
    {
      break;
    }
    case RC_A_DOWN:
    {
      break;
    }
  }
}

void informGraspperChange(std::string state)
{
  if(g_RC_channel == g_current_channel)
    return;
  else if(state == "open")
  {
    for(int i= 0; i < 10; i++)
      boost::asio::write(*g_serial_port, boost::asio::buffer("b"), g_err_code);
    g_current_channel= g_RC_channel;
    ROS_INFO_STREAM("open");
  }
  else if(state == "close")
  {
    for(int i= 0; i < 10; i++)
      boost::asio::write(*g_serial_port, boost::asio::buffer("a"), g_err_code);
    g_current_channel= g_RC_channel;
    ROS_INFO_STREAM("close");
  }
}

void initilizeSerialPort()
{
  g_serial_port= new boost::asio::serial_port(g_io_service);
  g_serial_port->open("/dev/ttyTHS0", g_err_code);
  g_serial_port->set_option(serial_port::baud_rate(9600), g_err_code);
  g_serial_port->set_option(
      serial_port::flow_control(serial_port::flow_control::none), g_err_code);
  g_serial_port->set_option(serial_port::parity(serial_port::parity::none),
                            g_err_code);
  g_serial_port->set_option(serial_port::stop_bits(serial_port::stop_bits::one),
                            g_err_code);
  g_serial_port->set_option(serial_port::character_size(8), g_err_code);
}

void vision_base_callback(const std_msgs::String::ConstPtr &msg)
{
  float vx, vy;
  bool can_bomb, discover_base;
  g_vx=0;
  g_vy=0;
  g_can_bomb=false;
  std::stringstream ss(msg->data.c_str());
  ss >> g_vx >> g_vy >> g_discover_base >> g_can_bomb;
  /*limit the maximum of velocity*/
  g_vx= fabs(g_vx) > MAX_VELOCITY ?
            MAX_VELOCITY * (fabs(g_vx) / (g_vx + 0.000001)) :
            g_vx;
  g_vy= fabs(g_vy) > MAX_VELOCITY ?
            MAX_VELOCITY * (fabs(g_vy) / (g_vy + 0.000001)) :
            g_vy;
}

void controlDroneVelocity(float x, float y, float z, float yaw)
{
#if CURRENT_COMPUTER == MANIFOLD
  m_drone->attitude_control(0x4B, x, y, z, yaw);
#endif
  ros::Duration(20 / 1000).sleep();
}

void bombBase()
{
  controlDroneVelocity(g_vx, g_vy, 0.0, 0.0);
  ROS_INFO_STREAM("velocity is :"<<g_vx<<","<<g_vy);
  if(g_can_bomb)
  {
    informGraspperChange("open");
  }
}

#define ZBY_PC 1
#define MANIFOLD 2
#define CURRENT_COMPUTER ZBY_PC
//  #define CURRENT_COMPUTER MANIFOLD

#define M100_CAMERA 1
#define VIDEO_STREAM 2
#define CURRENT_IMAGE_SOURCE VIDEO_STREAM
//#define CURRENT_IMAGE_SOURCE M100_CAMERA
#define VISABILITY true

/**RC channel define*/
#define RC_F_UP 0
#define RC_F_DOWN 1
#define RC_P_UP 2
#define RC_P_DOWN 3
#define RC_A_UP 4
#define RC_A_DOWN 5

/*uav parameters*/
#define MAX_VELOCITY 0.35
#define PA_CAMERA_DISPLACE 0.16
#define PA_CAMERA_F 507.75

#define PA_LAND_COUNT 1
#define PA_TIME_MIN 1.5
#define PA_TIME_MAX 5.0
#define PA_LAND_HEIGHT 1.05
#define PA_LAND_HEIGHT_FINAL 0.6
#define PA_LAND_HEIGHT_THRESHOLD 0.2
#define PA_LAND_HEIGHT_THRESHOLD_FINAL 0.2
#define PA_LAND_POSITION_THRESHOLD_HIGH 0.3
#define PA_LAND_POSITION_THRESHOLD_LOW 0.15
#define PA_LAND_POSITION_THRESHOLD_SUPER_LOW 0.06
#define PA_LAND_POSITION_THRESHOLD_SUPER_LOW_BIG 0.12
#define PA_V_MIN_HIGH 0.15
#define PA_V_MIN_LOW 0.04
#define PA_V_MIN_FINAL 0.025
#define PA_LAND_Z_VELOCITY_FINAL 0.15
#define PA_LAND_Z_VELOCITY 0.15
#define PA_LAND_TRIANGLE_VELOCITY_HIGH 0.15
#define PA_LAND_TRIANGLE_VELOCITY_LOW 0.07
#define PA_KP_BASE 0.35
#define PA_KP_PILLAR_HIGH 0.3
#define PA_KP_PILLAR_LOW 0.3

/*opencv*/
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
/*ros*/
#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <image_transport/image_transport.h>
#include <ros/assert.h>
#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/image_encodings.h>
#include <std_msgs/Float32.h>
#include <sstream>
#include "std_msgs/String.h"
#include "std_msgs/UInt8.h"
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
void guidance_distance_callback(const sensor_msgs::LaserScan &g_oa);
void vision_base_callback(const std_msgs::String::ConstPtr &msg);
void vision_pillar_callback(const std_msgs::String::ConstPtr &msg);
/**timer callback, control uav in a finite state machine*/
void taskTimerCallback(const ros::TimerEvent &evt);
void ledTimerCallback(const ros::TimerEvent &evt);

/**channel variables*/
int g_RC_channel= RC_P_UP;
int g_current_channel= -1;
/**base control variables*/
float g_base_vx= 0;
float g_base_vy= 0;
bool g_can_bomb= false;
bool g_discover_base= false;
/**pillar control variables*/
bool g_discover_pillar_circle;
bool g_discover_pillar_arc;
float g_circle_position_error[2];
float g_height_from_circle;
float g_arc_position_error[2];
int g_pillar_triangle[4];
float g_height_from_guidance= 1.0;
ros::Time g_checked_time;
enum PREPARE_TO_LAND_TYPE
{
  PREPARE_AT_HIGH,
  PREPARE_AT_LOW,
  PREPARE_AT_SUPER_LOW,
};
PREPARE_TO_LAND_TYPE g_prepare_to_land_type;
/**serial port*/
boost::asio::serial_port *g_serial_port;
boost::system::error_code g_err_code;
boost::asio::io_service g_io_service;
/**dji sdk*/
#if CURRENT_COMPUTER == MANIFOLD
DJIDrone *g_drone;
#endif
bool g_is_sdk_control= false;
/**vision control variables*/
cv::VideoCapture g_cap;
image_transport::Publisher g_image_pub;
sensor_msgs::ImagePtr g_image_ptr;
enum VISION_STATE
{
  VISION_ALL,
  VISION_PILLAR,
  VISION_BASE,
};
VISION_STATE g_vision_state= VISION_ALL;
ros::Publisher g_pillar_task_pub;
ros::Publisher g_base_task_pub;
/**
*global functions
*/
void informGraspperChange(std::string state);
void controlGraspper(std::string state);
void controlDroneVelocity(float x, float y, float z, float yaw);
void initilizeSerialPort();
void bombBase();
void calculateRealPositionError(float error[2]);

void navigateByTriangle(float &x, float &y, float &z);
void navigateByCircle(float &x, float &y, float &z);
void navigateByArc(float &x, float &y, float &z);
void droneGoToPillar();
bool discoverTriangle();
void droneHover();
bool isCheckedTimeSuitable();
void updateCheckedTime();
bool readyToLand();
void droneDropDown();
void droneLand();

void updateLEDColor();

/**vision task control*/
void changePillarTask(std::string state);
void changeBaseTask(std::string state);
void changeVisionTask(VISION_STATE state);
enum LED_COLOR
{
  LED_RED,
  LED_BLUE,
  LED_GREEN,
  LED_WHITE,
};
void changeLEDColor(LED_COLOR color);
void setCircleVariables(bool is_circle_found, float position_error[2],
                        float height);
void setTriangleVariables(int pillar_triangle[4]);
void setArcVariables(bool is_arc_found, float position_error[2]);

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
  ros::Subscriber guidance_distance_sub= node.subscribe(
      "/guidance/obstacle_distance", 1, guidance_distance_callback);
  ros::Subscriber vision_base_sub=
      node.subscribe("tpp/bomber", 1, vision_base_callback);
  ros::Subscriber vision_pillar_sub=
      node.subscribe("tpp/pillar", 1, vision_pillar_callback);

  /*initialize vision task control publisher*/
  g_pillar_task_pub= node.advertise<std_msgs::String>("/tpp/pillar_task", 1);
  g_base_task_pub= node.advertise<std_msgs::String>("/tpp/base_task", 1);
  /*initialize timers*/
  ros::Timer task_timer=
      node.createTimer(ros::Duration(1.0 / 50.0), taskTimerCallback);

  ros::Timer led_timer=
      node.createTimer(ros::Duration(1.0 / 5.0), ledTimerCallback);
      
  initilizeSerialPort();

#if CURRENT_COMPUTER == MANIFOLD
  g_drone= new DJIDrone(node);
#endif

  /*main loop begin */
  ros::spin();

  /*release resource*/
  g_cap.release();
  delete g_serial_port;
#if CURRENT_COMPUTER == MANIFOLD
  g_drone->release_sdk_permission_control();
  delete g_drone;
#endif
  return 0;
}

void ledTimerCallback(const ros::TimerEvent &evt)
{
  updateLEDColor();
  ROS_INFO_STREAM("update color");
}

void taskTimerCallback(const ros::TimerEvent &evt)
{
  ROS_INFO_STREAM("loop:");

  /*do different task according to mode*/
  switch(g_RC_channel)
  {
    case RC_P_UP:
    {
      /*close grabber*/
      informGraspperChange("close");
      break;
    }
    case RC_P_DOWN:
    {
      /*release grabber*/
      informGraspperChange("open");
      break;
    }
    case RC_F_UP:
    {
      if(!readyToLand())
      {
        droneGoToPillar();
      }
      else
      {
        controlGraspper("open");
        droneDropDown();
        droneLand();
      }
      break;
    }
    case RC_F_DOWN:
    {
      bombBase();
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

#if CURRENT_COMPUTER == MANIFOLD
void rc_channels_callback(const dji_sdk::RCChannels rc_channels)
{
  /*receive rc channel here and set flags*/
  g_rc_channels= rc_channels;
  if(fabs(rc_channels.mode - 8000) < 0.000001)
  {
    /*get sdk control*/
    if(!g_is_sdk_control)
    {
#if CURRENT_COMPUTER == MANIFOLD
      g_drone->request_sdk_permission_control();
#endif
      g_is_sdk_control= true;
    }

    if(fabs(rc_channels.gear + 10000) < 0.000001)
    {
      g_RC_channel= RC_F_UP;
      ROS_INFO_STREAM("RC channel is: F up");
      if(g_vision_state != VISION_PILLAR)
        changeVisionTask(VISION_PILLAR);
    }
    else if(fabs(rc_channels.gear + 4545) < 0.000001)
    {
      g_RC_channel= RC_F_DOWN;
      ROS_INFO_STREAM("RC channel is: F down");
      if(g_vision_state != VISION_BASE)
        changeVisionTask(VISION_BASE);
    }
  }
  else
  {
    g_is_sdk_control= false;
    changeVisionTask(VISION_ALL);
    if(fabs(rc_channels.mode + 8000) < 0.000001)
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
}
#endif

void uav_state_callback(const std_msgs::UInt8::ConstPtr &msg)
{
  int flight_status= msg->data;
}

/**this should be used in P mode when informing change of channels*/
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

void controlGraspper(std::string state)
{
  if(state == "open")
  {
    for(int i= 0; i < 10; i++)
      boost::asio::write(*g_serial_port, boost::asio::buffer("b"), g_err_code);
    g_current_channel= g_RC_channel;
    ROS_INFO_STREAM("open graspper");
  }
  else if(state == "close")
  {
    for(int i= 0; i < 10; i++)
      boost::asio::write(*g_serial_port, boost::asio::buffer("a"), g_err_code);
    g_current_channel= g_RC_channel;
    ROS_INFO_STREAM("close graspper");
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
  g_base_vx= 0;
  g_base_vy= 0;
  g_can_bomb= false;
  std::stringstream ss(msg->data.c_str());
  ss >> g_base_vx >> g_base_vy >> g_can_bomb >> g_discover_base;
  /*limit the maximum of velocity*/
  g_base_vx= fabs(g_base_vx) > MAX_VELOCITY ?
                 MAX_VELOCITY * (fabs(g_base_vx) / (g_base_vx + 0.000001)) :
                 g_base_vx;
  g_base_vy= fabs(g_base_vy) > MAX_VELOCITY ?
                 MAX_VELOCITY * (fabs(g_base_vy) / (g_base_vy + 0.000001)) :
                 g_base_vy;
}

void controlDroneVelocity(float x, float y, float z, float yaw)
{
#if CURRENT_COMPUTER == MANIFOLD
  g_drone->attitude_control(0x4B, x, y, z, yaw);
#endif
  ros::Duration(20 / 1000).sleep();
}

void bombBase()
{
  controlDroneVelocity(g_base_vx, g_base_vy, 0.0, 0.0);
  ROS_INFO_STREAM("velocity is :" << g_base_vx << "," << g_base_vy << ","
                                  << g_can_bomb);
  if(g_can_bomb)
  {
    // informGraspperChange("open");
    controlGraspper("open");
  }
}

void changeLEDColor(LED_COLOR color)
{
  switch(color)
  {
    case LED_RED:
    {
      for(int i= 0; i < 10; i++)
        boost::asio::write(*g_serial_port, boost::asio::buffer("c"),
                           g_err_code);
      break;
    }
    case LED_BLUE:
    {
      for(int i= 0; i < 10; i++)
        boost::asio::write(*g_serial_port, boost::asio::buffer("d"),
                           g_err_code);
      break;
    }
    case LED_GREEN:
    {
      for(int i= 0; i < 10; i++)
        boost::asio::write(*g_serial_port, boost::asio::buffer("e"),
                           g_err_code);
      break;
    }
    case LED_WHITE:
    {
      for(int i= 0; i < 10; i++)
        boost::asio::write(*g_serial_port, boost::asio::buffer("h"),
                           g_err_code);
      break;
    }
  }
}

void vision_pillar_callback(const std_msgs::String::ConstPtr &msg)
{
  float h;
  float circle_pos[2];
  float arc_pos[2];
  int tri[4];
  bool circle_found, arc_found;
  // ROS_INFO_STREAM(msg->data);
  std::stringstream ss(msg->data.c_str());
  ss >> tri[0] >> tri[1] >> tri[2] >> tri[3] >> circle_found >> circle_pos[1] >>
      circle_pos[0] >> h >> arc_pos[1] >> arc_pos[0] >> arc_found;
  setCircleVariables(circle_found, circle_pos, h);
  setTriangleVariables(tri);
  // height not used,set to 0
  setArcVariables(arc_found, arc_pos);
}

void setCircleVariables(bool is_circle_found, float position_error[2],
                        float height)
{
  g_discover_pillar_circle= is_circle_found;
  if(is_circle_found)
  {
    g_circle_position_error[0]= position_error[0] - PA_CAMERA_DISPLACE;
    g_circle_position_error[1]= position_error[1];
    g_height_from_circle= height;
  }
  else
  {
    g_circle_position_error[0]= g_circle_position_error[1]=
        g_height_from_circle= 0;
  }
}

void setTriangleVariables(int pillar_triangle[4])
{
  for(int i= 0; i < 4; ++i)
  {
    g_pillar_triangle[i]= pillar_triangle[i];
  }
  // ROS_INFO_STREAM("triangle is:" << pillar_triangle[0] << ","
  //                                << pillar_triangle[1] << ","
  //                                << pillar_triangle[2] << ","
  //                                << pillar_triangle[3]);
}

void setArcVariables(bool is_arc_found, float position_error[2])
{
  /*transform pixel position error to metric error*/
  calculateRealPositionError(position_error);

  g_arc_position_error[0]= position_error[0] - PA_CAMERA_DISPLACE;
  g_arc_position_error[1]= position_error[1];
  g_discover_pillar_arc= is_arc_found;
}

void calculateRealPositionError(float error[2])
{
  float z= g_height_from_guidance;
  float x= error[0], y= error[1];

  /*pin hole camera model*/
  error[0]= z * x / PA_CAMERA_F;
  error[1]= z * y / PA_CAMERA_F;
}

void guidance_distance_callback(const sensor_msgs::LaserScan &g_oa)
{
  ROS_INFO("frame_id: %s stamp: %d\n", g_oa.header.frame_id.c_str(),
           g_oa.header.stamp.sec);
  ROS_INFO("obstacle distance: [%f %f %f %f %f]\n", g_oa.ranges[0],
           g_oa.ranges[1], g_oa.ranges[2], g_oa.ranges[3], g_oa.ranges[4]);
  g_height_from_guidance= g_oa.ranges[0];
}

void droneGoToPillar()
{
  float vx= 0.0, vy= 0.0, vz= 0.0, yaw= 0.0;
  if(g_prepare_to_land_type == PREPARE_AT_SUPER_LOW)
  {
    navigateByArc(vx, vy, vz);
  }
  else if(g_discover_pillar_circle)
  {
    navigateByCircle(vx, vy, vz);
  }
  else if(discoverTriangle())
  {
    navigateByTriangle(vx, vy, vz);
    // ROS_INFO_STREAM("navigate by triangle");
  }
  else
  {
    // ROS_INFO_STREAM("Miss pillar!!!");
  }
  ROS_INFO_STREAM("landing v at pillar are:" << vx << "," << vy << "," << vz);
  controlDroneVelocity(vx, vy, vz, yaw);
}

void navigateByArc(float &vx, float &vy, float &vz)
{
  ROS_INFO_STREAM("navigate at super low");
  float height_error= PA_LAND_HEIGHT_FINAL - g_height_from_guidance;
  float pos_error=
      sqrt(pow(g_arc_position_error[0], 2) + pow(g_arc_position_error[1], 2));
  float pos_error_x= g_arc_position_error[0];
  float pos_error_y= g_arc_position_error[1];
  if(fabs(pos_error_x) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW_BIG ||
     fabs(pos_error_y) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW_BIG)
  {
    vz= 0;
    vx= fabs(pos_error_x) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW_BIG ?
            PA_KP_PILLAR_LOW * g_arc_position_error[0] :
            0;
    vy= fabs(pos_error_y) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW_BIG ?
            PA_KP_PILLAR_LOW * g_arc_position_error[1] :
            0;
  }
  else if(fabs(height_error) > PA_LAND_HEIGHT_THRESHOLD_FINAL)
  {
    /*height error too big, use height from guidance to navigate*/
    vz= fabs(height_error) / (height_error + 0.0000000001) *
        PA_LAND_Z_VELOCITY_FINAL;
    vx= fabs(pos_error_x) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW ?
            PA_KP_PILLAR_LOW * g_arc_position_error[0] :
            0;
    vy= fabs(pos_error_y) > PA_LAND_POSITION_THRESHOLD_SUPER_LOW ?
            PA_KP_PILLAR_LOW * g_arc_position_error[1] :
            0;
  }
  else
  {
    vz= 0;
    vx= PA_V_MIN_FINAL * fabs(g_arc_position_error[0]) /
        (g_arc_position_error[0] + 0.00000001);
    vy= PA_V_MIN_FINAL * fabs(g_arc_position_error[1]) /
        (g_arc_position_error[1] + 0.00000001);
  }
}

void navigateByTriangle(float &x, float &y, float &z)
{
  int triangle_sum= g_pillar_triangle[0] + g_pillar_triangle[1] +
                    g_pillar_triangle[2] + g_pillar_triangle[3];
  x= y= z= 0.0;
  float triangle_velocity= 0.0;

  /*use different velocity at different height*/
  if(g_prepare_to_land_type == PREPARE_AT_HIGH)
  {
    triangle_velocity= PA_LAND_TRIANGLE_VELOCITY_HIGH;
  }
  else if(g_prepare_to_land_type == PREPARE_AT_LOW)
  {
    triangle_velocity= PA_LAND_TRIANGLE_VELOCITY_LOW;
  }

  if(triangle_sum == 1)
  {
    if(g_pillar_triangle[0] == 1)
    {
      y= -triangle_velocity;
    }
    else if(g_pillar_triangle[1] == 1)
    {
      x= -triangle_velocity;
    }
    else if(g_pillar_triangle[2] == 1)
    {
      y= triangle_velocity;
    }
    else if(g_pillar_triangle[3] == 1)
    {
      x= triangle_velocity;
    }
  }
  else if(triangle_sum == 2)
  {
    if(g_pillar_triangle[0] == 1)
    {
      y= -triangle_velocity;
    }
    else if(g_pillar_triangle[2] == 1)
    {
      y= triangle_velocity;
    }
    if(g_pillar_triangle[1] == 1)
    {
      x= -triangle_velocity;
    }
    else if(g_pillar_triangle[3] == 1)
    {
      x= triangle_velocity;
    }
  }
  else if(triangle_sum == 3)
  {
    if(g_pillar_triangle[0] == 0)
    {
      y= triangle_velocity;
    }
    else if(g_pillar_triangle[1] == 0)
    {
      x= triangle_velocity;
    }
    else if(g_pillar_triangle[2] == 0)
    {
      y= -triangle_velocity;
    }
    else if(g_pillar_triangle[3] == 0)
    {
      x= -triangle_velocity;
    }
  }
}

void navigateByCircle(float &vx, float &vy, float &vz)
{
  ROS_INFO_STREAM("navigate by circle");
  if(g_prepare_to_land_type == PREPARE_AT_HIGH)
  {
    ROS_INFO_STREAM("navigate high");
    float land_err= sqrt(pow(g_circle_position_error[0], 2) +
                         pow(g_circle_position_error[1], 2));
    if(land_err > PA_LAND_POSITION_THRESHOLD_HIGH)
    {
      vx= PA_KP_PILLAR_HIGH * g_circle_position_error[0];
      vy= PA_KP_PILLAR_HIGH * g_circle_position_error[1];
      vz= 0;
      if(fabs(vx) < PA_V_MIN_HIGH)
        vx= fabs(vx) / (vx + 0.0001) * PA_V_MIN_HIGH;
      if(fabs(vy) < PA_V_MIN_HIGH)
        vy= fabs(vy) / (vy + 0.0001) * PA_V_MIN_HIGH;
    }
    else
    {
      vx= vy= 0.0;
      float height_error= PA_LAND_HEIGHT - g_height_from_circle;
      if(fabs(height_error) > PA_LAND_HEIGHT_THRESHOLD)
      {
        vz= fabs(height_error) / (height_error + 0.0000000001) *
            PA_LAND_Z_VELOCITY;
      }
      else
      {
        /*shift to PREPARE_AT_LOW when height and position
        error are small enough*/
        vz= 0.0;
        droneHover();
        g_prepare_to_land_type= PREPARE_AT_LOW;
      }
    }
  }
  else if(g_prepare_to_land_type == PREPARE_AT_LOW)
  {
    /*only use circle position error to adjust position*/
    ROS_INFO_STREAM("navigate at low");
    vx= PA_KP_PILLAR_LOW * g_circle_position_error[0];
    vy= PA_KP_PILLAR_LOW * g_circle_position_error[1];
    vz= 0;
    if(fabs(vx) < PA_V_MIN_LOW)
      vx= fabs(vx) / (vx + 0.0001) * PA_V_MIN_LOW;
    if(fabs(vy) < PA_V_MIN_LOW)
      vy= fabs(vy) / (vy + 0.0001) * PA_V_MIN_LOW;

    /*shift to PREPARE_AT_SUPER_LOW when position error
    is small enough */
    float land_err= sqrt(pow(g_circle_position_error[0], 2) +
                         pow(g_circle_position_error[1], 2));
    if(land_err < PA_LAND_POSITION_THRESHOLD_LOW)
    {
      g_prepare_to_land_type= PREPARE_AT_SUPER_LOW;
    }
  }
}

bool discoverTriangle()
{
  int triangle_num= g_pillar_triangle[0] + g_pillar_triangle[1] +
                    g_pillar_triangle[2] + g_pillar_triangle[3];
  if(triangle_num != 0)
  {
    // ROS_INFO_STREAM("discover triangle" << triangle_num);
    return true;
  }
  else
  {
    // ROS_INFO_STREAM("no triangle");
    return false;
  }
}

void droneHover()
{
  for(int i= 0; i < 25; i++)
    controlDroneVelocity(0, 0, 0, 0);
  // ros::Duration(0.5).sleep();
}

bool readyToLand()
{
  float height_error= fabs(PA_LAND_HEIGHT_FINAL - g_height_from_guidance);

  float pos_error_x= fabs(g_arc_position_error[0]);
  float pos_error_y= fabs(g_arc_position_error[1]);
  // need output
  if(g_prepare_to_land_type == PREPARE_AT_SUPER_LOW &&
     pos_error_x < PA_LAND_POSITION_THRESHOLD_SUPER_LOW &&
     pos_error_y < PA_LAND_POSITION_THRESHOLD_SUPER_LOW &&
     height_error < PA_LAND_HEIGHT_THRESHOLD_FINAL)
  {
    droneHover();
    if(isCheckedTimeSuitable())
    {
      g_prepare_to_land_type= PREPARE_AT_HIGH;
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    updateCheckedTime();
    return false;
  }
}

bool isCheckedTimeSuitable()
{
  ros::Time now= ros::Time::now();
  double d= now.toSec() - g_checked_time.toSec();
  if(d < PA_TIME_MAX && d > PA_TIME_MIN)
  {
    return true;
  }
  else
  {
    return false;
  }
}

void updateCheckedTime()
{
  g_checked_time= ros::Time::now();
}

void changePillarTask(std::string state)
{
  std_msgs::String msg;
  if(state == "open" || state == "close")
  {
    msg.data= state;
    for(int i= 0; i < 5; i++)
      g_pillar_task_pub.publish(msg);
  }
}

void droneDropDown()
{
  for(int i= 0; i < 200; i++)
    controlDroneVelocity(0.0, 0.0, -0.9, 0.0);
}

void droneLand()
{
#if CURRENT_COMPUTER == MANIFOLD
  g_drone->landing();
#endif
}

void changeBaseTask(std::string state)
{
  std_msgs::String msg;
  if(state == "open" || state == "close")
  {
    msg.data= state;
    for(int i= 0; i < 5; i++)
      g_base_task_pub.publish(msg);
  }
}

void changeVisionTask(VISION_STATE state)
{
  switch(state)
  {
    case VISION_ALL:
    {
      changePillarTask("open");
      changeBaseTask("open");
      g_vision_state= VISION_ALL;
      break;
    }
    case VISION_PILLAR:
    {
      changePillarTask("open");
      changeBaseTask("close");
      g_vision_state= VISION_PILLAR;

      break;
    }
    case VISION_BASE:
    {
      changePillarTask("close");
      changeBaseTask("open");
      g_vision_state= VISION_BASE;
      break;
    }
  }
}

void updateLEDColor()
{
  if(g_discover_pillar_circle || g_discover_pillar_arc)
    changeLEDColor(LED_GREEN);
  else if(discoverTriangle())
    changeLEDColor(LED_BLUE);
  else if(g_discover_base)
    changeLEDColor(LED_WHITE);
  else
    changeLEDColor(LED_RED);
}
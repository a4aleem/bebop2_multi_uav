#include "ros/ros.h"
#include "nav_msgs/Odometry.h"
#include "geometry_msgs/Twist.h"
#include "geometry_msgs/Quaternion.h"
#include "geometry_msgs/PoseStamped.h"
#include "tf/tfMessage.h"
#include "std_msgs/Empty.h"
#include "std_msgs/Bool.h"
#include "geometry_msgs/TransformStamped.h"
#include <dynamic_reconfigure/server.h>
#include <position_controller/PIDConfig.h>
#include <position_controller/msgData.h>
#include <position_controller/msgCoordinates.h>
#include <math.h>
#include <string>
using namespace std;
#define PI 3.14159265358979
struct sCoordinates
{
	double x,y,z,yaw;
};

class PositionController
{
	private:
		string drone_name;
		bool state_flying;
		sCoordinates target_pose, actual_pose, error, error_prev, integral, velocity, prev_vel;
		sCoordinates Kp, Ki, Kd;  // wzmocnienia regulatorow PID
		double max_vel_xy, max_vel_z, max_vel_yaw;  // ograniczenia wyjścia regulatorow
		double error_ctrl_x, error_ctrl_y;  // uchyby po przeliczeniu z katem yaw
		geometry_msgs::Twist cmd;  // velocity command
		geometry_msgs::Quaternion q;
		
	public:	
		PositionController(sCoordinates z, string name): actual_pose(z), error(z), error_prev(z), integral(z), velocity(z), prev_vel(z), drone_name(name)
		{	
			hover = false;
		}
		
		~PositionController();
	
		bool hover;  // jesli true to wylacza sterowanie, wtedy bebop_driver utrzymuje drona w miejscu
		ros::Subscriber odom_sub, target_sub, land_sub, takeoff_sub, reset_sub, hover_sub;
		ros::Publisher vel_pub, data_pub,  odom_conv_pub, state_pub;
		
		void change_state_land(const std_msgs::Empty::ConstPtr& msg);
		void change_state_takeoff(const std_msgs::Empty::ConstPtr& msg);
		void update_pose(const geometry_msgs::PoseStamped::ConstPtr& msg);	
		void update_target(const position_controller::msgCoordinates::ConstPtr& msg);
		void update_hover(const std_msgs::Bool::ConstPtr& msg);
		void control();
		void reconfig_callback(position_controller::PIDConfig &config);
		position_controller::msgData NewData();		
		bool flying();
		void target_after_takeoff(double altitude);
};


PositionController::~PositionController()
{
}

// aktualizacja stanu flying na false po ladowaniu
void PositionController::change_state_land(const std_msgs::Empty::ConstPtr& msg)
{
	std_msgs::Bool s;
	s.data = false;
	state_flying = false;
	state_pub.publish(s);
}
// aktualizacja stanu flying na true po starcie
void PositionController::change_state_takeoff(const std_msgs::Empty::ConstPtr& msg)
{
	std_msgs::Bool s;
	s.data = true;
	state_flying = true;
	state_pub.publish(s);
}
bool PositionController::flying()
{
	return state_flying;
}

// aktualizacja zadanej pozycji i orientacji (dane pobierane z topica target, do którego dane wysyła osobny węzeł - formation_controller)
void PositionController::update_target(const position_controller::msgCoordinates::ConstPtr& msg)
{
	target_pose.x = msg->x;
	target_pose.y = msg->y;
	target_pose.z = msg->z;
	target_pose.yaw = msg->yaw;
}

// funkcja tuż przed startem ustawia pierwszy punkt docelowy, tak aby dron wzniosl sie pionowo w gore na zadana wysokosc
void PositionController::target_after_takeoff(double altitude)
{	
	target_pose.x = actual_pose.x;
	target_pose.y = actual_pose.y;
	target_pose.z = altitude;
	target_pose.yaw = actual_pose.yaw;
}

// aktualizacja danych odometrycznych drona z optitracka
void PositionController::update_pose(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
	position_controller::msgCoordinates conv;
	actual_pose.x = conv.x = msg->pose.position.x;
	actual_pose.y = conv.y = msg->pose.position.z;
	actual_pose.z = conv.z = -msg->pose.position.y;
	q = msg->pose.orientation;
	actual_pose.yaw = -atan2(  (2*(q.w*q.y+q.x*q.z)) , (q.w*q.w - q.x*q.x - q.y*q.y - q.z*q.z) )  * 180 / PI;

	ROS_INFO("Actual pose - x: [%f], y: [%f], z:  [%f], yaw: [%f]\n", actual_pose.x, actual_pose.y, actual_pose.z, actual_pose.yaw);
	odom_conv_pub.publish(conv);
}


// przypisanie zmiennym programowym wartości zmiennych rekonfigurowalnych (zadeklarowanych w pliku PID.cfg)
void PositionController::reconfig_callback(position_controller::PIDConfig &config)
{
	Kp.x = config.Kp_x;
	Ki.x = config.Ki_x;
	Kd.x = config.Kd_x;
	Kp.y = config.Kp_y;
	Ki.y = config.Ki_y;
	Kd.y = config.Kd_y;
	Kp.z = config.Kp_z;
	Ki.z = config.Ki_z;
	Kd.z = config.Kd_z;
	Kp.yaw = config.Kp_yaw;
	Ki.yaw = config.Ki_yaw;
	Kd.yaw = config.Kd_yaw;
	max_vel_xy = config.max_vel_xy / 100.0;
	max_vel_z = config.max_vel_z / 100.0;
	max_vel_yaw = config.max_vel_yaw / 100.0;
}

// przygotowanie danych do wyslania do topica (/position_controller_data) ktory jest rejestrowany do baga na podstawie ktorego powstaja wykresy
position_controller::msgData PositionController::NewData()
{
   position_controller::msgData msgNewData;
	
	msgNewData.target_pos.x = target_pose.x;
	msgNewData.target_pos.y = target_pose.y;
	msgNewData.target_pos.z = target_pose.z;
	msgNewData.target_pos.yaw = target_pose.yaw;
	
	msgNewData.actual_pos.x = actual_pose.x;
    msgNewData.actual_pos.y = actual_pose.y;
	msgNewData.actual_pos.z = actual_pose.z;
	msgNewData.actual_pos.yaw = actual_pose.yaw;
	
	msgNewData.error_pos.x = error.x;
	msgNewData.error_pos.y = error.y;
	msgNewData.error_pos.z = error.z;
	msgNewData.error_pos.yaw = error.yaw;
	
	msgNewData.error_ctrl_x = error_ctrl_x;
	msgNewData.error_ctrl_y = error_ctrl_y;
		
	msgNewData.velocity_cmd.x = velocity.x;
	msgNewData.velocity_cmd.y = velocity.y;
	msgNewData.velocity_cmd.z = velocity.z;
	msgNewData.velocity_cmd.yaw = velocity.yaw;
	
	msgNewData.max_vel = max_vel_xy;
	msgNewData.min_vel = -max_vel_xy;
	
	msgNewData.Kp.x = Kp.x;
	msgNewData.Ki.x = Ki.x;
	msgNewData.Kd.x = Kd.x;
	msgNewData.Kp.y = Kp.y;
	msgNewData.Ki.y = Ki.y;
	msgNewData.Kd.y = Kd.y;
	msgNewData.Kp.z = Kp.z;
	msgNewData.Ki.z = Ki.z;
	msgNewData.Kd.z = Kd.z;
	msgNewData.Kp.yaw = Kp.yaw;
	msgNewData.Ki.yaw = Ki.yaw;
	msgNewData.Kd.yaw = Kd.yaw;
	
	return msgNewData;
}	

// główna funkcja regulacji PID
void PositionController::control()
{
	//------------------------------------------- control errors ----------------------------------------------
	error.x = target_pose.x - actual_pose.x;
	error.y = target_pose.y - actual_pose.y;
	error.z = target_pose.z - actual_pose.z;
	error.yaw = target_pose.yaw - actual_pose.yaw;
	
	// warunki zapewniające obroty we właściwą stronę (najkrótszą drogą):
	if(error.yaw < -180.0)  
		error.yaw = error.yaw + 360.0;
	else if(error.yaw > 180.0)
		error.yaw = error.yaw - 360.0;
	
	//przeliczenie uchybu z układu świata do układu drona
	error_ctrl_x = error.x * cos(actual_pose.yaw*PI/180.0) + error.y * sin(actual_pose.yaw*PI/180.0);
	error_ctrl_y = error.y * cos(actual_pose.yaw*PI/180.0) - error.x * sin(actual_pose.yaw*PI/180.0);
	
	//-------------------------------------------- anty-windup ------------------------------------------------------------
	if( ((prev_vel.x > max_vel_xy) && (error_ctrl_x >0)) || ((prev_vel.x < -max_vel_xy) && (error_ctrl_x <0)) )
		integral.x = 0;
	else
		integral.x += error_ctrl_x;
	
	if( ((prev_vel.y > max_vel_xy) && (error_ctrl_y >0)) || ((prev_vel.y < -max_vel_xy) && (error_ctrl_y <0)) )
		integral.y = 0;
	else
		integral.y += error_ctrl_y;
	
	if( ((prev_vel.z > max_vel_z) && (error.z >0)) || ((prev_vel.z < -max_vel_z) && (error.z <0)) )
		integral.z = 0;
	else
		integral.z += error.z;
	
	if( ((prev_vel.yaw > max_vel_yaw) && (error.yaw >0)) || ((prev_vel.yaw < -max_vel_yaw) && (error.yaw <0)) )
		integral.yaw = 0;
	else
		integral.yaw += error.yaw;
	
	//------------------------------------- calculate outputs -----------------------------------------------------
	velocity.x  =  	  Kp.x * error_ctrl_x   + Ki.x * integral.x 		 + Kd.x * (error_ctrl_x - error_prev.x);
	velocity.y  = 	  Kp.y * error_ctrl_y  + Ki.y * integral.y 		 + Kd.y * (error_ctrl_y - error_prev.y);
	velocity.z  =	  Kp.z * error.z 	  	   + Ki.z * integral.z 		 + Kd.z * (error.z - error_prev.z);
	velocity.yaw = Kp.yaw * error.yaw + Ki.yaw * integral.yaw + Kd.yaw * (error.yaw - error_prev.yaw);
	
	//------------------------------- save last output and error-----------------------------------------------
	prev_vel.x = velocity.x;
	prev_vel.y = velocity.y;
	prev_vel.z = velocity.z;
	prev_vel.yaw = velocity.yaw;
	// ----------------------------------
	error_prev.x = error_ctrl_x;
	error_prev.y = error_ctrl_y;
	error_prev.z = error.z;
	error_prev.yaw = error.yaw;
	
	//------------------------------------------ saturation -----------------------------------------------
	if (velocity.x < -max_vel_xy)
		velocity.x = -max_vel_xy;
	else if (velocity.x > max_vel_xy)
		velocity.x = max_vel_xy;
	if (velocity.y < -max_vel_xy)
		velocity.y = -max_vel_xy;
	else if (velocity.y > max_vel_xy)
		velocity.y = max_vel_xy;
	if (velocity.z < -max_vel_z)
		velocity.z = -max_vel_z;
	else if (velocity.z > max_vel_z)
		velocity.z = max_vel_z;
	// velocity in ,,z" axis is also limited by SpeedSettingsMaxVerticalSpeedCurrent parameter (default 1 m/s)
	if (velocity.yaw < -max_vel_yaw)
		velocity.yaw = -max_vel_yaw;
	else if (velocity.yaw > max_vel_yaw)
		velocity.yaw = max_vel_yaw;
	
	//---------------------------------- prepare command --------------------------------------------
	cmd.linear.x = velocity.x;	
	cmd.linear.y = velocity.y;	
	cmd.linear.z = velocity.z;
	cmd.angular.x = 0.0;
	cmd.angular.y = 0.0;
	cmd.angular.z = velocity.yaw;	
	
	//--------------------------------- publish command -----------------------------------------------
	vel_pub.publish(cmd);
	data_pub.publish(this->NewData()); // send data to topic position_controller_data
}

void PositionController::update_hover(const std_msgs::Bool::ConstPtr& msg)
{
	hover = msg->data;
}

int main(int argc, char **argv)
{	
	ros::init(argc, argv, "position_controller");
	ros::NodeHandle n;
	ros::Rate loop_rate(30);  // in Hz

	sCoordinates zeros = {0,0,0,0};
	double init_altitude = 1.5;  // zadana wysokosc po starcie
	
	PositionController pc_leader = PositionController(zeros, "bebop_leader");
	PositionController pc_follower1 = PositionController(zeros, "bebop_follower1");
	
	//  ---------------  publishers ---------------------------------------------------------------------------
	pc_leader.vel_pub = n.advertise<geometry_msgs::Twist>("/bebop_leader/cmd_vel", 100);
	pc_leader.data_pub = n.advertise<position_controller::msgData>("/position_controller_leader_data", 100);
	pc_leader.state_pub = n.advertise<std_msgs::Bool>("/bebop_leader/state", 100);
	pc_leader.odom_conv_pub = n.advertise<position_controller::msgCoordinates>("/bebop_leader/odom_conv", 100);
	
	pc_follower1.vel_pub = n.advertise<geometry_msgs::Twist>("/bebop_follower1/cmd_vel", 100);
	pc_follower1.data_pub = n.advertise<position_controller::msgData>("/position_controller_follower1_data", 100);
	pc_follower1.state_pub = n.advertise<std_msgs::Bool>("/bebop_follower1/state", 100);
	pc_follower1.odom_conv_pub = n.advertise<position_controller::msgCoordinates>("/bebop_follower1/odom_conv", 100);
	
//  ---------------  subscribers -------------------------------------------------------------------------
	pc_leader.odom_sub = n.subscribe("/optitrack_node/optitrack0", 100, &PositionController::update_pose, &pc_leader);
	pc_leader.target_sub = n.subscribe("/bebop_leader/target", 100, &PositionController::update_target, &pc_leader);
	pc_leader.land_sub = n.subscribe("/bebop_leader/land", 100, &PositionController::change_state_land, &pc_leader);
	pc_leader.reset_sub = n.subscribe("/bebop_leader/reset", 100, &PositionController::change_state_land, &pc_leader);
	pc_leader.takeoff_sub = n.subscribe("/bebop_leader/takeoff", 100, &PositionController::change_state_takeoff, &pc_leader);
	pc_leader.hover_sub = n.subscribe("/bebop_leader/hover", 100, &PositionController::update_hover, &pc_leader);

	pc_follower1.odom_sub = n.subscribe("/optitrack_node/optitrack1", 100, &PositionController::update_pose, &pc_follower1);
	pc_follower1.target_sub = n.subscribe("/bebop_follower1/target", 100, &PositionController::update_target, &pc_follower1);
	pc_follower1.land_sub = n.subscribe("/bebop_follower1/land", 100, &PositionController::change_state_land, &pc_follower1);
	pc_follower1.reset_sub = n.subscribe("/bebop_follower1/reset", 100, &PositionController::change_state_land, &pc_follower1);
	pc_follower1.takeoff_sub = n.subscribe("/bebop_follower1/takeoff", 100, &PositionController::change_state_takeoff, &pc_follower1);
	pc_follower1.hover_sub = n.subscribe("/bebop_follower1/hover", 100, &PositionController::update_hover, &pc_follower1);


	dynamic_reconfigure::Server<position_controller::PIDConfig> server, server2;
	dynamic_reconfigure::Server<position_controller::PIDConfig>::CallbackType f, f2;
	f = boost::bind(&PositionController::reconfig_callback, boost::ref(pc_leader), _1);
	f2 = boost::bind(&PositionController::reconfig_callback, boost::ref(pc_follower1), _1);
	server.setCallback(f);
	server2.setCallback(f2);
	
	pc_leader.target_after_takeoff(init_altitude);
	pc_follower1.target_after_takeoff(init_altitude);
	
	while (ros::ok())
	{
		if(pc_leader.flying() && !pc_leader.hover )
			pc_leader.control();
		if(pc_follower1.flying() && !pc_follower1.hover)
			pc_follower1.control();
		ros::spinOnce();
		loop_rate.sleep();
	}
	return 0;
}

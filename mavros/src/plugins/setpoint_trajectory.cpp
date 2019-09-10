/**
 * @brief SetpointTRAJECTORY plugin
 * @file setpoint_trajectory.cpp
 * @author Jaeyoung Lim <jaeyoung@auterion.com>
 *
 * @addtogroup plugin
 * @{
 */
/*
 * Copyright 2019 Jaeyoung Lim.
 *
 * This file is part of the mavros package and subject to the license terms
 * in the top-level LICENSE file of the mavros repository.
 * https://github.com/mavlink/mavros/tree/master/LICENSE.md
 */

#include <mavros/mavros_plugin.h>
#include <mavros/setpoint_mixin.h>
#include <eigen_conversions/eigen_msg.h>

#include <nav_msgs/Path.h>
#include <mavros_msgs/PositionTarget.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>

namespace mavros {
namespace std_plugins {
/**
 * @brief Setpoint TRAJECTORY plugin
 *
 * Receive trajectory setpoints and send setpoint_raw setpoints along the trajectory.
 */
class SetpointTrajectoryPlugin : public plugin::PluginBase,
	private plugin::SetPositionTargetLocalNEDMixin<SetpointTrajectoryPlugin> {
public:
	SetpointTrajectoryPlugin() : PluginBase(),
		sp_nh("~setpoint_trajectory"),
		TRAJ_SAMPLING_DT(TRAJ_SAMPLING_MS / 1000.0)
	{ }

	void initialize(UAS &uas_)
	{
		PluginBase::initialize(uas_);

		sp_nh.param<std::string>("frame_id", frame_id, "map");

		local_sub = sp_nh.subscribe("local", 10, &SetpointTrajectoryPlugin::local_cb, this);
		desired_pub = sp_nh.advertise<nav_msgs::Path>("desired", 10);

		sp_timer = sp_nh.createTimer(ros::Duration(0.01), &SetpointTrajectoryPlugin::reference_cb, this);
	}

	Subscriptions get_subscriptions()
	{
		return { /* Rx disabled */ };
	}

private:
	friend class SetPositionTargetLocalNEDMixin;
	ros::NodeHandle sp_nh;

	ros::Timer sp_timer;
	ros::Time refstart_time;

	ros::Subscriber local_sub;
	ros::Publisher desired_pub;

	trajectory_msgs::MultiDOFJointTrajectory::ConstPtr trajectory_target_msg;

	std::string frame_id;

	static constexpr int TRAJ_SAMPLING_MS = 100;

	const ros::Duration TRAJ_SAMPLING_DT;

	void publish_path(const trajectory_msgs::MultiDOFJointTrajectory::ConstPtr &req){
		nav_msgs::Path msg;

		msg.header.stamp = ros::Time::now();
		msg.header.frame_id = frame_id;
		for (const auto &p : req->points) {
			if (p.transforms.empty())
				continue;
	
			geometry_msgs::PoseStamped pose_msg;
			pose_msg.pose.position.x = p.transforms[0].translation.x;
			pose_msg.pose.position.y = p.transforms[0].translation.y;
			pose_msg.pose.position.z = p.transforms[0].translation.z;
			pose_msg.pose.orientation = p.transforms[0].rotation;
			msg.poses.emplace_back(pose_msg);
		}
		desired_pub.publish(msg);
	}

	/* -*- callbacks -*- */

	void local_cb(const trajectory_msgs::MultiDOFJointTrajectory::ConstPtr &req)
	{
		trajectory_target_msg = req;
		refstart_time = ros::Time::now();
		publish_path(req);
	}

	void reference_cb(const ros::TimerEvent &event)
	{
		if(!trajectory_target_msg)
			return;

		ros::Duration curr_time_from_start;
		curr_time_from_start = ros::Time::now() - refstart_time;
		for(auto &pt : trajectory_target_msg->points) {
			Eigen::Vector3d position, velocity, af;
			Eigen::Quaterniond attitude;
			float yaw, yaw_rate;
			uint16_t type_mask;

			if(pt.time_from_start.toSec() >= curr_time_from_start.toSec() ) { //TODO: Better logic to handle this case?
				if(!pt.transforms.empty()){
					position = ftf::to_eigen(pt.transforms[0].translation);
					tf::quaternionMsgToEigen(pt.transforms[0].rotation, attitude);
				} else {
					type_mask = type_mask | uint16_t(POSITION_TARGET_TYPEMASK::X_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::Y_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::Z_IGNORE);
				}

				if(!pt.velocities.empty()) velocity = ftf::to_eigen(pt.velocities[0].linear);
				else type_mask = type_mask | uint16_t(POSITION_TARGET_TYPEMASK::VX_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::VY_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::VZ_IGNORE);
				
				if(!pt.accelerations.empty()) af = ftf::to_eigen(pt.accelerations[0].linear);
				else type_mask = type_mask | uint16_t(POSITION_TARGET_TYPEMASK::AX_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::AY_IGNORE) | uint16_t(POSITION_TARGET_TYPEMASK::AZ_IGNORE);

				// Transform frame ENU->NED
				position = ftf::transform_frame_enu_ned(position);
				velocity = ftf::transform_frame_enu_ned(velocity);
				af = ftf::transform_frame_enu_ned(af);
				Eigen::Quaterniond q = ftf::transform_orientation_enu_ned(
						ftf::transform_orientation_baselink_aircraft(attitude));
				yaw = ftf::quaternion_get_yaw(q);

				set_position_target_local_ned(
							trajectory_target_msg->header.stamp.toNSec() / 1000000,
							1,
							0,
							position,
							velocity,
							af,
							yaw, 0);
				return;
			}
		}
		trajectory_target_msg.reset(); //End of trajectory
	}
};
}	// namespace std_plugins
}	// namespace mavros

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(mavros::std_plugins::SetpointTrajectoryPlugin, mavros::plugin::PluginBase)

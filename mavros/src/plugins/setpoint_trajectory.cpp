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
	private plugin::SetPositionTargetLocalNEDMixin<SetpointTrajectoryPlugin>,
	private plugin::SetPositionTargetGlobalIntMixin<SetpointTrajectoryPlugin>,
	private plugin::SetAttitudeTargetMixin<SetpointTrajectoryPlugin> {
public:
	SetpointTrajectoryPlugin() : PluginBase(),
		sp_nh("~setpoint_trajectory"),
		TRAJ_SAMPLING_DT(TRAJ_SAMPLING_MS / 1000.0)
	{ }

	void initialize(UAS &uas_)
	{
		PluginBase::initialize(uas_);

		bool tf_listen;

		local_sub = sp_nh.subscribe("local", 10, &SetpointTrajectoryPlugin::local_cb, this);
		target_local_pub = sp_nh.advertise<mavros_msgs::PositionTarget>("target_trajectory", 10);

		sp_timer = sp_nh.createTimer(ros::Duration(0.01), &SetpointTrajectoryPlugin::reference_cb, this);
	}

	Subscriptions get_subscriptions()
	{
		return {
				make_handler(&SetpointTrajectoryPlugin::handle_position_target_local_ned),
		};
	}

private:
	friend class SetPositionTargetLocalNEDMixin;
	friend class SetPositionTargetGlobalIntMixin;
	friend class SetAttitudeTargetMixin;
	ros::NodeHandle sp_nh;

	ros::Timer sp_timer;

	ros::Time refstart_time;

	ros::Subscriber local_sub, global_sub, attitude_sub;
	ros::Publisher target_local_pub, target_global_pub, target_attitude_pub;

	trajectory_msgs::MultiDOFJointTrajectory trajectory_target_msg;

	static constexpr int TRAJ_SAMPLING_MS = 100;

	const ros::Duration TRAJ_SAMPLING_DT;

	/* -*- message handlers -*- */
	void handle_position_target_local_ned(const mavlink::mavlink_message_t *msg, mavlink::common::msg::POSITION_TARGET_LOCAL_NED &tgt)
	{
		// Transform desired position,velocities,and accels from ENU to NED frame
		auto position = ftf::transform_frame_ned_enu(Eigen::Vector3d(tgt.x, tgt.y, tgt.z));
		auto velocity = ftf::transform_frame_ned_enu(Eigen::Vector3d(tgt.vx, tgt.vy, tgt.vz));
		auto af = ftf::transform_frame_ned_enu(Eigen::Vector3d(tgt.afx, tgt.afy, tgt.afz));
		float yaw = ftf::quaternion_get_yaw(
					ftf::transform_orientation_aircraft_baselink(
						ftf::transform_orientation_ned_enu(
							ftf::quaternion_from_rpy(0.0, 0.0, tgt.yaw))));
		Eigen::Vector3d ang_vel_ned(0.0, 0.0, tgt.yaw_rate);
		auto ang_vel_enu = ftf::transform_frame_ned_enu(ang_vel_ned);
		float yaw_rate = ang_vel_enu.z();

		auto target = boost::make_shared<mavros_msgs::PositionTarget>();

		target->header.stamp = m_uas->synchronise_stamp(tgt.time_boot_ms);
		target->coordinate_frame = tgt.coordinate_frame;
		target->type_mask = tgt.type_mask;
		tf::pointEigenToMsg(position, target->position);
		tf::vectorEigenToMsg(velocity, target->velocity);
		tf::vectorEigenToMsg(af, target->acceleration_or_force);
		target->yaw = yaw;
		target->yaw_rate = yaw_rate;

		target_local_pub.publish(target);
	}

	/* -*- callbacks -*- */

	void local_cb(const trajectory_msgs::MultiDOFJointTrajectory& msg)
	{
		trajectory_target_msg = msg;

		//Start trajectory from when trajectory message was received
		//TODO: Should this be the time that is stamped in the trajectory?
		refstart_time = ros::Time::now();
	}

	void reference_cb(const ros::TimerEvent &event)
	{

		ros::Duration curr_time_from_start;
		curr_time_from_start = ros::Time::now() - refstart_time;
	
		for(size_t i = 0; i < trajectory_target_msg.points.size(); i++){

			Eigen::Vector3d position, velocity, af;
			Eigen::Quaterniond attitude;
			float yaw, yaw_rate;
			trajectory_msgs::MultiDOFJointTrajectoryPoint pt = trajectory_target_msg.points[i];
			uint16_t type_mask;
			if(pt.time_from_start.toSec() >= curr_time_from_start.toSec() ) { //TODO: Better logic to handle this case?
				if(!pt.transforms.empty()){
				position << pt.transforms[0].translation.x, pt.transforms[0].translation.y, pt.transforms[0].translation.z;
				attitude = Eigen::Quaterniond(pt.transforms[0].rotation.w, pt.transforms[0].rotation.x, pt.transforms[0].rotation.y, pt.transforms[0].rotation.z);
				} else {
					type_mask = type_mask || mavros_msgs::PositionTarget::IGNORE_PX || mavros_msgs::PositionTarget::IGNORE_PY || mavros_msgs::PositionTarget::IGNORE_PZ;
				}

				if(!pt.velocities.empty()) velocity << pt.velocities[0].linear.x, pt.velocities[0].linear.y, pt.velocities[0].linear.z;
				else type_mask = type_mask || mavros_msgs::PositionTarget::IGNORE_VX || mavros_msgs::PositionTarget::IGNORE_VY || mavros_msgs::PositionTarget::IGNORE_VZ;
				
				if(!pt.accelerations.empty()) af << pt.accelerations[0].linear.x, pt.accelerations[0].linear.y, pt.accelerations[0].linear.z;
				else type_mask = type_mask || mavros_msgs::PositionTarget::IGNORE_VX || mavros_msgs::PositionTarget::IGNORE_VY || mavros_msgs::PositionTarget::IGNORE_VZ;

				// Transform frame ENU->NED
				position = ftf::transform_frame_enu_ned(position);
				velocity = ftf::transform_frame_enu_ned(velocity);
				af = ftf::transform_frame_enu_ned(af);
				yaw = ftf::quaternion_get_yaw(attitude);

				set_position_target_local_ned(
							trajectory_target_msg.header.stamp.toNSec() / 1000000,
							1,
							0,
							position,
							velocity,
							af,
							yaw, 0);
				break;
			}
		}
	}
};
}	// namespace std_plugins
}	// namespace mavros

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(mavros::std_plugins::SetpointTrajectoryPlugin, mavros::plugin::PluginBase)

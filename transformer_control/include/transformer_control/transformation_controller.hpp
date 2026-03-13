#ifndef TRANSFORMATION_CONTROLLER_HPP_
#define TRANSFORMATION_CONTROLLER_HPP_

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "trajectory_msgs/msg/joint_trajectory.hpp"
#include "control_msgs/action/joint_trajectory.hpp"
#include "transformer_control/msg/transformation_status.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"

namespace transformer_control
{

class TransformationController : public rclcpp::Node
{
public:
  explicit TransformationController(const rclcpp::NodeOptions & options);
  ~TransformationController();

private:
  // Form joint definitions
  struct FormJointConfig
  {
    std::vector<std::string> joint_names;
    std::map<std::string, double> target_positions;
    // For wheels in vehicular form, we use velocity control
    bool uses_velocity_control;
  };

  // ROS entities
  rclcpp::Subscription<transformer_control::msg::TransformationStatus>::SharedPtr status_sub_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_state_sub_;
  
  // Action clients for trajectory execution
  rclcpp_action::Client<control_msgs::action::JointTrajectory>::SharedPtr humanoid_trajectory_client_;
  rclcpp_action::Client<control_msgs::action::JointTrajectory>::SharedPtr arm_head_trajectory_client_;
  rclcpp_action::Client<control_msgs::action::JointTrajectory>::SharedPtr wheel_velocity_client_;

  // Controller management clients
  rclcpp::Node::SharedPtr controller_manager_node_;
  rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedPtr controller_manager_client_;
  rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr controller_state_client_;

  // Parameters
  std::string humanoid_controller_name_;
  std::string arm_head_controller_name_;
  std::string wheel_controller_name_;
  std::string controller_manager_name_;
  double transformation_duration_;
  double spline_resolution_;
  double collision_check_threshold_;
  std::vector<std::string> humanoid_joints_;
  std::vector<std::string> vehicular_position_joints_;
  std::vector<std::string> wheel_joints_;

  // State
  std::atomic<bool> transformation_active_;
  std::string current_form_;
  std::string target_form_;
  std::mutex joint_state_mutex_;
  sensor_msgs::msg::JointState::SharedPtr current_joint_state_;

  // Form configurations
  FormJointConfig humanoid_config_;
  FormJointConfig vehicular_config_;

  // Callbacks
  void status_callback(
    const transformer_control::msg::TransformationStatus::SharedPtr msg);
  void joint_state_callback(
    const sensor_msgs::msg::JointState::SharedPtr msg);
  
  // Trajectory generation
  trajectory_msgs::msg::JointTrajectory generate_spline_trajectory(
    const std::vector<std::string>& joint_names,
    const std::map<std::string, double>& start_positions,
    const std::map<std::string, double>& target_positions,
    double duration);
  
  // Generate waypoints using cubic spline interpolation
  std::vector<trajectory_msgs::msg::JointTrajectoryPoint> generate_spline_waypoints(
    const std::vector<std::string>& joint_names,
    const std::map<std::string, double>& start_positions,
    const std::map<std::string, double>& target_positions,
    double duration);
  
  // Collision avoidance
  bool check_collision_risk(
    const std::map<std::string, double>& positions,
    const std::string& form);
  bool check_joint_limits(
    const std::map<std::string, double>& positions,
    const std::vector<std::string>& joint_names);
  bool check_self_collision(
    const std::map<std::string, double>& positions,
    const std::string& form);

  // Trajectory execution
  void execute_transformation(const std::string& target_form);
  bool send_trajectory_goal(
    const std::string& controller_name,
    const trajectory_msgs::msg::JointTrajectory& trajectory);
  void handle_trajectory_result(
    const rclcpp_action::ClientGoalHandle<control_msgs::action::JointTrajectory>::WrappedResult & result);
  
  // Helper functions
  std::map<std::string, double> extract_positions(
    const std::vector<std::string>& joint_names) const;
  void publish_transformation_status(
    const std::string& state,
    double progress,
    bool is_safe);
  
  // Controller management
  bool ensure_controller_active(const std::string& controller_name);
  bool deactivate_controller(const std::string& controller_name);
  
  // Initialize form configurations
  void initialize_humanoid_config();
  void initialize_vehicular_config();
};

}  // namespace transformer_control

#endif  // TRANSFORMATION_CONTROLLER_HPP_

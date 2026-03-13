#include "transformer_control/transformation_controller.hpp"

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

namespace transformer_control
{

TransformationController::TransformationController(const rclcpp::NodeOptions & options)
: Node("transformation_controller", options)
{
  RCLCPP_INFO(this->get_logger(), "Initializing Transformation Controller...");

  // Declare and get parameters
  this->declare_parameter("humanoid_controller_name", "humanoid_trajectory_controller");
  this->declare_parameter("arm_head_controller_name", "arm_head_position_controller");
  this->declare_parameter("wheel_controller_name", "wheel_velocity_controller");
  this->declare_parameter("controller_manager_name", "/controller_manager");
  this->declare_parameter("transformation_duration", 5.0);
  this->declare_parameter("spline_resolution", 0.05);
  this->declare_parameter("collision_check_threshold", 0.1);

  humanoid_controller_name_ = this->get_parameter("humanoid_controller_name").as_string();
  arm_head_controller_name_ = this->get_parameter("arm_head_controller_name").as_string();
  wheel_controller_name_ = this->get_parameter("wheel_controller_name").as_string();
  controller_manager_name_ = this->get_parameter("controller_manager_name").as_string();
  transformation_duration_ = this->get_parameter("transformation_duration").as_double();
  spline_resolution_ = this->get_parameter("spline_resolution").as_double();
  collision_check_threshold_ = this->get_parameter("collision_check_threshold").as_double();

  // Create controller manager clients
  controller_manager_node_ = rclcpp::Node::make_shared("controller_manager_client");
  controller_manager_client_ = controller_manager_node_->create_client<lifecycle_msgs::srv::ChangeState>(
    controller_manager_name_ + "/change_state");
  controller_state_client_ = controller_manager_node_->create_client<lifecycle_msgs::srv::GetState>(
    controller_manager_name_ + "/get_state");

  // Wait for controller manager services (non-blocking with warning)
  RCLCPP_INFO(this->get_logger(), "Waiting for controller manager services...");
  if (!controller_manager_client_->wait_for_service(5s))
  {
    RCLCPP_WARN(this->get_logger(), "Controller manager change_state service not available");
  }
  if (!controller_state_client_->wait_for_service(5s))
  {
    RCLCPP_WARN(this->get_logger(), "Controller manager get_state service not available");
  }

  // Define joint sets for each form
  // Humanoid form: torso (3) + head (2) + arms (6) + legs (6) = 17 joints
  humanoid_joints_ = {
    "torso_yaw", "torso_pitch", "torso_roll",
    "head_pan", "head_tilt",
    "left_shoulder_pitch", "left_shoulder_roll", "left_elbow",
    "right_shoulder_pitch", "right_shoulder_roll", "right_elbow",
    "left_hip_yaw", "left_hip_pitch", "left_knee",
    "right_hip_yaw", "right_hip_pitch", "right_knee"
  };

  // Vehicular position-controlled joints: head (2) + arms (6) = 8 joints
  vehicular_position_joints_ = {
    "head_pan", "head_tilt",
    "left_shoulder_pitch", "left_shoulder_roll", "left_elbow",
    "right_shoulder_pitch", "right_shoulder_roll", "right_elbow"
  };

  // Wheel joints: 4 wheels
  wheel_joints_ = {
    "front_left_wheel_joint", "front_right_wheel_joint",
    "rear_left_wheel_joint", "rear_right_wheel_joint"
  };

  // Initialize form configurations
  initialize_humanoid_config();
  initialize_vehicular_config();

  // Create subscribers
  status_sub_ = this->create_subscription<transformer_control::msg::TransformationStatus>(
    "transformation_status", 10,
    std::bind(&TransformationController::status_callback, this, std::placeholders::_1));

  joint_state_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
    "/joint_states", 10,
    std::bind(&TransformationController::joint_state_callback, this, std::placeholders::_1));

  // Create action clients
  humanoid_trajectory_client_ = rclcpp_action::create_client<control_msgs::action::JointTrajectory>(
    this, humanoid_controller_name_);
  
  arm_head_trajectory_client_ = rclcpp_action::create_client<control_msgs::action::JointTrajectory>(
    this, arm_head_controller_name_);
  
  wheel_velocity_client_ = rclcpp_action::create_client<control_msgs::action::JointTrajectory>(
    this, wheel_controller_name_);

  // Wait for action servers
  RCLCPP_INFO(this->get_logger(), "Waiting for action servers...");
  
  // Give servers time to become available (non-blocking in real implementation)
  // In production, should use proper waiting with timeout
  transformation_active_ = false;
  current_form_ = "humanoid";  // Default assumption
  
  RCLCPP_INFO(this->get_logger(), "Transformation Controller initialized");
  RCLCPP_INFO(this->get_logger(), "Humanoid joints: %zu", humanoid_joints_.size());
  RCLCPP_INFO(this->get_logger(), "Vehicular position joints: %zu", vehicular_position_joints_.size());
  RCLCPP_INFO(this->get_logger(), "Wheel joints: %zu", wheel_joints_.size());
}

TransformationController::~TransformationController()
{
  if (transformation_active_)
  {
    RCLCPP_WARN(this->get_logger(), "TransformationController destroyed during active transformation!");
  }
}

void TransformationController::status_callback(
  const transformer_control::msg::TransformationStatus::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(joint_state_mutex_);
  
  current_form_ = msg->current_form;
  
  // Check if transformation is starting or in progress
  if (msg->transformation_state == "TRANSFORMING_TO_VEHICULAR" ||
      msg->transformation_state == "TRANSFORMING_TO_HUMANOID")
  {
    if (!transformation_active_)
    {
      // Transformation just started
      target_form_ = (msg->transformation_state == "TRANSFORMING_TO_VEHICULAR") ?
                     "vehicular" : "humanoid";
      transformation_active_ = true;
      RCLCPP_INFO(this->get_logger(), "Transformation started: %s -> %s",
                  current_form_.c_str(), target_form_.c_str());
      execute_transformation(target_form_);
    }
  }
  else if (msg->transformation_state == "IDLE")
  {
    transformation_active_ = false;
    RCLCPP_DEBUG(this->get_logger(), "Transformation complete, system idle");
  }
  else if (msg->transformation_state == "EMERGENCY_STOP" ||
           msg->transformation_state == "ERROR")
  {
    transformation_active_ = false;
    RCLCPP_WARN(this->get_logger(), "Transformation interrupted: %s",
                msg->transformation_state.c_str());
  }
}

void TransformationController::joint_state_callback(
  const sensor_msgs::msg::JointState::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(joint_state_mutex_);
  current_joint_state_ = msg;
}

void TransformationController::execute_transformation(const std::string& target_form)
{
  RCLCPP_INFO(this->get_logger(), "Executing transformation to %s", target_form.c_str());
  
  // Get current joint positions
  std::lock_guard<std::mutex> lock(joint_state_mutex_);
  if (!current_joint_state_)
  {
    RCLCPP_ERROR(this->get_logger(), "No joint state available!");
    return;
  }
  
  // Extract current positions for all joints we care about
  auto current_positions = extract_positions(humanoid_joints_);
  
  // Determine target configurations
  const FormJointConfig* target_config = nullptr;
  std::string controller_name;
  
  if (target_form == "vehicular")
  {
    target_config = &vehicular_config_;
    RCLCPP_INFO(this->get_logger(), "Target: Vehicular form");
  }
  else if (target_form == "humanoid")
  {
    target_config = &humanoid_config_;
    RCLCPP_INFO(this->get_logger(), "Target: Humanoid form");
    controller_name = humanoid_controller_name_;
  }
  else
  {
    RCLCPP_ERROR(this->get_logger(), "Unknown target form: %s", target_form.c_str());
    return;
  }

  // Ensure necessary controllers are active
  if (target_form == "humanoid")
  {
    if (!ensure_controller_active(humanoid_controller_name_))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to activate humanoid controller");
      return;
    }
  }
  else if (target_form == "vehicular")
  {
    if (!ensure_controller_active(arm_head_controller_name_))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to activate arm/head controller");
      return;
    }
    if (!ensure_controller_active(wheel_controller_name_))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to activate wheel controller");
      return;
    }
  }
  else
  {
    RCLCPP_ERROR(this->get_logger(), "Unknown target form: %s", target_form.c_str());
    return;
  }
  
  // Build target position map
  std::map<std::string, double> target_positions;
  for (const auto& joint : target_config->joint_names)
  {
    target_positions[joint] = target_config->target_positions.at(joint);
  }
  
  // For transformation to humanoid, all 17 joints are controlled by one controller
  if (target_form == "humanoid")
  {
    // Check collision risk
    if (!check_collision_risk(target_positions, "humanoid"))
    {
      RCLCPP_ERROR(this->get_logger(), "Collision risk detected in target positions!");
      return;
    }
    
    // Generate trajectory for all humanoid joints
    auto trajectory = generate_spline_trajectory(humanoid_joints_, current_positions, target_positions, transformation_duration_);
    
    // Send to humanoid trajectory controller
    if (!send_trajectory_goal(humanoid_controller_name_, trajectory))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to send trajectory to %s", humanoid_controller_name_.c_str());
    }
  }
  // For transformation to vehicular, we need multiple controllers
  else if (target_form == "vehicular")
  {
    // 1. Set wheel target positions to current to keep wheels stationary (velocity will be 0)
    std::map<std::string, double> wheel_targets;
    auto current_wheel_positions = extract_positions(wheel_joints_);
    for (const auto& wheel : wheel_joints_)
    {
      wheel_targets[wheel] = current_wheel_positions[wheel];
    }
    
    if (!check_joint_limits(wheel_targets, wheel_joints_))
    {
      RCLCPP_ERROR(this->get_logger(), "Wheel target positions exceed limits!");
      return;
    }
    
    auto wheel_trajectory = generate_spline_trajectory(wheel_joints_, current_positions, wheel_targets, transformation_duration_ / 2.0);
    // Note: For velocity control, we'd need to convert position trajectory to velocity profile
    // For simplicity, we'll send positions and let controller handle it or use velocity directly
    // In practice, for velocity joints we'd send velocities in the trajectory
    send_trajectory_goal(wheel_velocity_controller_name_, wheel_trajectory);
    
    // 2. Set arms and head to tucked position
    std::map<std::string, double> arm_head_targets;
    for (const auto& joint : vehicular_position_joints_)
    {
      arm_head_targets[joint] = target_config->target_positions.at(joint);
    }
    
    if (!check_collision_risk(arm_head_targets, "vehicular"))
    {
      RCLCPP_ERROR(this->get_logger(), "Collision risk detected in vehicular arm/head positions!");
      return;
    }
    
    auto arm_head_trajectory = generate_spline_trajectory(vehicular_position_joints_, current_positions, arm_head_targets, transformation_duration_);
    send_trajectory_goal(arm_head_controller_name_, arm_head_trajectory);
  }
}

trajectory_msgs::msg::JointTrajectory TransformationController::generate_spline_trajectory(
  const std::vector<std::string>& joint_names,
  const std::map<std::string, double>& start_positions,
  const std::map<std::string, double>& target_positions,
  double duration)
{
  trajectory_msgs::msg::JointTrajectory trajectory;
  trajectory.joint_names = joint_names;
  
  // Generate waypoints
  auto waypoints = generate_spline_waypoints(joint_names, start_positions, target_positions, duration);
  trajectory.points = waypoints;
  
  RCLCPP_DEBUG(this->get_logger(), "Generated trajectory with %zu points for %zu joints",
               waypoints.size(), joint_names.size());
  
  return trajectory;
}

std::vector<trajectory_msgs::msg::JointTrajectoryPoint> TransformationController::generate_spline_waypoints(
  const std::vector<std::string>& joint_names,
  const std::map<std::string, double>& start_positions,
  const std::map<std::string, double>& target_positions,
  double duration)
{
  std::vector<trajectory_msgs::msg::JointTrajectoryPoint> waypoints;
  
  if (joint_names.empty() || duration <= 0.0)
  {
    return waypoints;
  }
  
  // Calculate number of waypoints based on spline resolution
  int num_points = static_cast<int>(duration / spline_resolution_) + 1;
  num_points = std::max(num_points, 2);  // At least start and end
  
  waypoints.reserve(num_points);
  
  // For each joint, compute cubic spline coefficients
  // Using simplified cubic polynomial: s(t) = a0 + a1*t + a2*t^2 + a3*t^3
  // With boundary conditions: s(0)=start, s(T)=target, s'(0)=0, s'(T)=0
  
  for (int i = 0; i < num_points; ++i)
  {
    double t = (i == num_points - 1) ? duration : i * spline_resolution_;
    double normalized_t = t / duration;  // Normalized time [0, 1]
    
    trajectory_msgs::msg::JointTrajectoryPoint point;
    point.positions.resize(joint_names.size());
    point.velocities.resize(joint_names.size(), 0.0);
    point.accelerations.resize(joint_names.size(), 0.0);
    point.time_from_start = rclcpp::Duration::from_seconds(t);
    
    for (size_t j = 0; j < joint_names.size(); ++j)
    {
      const std::string& joint = joint_names[j];
      double start = start_positions.at(joint);
      double target = target_positions.at(joint);
      
      // Cubic Hermite spline with zero initial and final velocities
      // s(t) = (2t^3 - 3t^2 + 1)*p0 + (t^3 - 2t^2 + t)*m0 + (-2t^3 + 3t^2)*p1 + (t^3 - t^2)*m1
      // With m0 = m1 = 0: s(t) = (2t^3 - 3t^2 + 1)*p0 + (-2t^3 + 3t^2)*p1
      // Simplified: s(t) = p0 + (p1 - p0) * (3t^2 - 2t^3) where t = normalized time
      
      double t_norm = normalized_t;
      double s = start + (target - start) * (3 * t_norm * t_norm - 2 * t_norm * t_norm * t_norm);
      double v = 0.0;
      
      if (i > 0 && i < num_points - 1)
      {
        // Estimate velocity from position differences
        double dt_next = spline_resolution_;
        double t_next = (i + 1) * spline_resolution_ / duration;
        double s_next = start + (target - start) * (3 * t_next * t_next - 2 * t_next * t_next * t_next);
        v = (s_next - s) / dt_next;
      }
      else if (i == num_points - 1)
      {
        v = 0.0;
      }
      
      point.positions[j] = s;
      point.velocities[j] = v;
    }
    
    waypoints.push_back(point);
  }
  
  return waypoints;
}

bool TransformationController::check_collision_risk(
  const std::map<std::string, double>& positions,
  const std::string& form)
{
  // Check joint limits
  if (!check_joint_limits(positions, form == "humanoid" ? humanoid_joints_ : vehicular_position_joints_))
  {
    return false;
  }
  
  // Check self-collision
  if (!check_self_collision(positions, form))
  {
    return false;
  }
  
  return true;
}

bool TransformationController::check_joint_limits(
  const std::map<std::string, double>& positions,
  const std::vector<std::string>& joint_names)
{
  // This would ideally get limits from parameter server or URDF
  // For now, use hardcoded limits matching the URDF
  
  const std::map<std::string, std::pair<double, double>> joint_limits = {
    // Torso
    {"torso_yaw", {-1.57, 1.57}},
    {"torso_pitch", {-0.5, 0.5}},
    {"torso_roll", {-0.3, 0.3}},
    // Head
    {"head_pan", {-1.57, 1.57}},
    {"head_tilt", {-0.5, 0.5}},
    // Left arm
    {"left_shoulder_pitch", {-1.57, 1.57}},
    {"left_shoulder_roll", {-1.57, 1.57}},
    {"left_elbow", {-1.57, 0.0}},
    // Right arm
    {"right_shoulder_pitch", {-1.57, 1.57}},
    {"right_shoulder_roll", {-1.57, 1.57}},
    {"right_elbow", {-1.57, 0.0}},
    // Left leg
    {"left_hip_yaw", {-0.5, 0.5}},
    {"left_hip_pitch", {-1.57, 1.57}},
    {"left_knee", {0.0, 2.0}},
    // Right leg
    {"right_hip_yaw", {-0.5, 0.5}},
    {"right_hip_pitch", {-1.57, 1.57}},
    {"right_knee", {0.0, 2.0}}
  };
  
  for (const auto& joint : joint_names)
  {
    auto it = positions.find(joint);
    if (it == positions.end())
    {
      RCLCPP_WARN(this->get_logger(), "Joint %s not found in positions", joint.c_str());
      continue;
    }
    
    double pos = it->second;
    auto limit_it = joint_limits.find(joint);
    if (limit_it != joint_limits.end())
    {
      double lower = limit_it->second.first;
      double upper = limit_it->second.second;
      if (pos < lower - collision_check_threshold_ || pos > upper + collision_check_threshold_)
      {
        RCLCPP_ERROR(this->get_logger(), "Joint %s exceeds limits: %.3f (range: [%.3f, %.3f])",
                     joint.c_str(), pos, lower, upper);
        return false;
      }
    }
  }
  
  return true;
}

bool TransformationController::check_self_collision(
  const std::map<std::string, double>& positions,
  const std::string& form)
{
  // Simple collision checking based on heuristics
  // In a real implementation, this would use FCL or similar
  
  if (form == "humanoid")
  {
    // Check if legs are intersecting or at invalid configuration
    auto get_leg_angle = [&](const std::string& prefix) {
      auto hip = positions.at(prefix + "_hip_pitch");
      auto knee = positions.at(prefix + "_knee");
      return hip + knee;
    };
    
    double left_leg_angle = get_leg_angle("left");
    double right_leg_angle = get_leg_angle("right");
    
    // Legs should not be fully folded backwards (hyperextension check)
    if (left_leg_angle > 2.5 || right_leg_angle > 2.5)
    {
      RCLCPP_WARN(this->get_logger(), "Potential leg hyperextension detected");
      return false;
    }
    
    // Check arms not interfering with torso
    if (positions.at("left_shoulder_pitch") < -1.0 && positions.at("torso_roll") > 0.2)
    {
      RCLCPP_WARN(this->get_logger(), "Potential left arm/torso interference");
      return false;
    }
    
    if (positions.at("right_shoulder_pitch") < -1.0 && positions.at("torso_roll") < -0.2)
    {
      RCLCPP_WARN(this->get_logger(), "Potential right arm/torso interference");
      return false;
    }
  }
  else if (form == "vehicular")
  {
    // Check arms are properly tucked
    for (const auto& side : {"left", "right"})
    {
      double shoulder_pitch = positions.at(side + "_shoulder_pitch");
      double shoulder_roll = positions.at(side + "_shoulder_roll");
      double elbow = positions.at(side + "_elbow");
      
      // Arms should be folded against body
      if (shoulder_pitch > -0.5 || elbow > -0.3)
      {
        RCLCPP_WARN(this->get_logger(), "%s arm not properly tucked (pitch: %.2f, elbow: %.2f)",
                    side.c_str(), shoulder_pitch, elbow);
        return false;
      }
    }
  }
  
  return true;
}

bool TransformationController::send_trajectory_goal(
  const std::string& controller_name,
  const trajectory_msgs::msg::JointTrajectory& trajectory)
{
  RCLCPP_INFO(this->get_logger(), "Sending trajectory to controller: %s", controller_name.c_str());
  
  auto goal_msg = control_msgs::action::JointTrajectory::Goal();
  goal_msg.trajectory = trajectory;
  goal_msg.goal_time_tolerance = rclcpp::Duration::from_seconds(1.0);
  
  rclcpp_action::Client<control_msgs::action::JointTrajectory>::SharedPtr client;
  
  if (controller_name == humanoid_controller_name_)
  {
    client = humanoid_trajectory_client_;
  }
  else if (controller_name == arm_head_controller_name_)
  {
    client = arm_head_trajectory_client_;
  }
  else if (controller_name == wheel_velocity_controller_name_)
  {
    client = wheel_velocity_client_;
  }
  else
  {
    RCLCPP_ERROR(this->get_logger(), "Unknown controller: %s", controller_name.c_str());
    return false;
  }
  
  if (!client)
  {
    RCLCPP_ERROR(this->get_logger(), "Action client for %s is null", controller_name.c_str());
    return false;
  }
  
  // Check if server is available
  if (!client->wait_for_action_server(std::chrono::seconds(5)))
  {
    RCLCPP_ERROR(this->get_logger(), "Action server for %s not available", controller_name.c_str());
    return false;
  }
  
  auto send_goal_options = rclcpp_action::Client<control_msgs::action::JointTrajectory>::SendGoalOptions();
  send_goal_options.goal_response_callback =
    [this](const rclcpp_action::ClientGoalHandle<control_msgs::action::JointTrajectory>::SharedPtr & handle)
    {
      if (!handle)
      {
        RCLCPP_ERROR(this->get_logger(), "Goal was rejected by server");
      }
      else
      {
        RCLCPP_INFO(this->get_logger(), "Goal accepted by server");
      }
    };
  
  send_goal_options.result_callback =
    [this](const rclcpp_action::ClientGoalHandle<control_msgs::action::JointTrajectory>::WrappedResult & result)
    {
      handle_trajectory_result(result);
    };
  
  send_goal_options.feedback_callback =
    [](rclcpp_action::ClientGoalHandle<control_msgs::action::JointTrajectory>::SharedPtr,
      const std::shared_ptr<const control_msgs::action::JointTrajectory::Feedback> feedback)
    {
      // Could publish intermediate progress
      RCLCPP_DEBUG(rclcpp::get_logger("transformation_controller"),
                   "Trajectory execution progress feedback received");
    };
  
  auto future_result = client->async_send_goal(goal_msg, send_goal_options);
  
  // Wait for result (in a real implementation, this should be async or with a future)
  // For now, we don't block; the result callback handles completion
  return true;
}

void TransformationController::handle_trajectory_result(
  const rclcpp_action::ClientGoalHandle<control_msgs::action::JointTrajectory>::WrappedResult & result)
{
  switch (result.code)
  {
    case rclcpp_action::ResultCode::SUCCEEDED:
      RCLCPP_INFO(this->get_logger(), "Trajectory execution succeeded");
      break;
    case rclcpp_action::ResultCode::ABORTED:
      RCLCPP_ERROR(this->get_logger(), "Trajectory execution aborted");
      transformation_active_ = false;
      break;
    case rclcpp_action::ResultCode::CANCELED:
      RCLCPP_WARN(this->get_logger(), "Trajectory execution canceled");
      transformation_active_ = false;
      break;
    default:
      RCLCPP_ERROR(this->get_logger(), "Trajectory execution failed with code: %d", result.code);
      transformation_active_ = false;
      break;
  }
}

std::map<std::string, double> TransformationController::extract_positions(
  const std::vector<std::string>& joint_names) const
{
  std::map<std::string, double> positions;
  
  if (!current_joint_state_)
  {
    RCLCPP_WARN(this->get_logger(), "No joint state available for extraction");
    for (const auto& joint : joint_names)
    {
      positions[joint] = 0.0;  // Default to zero
    }
    return positions;
  }
  
  const auto& names = current_joint_state_->name;
  const auto& states = current_joint_state_->position;
  
  for (size_t i = 0; i < names.size(); ++i)
  {
    if (std::find(joint_names.begin(), joint_names.end(), names[i]) != joint_names.end())
    {
      positions[names[i]] = states[i];
    }
  }
  
  // Check for missing joints
  for (const auto& joint : joint_names)
  {
    if (positions.find(joint) == positions.end())
    {
      RCLCPP_WARN(this->get_logger(), "Joint %s not found in current state, defaulting to 0.0", joint.c_str());
      positions[joint] = 0.0;
    }
  }
  
  return positions;
}

void TransformationController::publish_transformation_status(
  const std::string& state,
  double progress,
  bool is_safe)
{
  // This could publish to a dedicated topic if needed, but state machine already publishes
  RCLCPP_DEBUG(this->get_logger(), "Status: %s, Progress: %.2f%%, Safe: %s",
               state.c_str(), progress * 100.0, is_safe ? "true" : "false");
}

void TransformationController::initialize_humanoid_config()
{
  // Define target positions for humanoid neutral pose
  // Most joints at zero, legs in slight standing pose
  humanoid_config_.joint_names = humanoid_joints_;
  humanoid_config_.uses_velocity_control = false;
  
  auto& targets = humanoid_config_.target_positions;
  
  // Torso - centered
  targets["torso_yaw"] = 0.0;
  targets["torso_pitch"] = 0.0;
  targets["torso_roll"] = 0.0;
  
  // Head - centered
  targets["head_pan"] = 0.0;
  targets["head_tilt"] = 0.0;
  
  // Left arm - relaxed down
  targets["left_shoulder_pitch"] = 0.0;
  targets["left_shoulder_roll"] = 0.0;
  targets["left_elbow"] = 0.0;
  
  // Right arm - relaxed down
  targets["right_shoulder_pitch"] = 0.0;
  targets["right_shoulder_roll"] = 0.0;
  targets["right_elbow"] = 0.0;
  
  // Left leg - standing pose (slight bend)
  targets["left_hip_yaw"] = 0.0;
  targets["left_hip_pitch"] = -0.3;  // Slight forward lean
  targets["left_knee"] = 0.1;       // Slight bend
  
  // Right leg - standing pose
  targets["right_hip_yaw"] = 0.0;
  targets["right_hip_pitch"] = -0.3;
  targets["right_knee"] = 0.1;
  
  RCLCPP_INFO(this->get_logger(), "Humanoid configuration initialized with %zu joints",
              humanoid_config_.joint_names.size());
}

void TransformationController::initialize_vehicular_config()
{
  // Define target positions for vehicular form
  vehicular_config_.joint_names = vehicular_position_joints_;
  vehicular_config_.uses_velocity_control = false;
  
  auto& targets = vehicular_config_.target_positions;
  
  // Head - centered
  targets["head_pan"] = 0.0;
  targets["head_tilt"] = 0.0;
  
  // Left arm - fully tucked (folded against body)
  targets["left_shoulder_pitch"] = -1.2;  // Arm forward
  targets["left_shoulder_roll"] = -0.5;   // Arm inward
  targets["left_elbow"] = -0.8;          // Elbow bent
  
  // Right arm - fully tucked
  targets["right_shoulder_pitch"] = -1.2;
  targets["right_shoulder_roll"] = 0.5;   // Arm inward (mirror)
  targets["right_elbow"] = -0.8;
  
  RCLCPP_INFO(this->get_logger(), "Vehicular configuration initialized with %zu position joints",
              vehicular_config_.joint_names.size());
}

bool TransformationController::ensure_controller_active(const std::string& controller_name)
{
  if (!controller_state_client_)
  {
    RCLCPP_ERROR(this->get_logger(), "Controller state client not initialized");
    return false;
  }

  auto request = std::make_shared<lifecycle_msgs::srv::GetState::Request>();
  auto future = controller_state_client_->async_send_request(request);
  
  if (rclcpp::spin_until_future_complete(controller_manager_node_, future, 2s) !=
      rclcpp::FutureReturnCode::SUCCESS)
  {
    RCLCPP_ERROR(this->get_logger(), "Failed to get controller state: %s", 
                 controller_name.c_str());
    return false;
  }
  
  auto result = future.get();
  if (result->current_state.id == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
  {
    RCLCPP_DEBUG(this->get_logger(), "Controller %s already active", controller_name.c_str());
    return true;
  }
  
  // Activate it
  auto change_request = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
  change_request->transition.id = lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE;
  
  auto change_future = controller_manager_client_->async_send_request(change_request);
  
  if (rclcpp::spin_until_future_complete(controller_manager_node_, change_future, 2s) !=
      rclcpp::FutureReturnCode::SUCCESS)
  {
    RCLCPP_ERROR(this->get_logger(), "Failed to activate controller: %s", 
                 controller_name.c_str());
    return false;
  }
  
  auto change_result = change_future.get();
  if (!change_result->success)
  {
    RCLCPP_ERROR(this->get_logger(), "Controller activation failed for %s", 
                 controller_name.c_str());
    return false;
  }
  
  RCLCPP_INFO(this->get_logger(), "Controller %s activated", controller_name.c_str());
  return true;
}

bool TransformationController::deactivate_controller(const std::string& controller_name)
{
  if (!controller_manager_client_)
  {
    RCLCPP_ERROR(this->get_logger(), "Controller manager client not initialized");
    return false;
  }

  auto request = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
  request->transition.id = lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE;
  
  auto future = controller_manager_client_->async_send_request(request);
  
  if (rclcpp::spin_until_future_complete(controller_manager_node_, future, 2s) !=
      rclcpp::FutureReturnCode::SUCCESS)
  {
    RCLCPP_ERROR(this->get_logger(), "Failed to deactivate controller: %s", 
                 controller_name.c_str());
    return false;
  }
  
  auto result = future.get();
  if (!result->success)
  {
    RCLCPP_ERROR(this->get_logger(), "Controller deactivation failed for %s", 
                 controller_name.c_str());
    return false;
  }
  
  RCLCPP_INFO(this->get_logger(), "Controller %s deactivated", controller_name.c_str());
  return true;
}

}  // namespace transformer_control

#include "rclcpp_components/register_node_macro.hpp"

// Register the component
RCLCPP_COMPONENTS_REGISTER_NODE(transformer_control::TransformationController)

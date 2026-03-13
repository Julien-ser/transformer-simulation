#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "rclcpp/lifecycle/lifecycle_publisher.hpp"
#include "rclcpp/lifecycle/lifecycle_subscription.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "transformer_control/msg/transformation_status.hpp"
#include "transformer_control/srv/emergency_stop.hpp"
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

namespace transformer_control
{

enum class SafetyLevel
{
  NORMAL,
  WARNING,
  CRITICAL,
  EMERGENCY
};

struct JointLimits
{
  double lower;
  double upper;
  double velocity_limit;
  double effort_limit;
};

class SafetyMonitor : public rclcpp_lifecycle::LifecycleNode
{
public:
  SafetyMonitor(const rclcpp::NodeOptions & options)
  : LifecycleNode("safety_monitor", options)
  {
    // Declare parameters
    this->declare_parameter("safety_check_rate", 50.0);  // Hz
    this->declare_parameter("joint_state_timeout", 1.0);  // seconds
    this->declare_parameter("self_collision_check_enabled", true);
    this->declare_parameter("joint_limit_check_enabled", true);
    this->declare_parameter("sensor_health_check_enabled", true);
    
    // Get parameters
    safety_check_rate_ = this->get_parameter("safety_check_rate").as_double();
    joint_state_timeout_ = this->get_parameter("joint_state_timeout").as_double();
    self_collision_check_enabled_ = this->get_parameter("self_collision_check_enabled").as_bool();
    joint_limit_check_enabled_ = this->get_parameter("joint_limit_check_enabled").as_bool();
    sensor_health_check_enabled_ = this->get_parameter("sensor_health_check_enabled").as_bool();

    RCLCPP_INFO(this->get_logger(), "Safety Monitor initialized");
    RCLCPP_INFO(this->get_logger(), "Check rate: %.1f Hz", safety_check_rate_);
  }

  ~SafetyMonitor()
  {
    if (safety_pub_ && safety_pub_->get_subscription_count() > 0)
    {
      auto msg = std::make_unique<std_msgs::msg::Bool>();
      msg->data = false;
      safety_pub_->publish(std::move(msg));
    }
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Configuring safety monitor...");

    // Initialize state
    safety_level_ = SafetyLevel::NORMAL;
    emergency_stop_active_ = false;
    joint_state_received_ = false;
    last_joint_state_time_ = this->now();
    
    // Initialize joint limits from URDF/parameters
    initialize_joint_limits();

    // Create publishers
    safety_pub_ = this->create_publisher<std_msgs::msg::Bool>("safety_status", 10);
    safety_level_pub_ = this->create_publisher<std_msgs::msg::String>("safety_level", 10);
    safety_violation_pub_ = this->create_publisher<std_msgs::msg::String>("safety_violation", 10);

    // Create services
    emergency_stop_srv_ = this->create_service<transformer_control::srv::EmergencyStop>(
      "safety_monitor/emergency_stop",
      std::bind(&SafetyMonitor::emergency_stop_callback, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    reset_safety_srv_ = this->create_service<std_srvs::srv::Trigger>(
      "safety_monitor/reset",
      std::bind(&SafetyMonitor::reset_safety_callback, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Create subscribers
    joint_state_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
      "/joint_states", 10,
      std::bind(&SafetyMonitor::joint_state_callback, this, std::placeholders::_1));
    
    transformation_status_sub_ = this->create_subscription<transformer_control::msg::TransformationStatus>(
      "transformation_status", 10,
      std::bind(&SafetyMonitor::transformation_status_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "Safety monitor configured successfully");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Activating safety monitor...");
    
    // Start safety check timer
    safety_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(1.0 / safety_check_rate_),
      std::bind(&SafetyMonitor::safety_check_timer_callback, this));
    
    // Start joint state timeout monitoring
    timeout_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(joint_state_timeout_ / 2.0),
      std::bind(&SafetyMonitor::timeout_check_callback, this));
    
    publish_safety_status();
    
    RCLCPP_INFO(this->get_logger(), "Safety monitor active");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_deactivate(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Deactivating safety monitor...");
    
    if (safety_timer_)
    {
      safety_timer_->cancel();
      safety_timer_.reset();
    }
    
    if (timeout_timer_)
    {
      timeout_timer_->cancel();
      timeout_timer_.reset();
    }

    publish_safety_status();
    
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Cleaning up safety monitor...");
    
    // Clean up publishers
    safety_pub_.reset();
    safety_level_pub_.reset();
    safety_violation_pub_.reset();
    
    // Clean up services
    emergency_stop_srv_.reset();
    reset_safety_srv_.reset();
    
    // Clean up timers
    if (safety_timer_)
    {
      safety_timer_->cancel();
      safety_timer_.reset();
    }
    if (timeout_timer_)
    {
      timeout_timer_->cancel();
      timeout_timer_.reset();
    }

    // Clean up subscribers
    joint_state_sub_.reset();
    transformation_status_sub_.reset();

    // Reset state
    safety_level_ = SafetyLevel::NORMAL;
    emergency_stop_active_ = false;
    joint_state_received_ = false;
    current_joint_state_.reset();
    current_transformation_state_ = "IDLE";
    
    RCLCPP_INFO(this->get_logger(), "Safety monitor cleaned up");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_shutdown(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Shutting down safety monitor...");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

private:
  // Parameters
  double safety_check_rate_;
  double joint_state_timeout_;
  bool self_collision_check_enabled_;
  bool joint_limit_check_enabled_;
  bool sensor_health_check_enabled_;

  // State
  SafetyLevel safety_level_;
  bool emergency_stop_active_;
  bool joint_state_received_;
  rclcpp::Time last_joint_state_time_;
  std::string current_transformation_state_;
  
  // Current data
  sensor_msgs::msg::JointState::SharedPtr current_joint_state_;
  std::mutex joint_state_mutex_;

  // ROS entities
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr safety_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr safety_level_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr safety_violation_pub_;
  
  rclcpp::Service<transformer_control::srv::EmergencyStop>::SharedPtr emergency_stop_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_safety_srv_;
  
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_state_sub_;
  rclcpp::Subscription<transformer_control::msg::TransformationStatus>::SharedPtr transformation_status_sub_;
  
  rclcpp::TimerBase::SharedPtr safety_timer_;
  rclcpp::TimerBase::SharedPtr timeout_timer_;

  // Joint limits storage
  std::map<std::string, JointLimits> joint_limits_;

  // Callback functions
  void joint_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(joint_state_mutex_);
    current_joint_state_ = msg;
    joint_state_received_ = true;
    last_joint_state_time_ = this->now();
  }

  void transformation_status_callback(const transformer_control::msg::TransformationStatus::SharedPtr msg)
  {
    current_transformation_state_ = msg->transformation_state;
  }

  void emergency_stop_callback(
    const std::shared_ptr<transformer_control::srv::EmergencyStop::Request> request,
    std::shared_ptr<transformer_control::srv::EmergencyStop::Response> response)
  {
    RCLCPP_WARN(this->get_logger(), "Emergency stop requested: %s", 
                request->reason.empty() ? "No reason given" : request->reason.c_str());
    
    emergency_stop_active_ = true;
    safety_level_ = SafetyLevel::EMERGENCY;
    
    response->success = true;
    response->message = "Emergency stop activated by safety monitor";
    
    publish_safety_status();
    publish_safety_violation("Emergency stop activated: " + (request->reason.empty() ? "No reason" : request->reason));
  }

  void reset_safety_callback(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response)
  {
    if (emergency_stop_active_)
    {
      RCLCPP_INFO(this->get_logger(), "Resetting emergency stop");
      emergency_stop_active_ = false;
      
      // Only reset to NORMAL if no other violations
      if (safety_level_ == SafetyLevel::EMERGENCY)
      {
        safety_level_ = SafetyLevel::NORMAL;
      }
      
      response->success = true;
      response->message = "Emergency stop reset";
    }
    else
    {
      response->success = false;
      response->message = "No emergency stop active";
    }
    
    publish_safety_status();
  }

  void safety_check_timer_callback()
  {
    if (emergency_stop_active_)
    {
      // Emergency stop is active, publish unsafe and return
      publish_safety_status();
      return;
    }

    bool all_checks_passed = true;
    std::vector<std::string> violations;

    // 1. Sensor health check
    if (sensor_health_check_enabled_)
    {
      if (!check_sensor_health())
      {
        all_checks_passed = false;
        violations.push_back("Sensor health check failed");
      }
    }

    // 2. Joint limits check
    if (joint_limit_check_enabled_ && joint_state_received_)
    {
      if (!check_joint_limits(violations))
      {
        all_checks_passed = false;
      }
    }

    // 3. Self-collision check
    if (self_collision_check_enabled_ && joint_state_received_)
    {
      if (!check_self_collision(violations))
      {
        all_checks_passed = false;
      }
    }

    // 4. Transformation state validation
    if (!check_transformation_state_validity(violations))
    {
      all_checks_passed = false;
    }

    // Update safety level based on violations
    update_safety_level(all_checks_passed, violations);
    
    // Take action if critical
    if (safety_level_ == SafetyLevel::CRITICAL || safety_level_ == SafetyLevel::EMERGENCY)
    {
      handle_safety_violation();
    }

    publish_safety_status();
  }

  void timeout_check_callback()
  {
    if (!sensor_health_check_enabled_)
      return;
      
    auto now = this->now();
    auto time_since_last = (now - last_joint_state_time_).seconds();
    
    if (joint_state_received_ && time_since_last > joint_state_timeout_)
    {
      RCLCPP_WARN(this->get_logger(), "Joint state timeout: %.2f seconds without update", 
                  time_since_last);
      joint_state_received_ = false;
      publish_safety_violation("Joint state data stale (timeout)");
    }
  }

  bool check_sensor_health()
  {
    // Check if we've ever received joint states
    if (!joint_state_received_)
    {
      RCLCPP_WARN(this->get_logger(), "No joint state received yet");
      return false;
    }

    // Check joint state freshness
    auto now = this->now();
    auto time_since_last = (now - last_joint_state_time_).seconds();
    if (time_since_last > joint_state_timeout_)
    {
      RCLCPP_WARN(this->get_logger(), "Joint state data stale: %.2f seconds old", 
                  time_since_last);
      return false;
    }

    // Validate joint state message structure
    std::lock_guard<std::mutex> lock(joint_state_mutex_);
    if (!current_joint_state_)
    {
      return false;
    }

    // Check that position array size matches name array size
    if (current_joint_state_->name.size() != current_joint_state_->position.size())
    {
      RCLCPP_WARN(this->get_logger(), "Joint state size mismatch: names=%zu, positions=%zu",
                  current_joint_state_->name.size(), current_joint_state_->position.size());
      return false;
    }

    // Check for NaN or Inf in positions
    for (size_t i = 0; i < current_joint_state_->position.size(); ++i)
    {
      if (std::isnan(current_joint_state_->position[i]) || 
          std::isinf(current_joint_state_->position[i]))
      {
        RCLCPP_ERROR(this->get_logger(), "Invalid position value for joint %s: %f",
                     current_joint_state_->name[i].c_str(), current_joint_state_->position[i]);
        return false;
      }
    }

    return true;
  }

  bool check_joint_limits(std::vector<std::string>& violations)
  {
    bool all_ok = true;
    
    std::lock_guard<std::mutex> lock(joint_state_mutex_);
    if (!current_joint_state_)
    {
      violations.push_back("No joint state available for limit check");
      return false;
    }

    for (size_t i = 0; i < current_joint_state_->name.size(); ++i)
    {
      const std::string& joint_name = current_joint_state_->name[i];
      double position = current_joint_state_->position[i];
      
      auto limit_it = joint_limits_.find(joint_name);
      if (limit_it != joint_limits_.end())
      {
        const JointLimits& limits = limit_it->second;
        
        if (position < limits.lower || position > limits.upper)
        {
          std::string violation = fmt::format(
            "Joint {} out of limits: {:.3f} (range: [{:.3f}, {:.3f}])",
            joint_name, position, limits.lower, limits.upper);
          RCLCPP_ERROR(this->get_logger(), "%s", violation.c_str());
          violations.push_back(violation);
          all_ok = false;
        }
      }
    }
    
    return all_ok;
  }

  bool check_self_collision(std::vector<std::string>& violations)
  {
    bool all_ok = true;
    
    std::lock_guard<std::mutex> lock(joint_state_mutex_);
    if (!current_joint_state_)
    {
      violations.push_back("No joint state available for collision check");
      return false;
    }

    // Build position map for easy access
    std::map<std::string, double> positions;
    for (size_t i = 0; i < current_joint_state_->name.size(); ++i)
    {
      positions[current_joint_state_->name[i]] = current_joint_state_->position[i];
    }

    // Check based on current transformation state
    if (current_transformation_state_ == "TRANSFORMING_TO_VEHICULAR" ||
        current_transformation_state_ == "TRANSFORMING_TO_HUMANOID")
    {
      // During transformation, check intermediate poses for collisions
      if (!check_transformation_collision(positions, violations))
      {
        all_ok = false;
      }
    }
    else if (current_transformation_state_ == "IDLE")
    {
      // Check current form's static collision
      std::string current_form = determine_current_form(positions);
      if (current_form == "humanoid")
      {
        if (!check_humanoid_collision(positions, violations))
        {
          all_ok = false;
        }
      }
      else if (current_form == "vehicular")
      {
        if (!check_vehicular_collision(positions, violations))
        {
          all_ok = false;
        }
      }
    }

    return all_ok;
  }

  bool check_transformation_collision(const std::map<std::string, double>& positions,
                                      std::vector<std::string>& violations)
  {
    // During transformation, check for intermediate configurations that could cause collisions
    // This is a simplified check - full collision detection would use FCL or similar
    
    bool all_ok = true;
    
    // Check if legs and arms are too close during transformation
    // Basic heuristic: if any arm joint is extended while legs are in certain positions
    
    // Check legs
    try
    {
      double left_leg_angle = positions.at("left_hip_pitch") + positions.at("left_knee");
      double right_leg_angle = positions.at("right_hip_pitch") + positions.at("right_knee");
      
      if (left_leg_angle > 2.2 || right_leg_angle > 2.2)
      {
        violations.push_back("Leg hyperextension risk during transformation");
        all_ok = false;
      }
    }
    catch (const std::out_of_range&)
    {
      // Joints not found, skip this check
    }

    // Check arms interfering with torso during movement
    try
    {
      if (positions.at("left_shoulder_pitch") < -0.8 && positions.at("torso_roll") > 0.15)
      {
        violations.push_back("Left arm/torso interference risk");
        all_ok = false;
      }
      if (positions.at("right_shoulder_pitch") < -0.8 && positions.at("torso_roll") < -0.15)
      {
        violations.push_back("Right arm/torso interference risk");
        all_ok = false;
      }
    }
    catch (const std::out_of_range&)
    {
      // Joints not found, skip
    }

    return all_ok;
  }

  bool check_humanoid_collision(const std::map<std::string, double>& positions,
                                std::vector<std::string>& violations)
  {
    bool all_ok = true;
    
    try
    {
      // Check leg extension limits
      double left_leg_angle = positions.at("left_hip_pitch") + positions.at("left_knee");
      double right_leg_angle = positions.at("right_hip_pitch") + positions.at("right_knee");
      
      if (left_leg_angle > 2.5 || right_leg_angle > 2.5)
      {
        violations.push_back("Leg hyperextension");
        all_ok = false;
      }

      // Check if knees are overextended (negative angle indicates hyperextension)
      if (positions.at("left_knee") < -0.1 || positions.at("right_knee") < -0.1)
      {
        violations.push_back("Knee hyperextension");
        all_ok = false;
      }

      // Check arm/torso interference
      if (positions.at("left_shoulder_pitch") < -1.2 && 
          std::abs(positions.at("torso_roll")) < 0.1)
      {
        violations.push_back("Left arm may intersect torso");
        all_ok = false;
      }
      
      if (positions.at("right_shoulder_pitch") < -1.2 && 
          std::abs(positions.at("torso_roll")) < 0.1)
      {
        violations.push_back("Right arm may intersect torso");
        all_ok = false;
      }
    }
    catch (const std::out_of_range&)
    {
      // Missing joints
    }

    return all_ok;
  }

  bool check_vehicular_collision(const std::map<std::string, double>& positions,
                                 std::vector<std::string>& violations)
  {
    bool all_ok = true;
    
    try
    {
      // Arms should be properly tucked
      for (const auto& side : {"left", "right"})
      {
        double shoulder_pitch = positions.at(side + "_shoulder_pitch");
        double shoulder_roll = positions.at(side + "_shoulder_roll");
        double elbow = positions.at(side + "_elbow");
        
      if (shoulder_pitch > -0.3)
      {
        violations.push_back( side + " arm shoulder pitch not tucked: " + std::to_string(shoulder_pitch));
        all_ok = false;
      }
      
      if (elbow > -0.1)
      {
        violations.push_back( side + " arm elbow not folded: " + std::to_string(elbow));
        all_ok = false;
      }
      }

      // Check that head is not tilted too much (obstructing sensors/path)
      if (std::abs(positions.at("head_tilt")) > 0.6)
      {
        violations.push_back("Head tilt extreme - may obstruct sensors");
        all_ok = false;
      }
    }
    catch (const std::out_of_range&)
    {
      // Missing joints
    }

    return all_ok;
  }

  bool check_transformation_state_validity(std::vector<std::string>& violations)
  {
    bool all_ok = true;
    
    // Check for valid transformation states
    if (current_transformation_state_ == "ERROR")
    {
      violations.push_back("Transformation system in ERROR state");
      all_ok = false;
    }
    
    if (current_transformation_state_ == "EMERGENCY_STOP")
    {
      violations.push_back("Transformation system in EMERGENCY_STOP state");
      all_ok = false;
    }

    return all_ok;
  }

  std::string determine_current_form(const std::map<std::string, double>& positions)
  {
    // Determine current form based on active joints and their positions
    // This is a heuristic - ideally we'd read from parameter or transformation status
    
    // Check if leg joints exist and are in a standing position
    bool has_legs = false;
    try
    {
      has_legs = positions.count("left_hip_pitch") && positions.count("right_hip_pitch") &&
                 (positions.at("left_hip_pitch") != 0.0 || positions.at("right_hip_pitch") != 0.0);
    }
    catch (...) {}
    
    if (has_legs)
    {
      return "humanoid";
    }
    
    return "vehicular";
  }

  void update_safety_level(bool all_checks_passed, const std::vector<std::string>& violations)
  {
    if (emergency_stop_active_)
    {
      safety_level_ = SafetyLevel::EMERGENCY;
      return;
    }

    if (violations.empty() || all_checks_passed)
    {
      safety_level_ = SafetyLevel::NORMAL;
    }
    else
    {
      // Determine severity based on violation types
      for (const auto& violation : violations)
      {
        if (violation.find("Emergency") != std::string::npos ||
            violation.find("CRITICAL") != std::string::npos)
        {
          safety_level_ = SafetyLevel::EMERGENCY;
          return;
        }
        if (violation.find("limit") != std::string::npos ||
            violation.find("collision") != std::string::npos ||
            violation.find("hyperextension") != std::string::npos)
        {
          safety_level_ = SafetyLevel::CRITICAL;
        }
      }
      
      if (safety_level_ != SafetyLevel::CRITICAL)
      {
        safety_level_ = SafetyLevel::WARNING;
      }
    }
  }

  void handle_safety_violation()
  {
    if (safety_level_ == SafetyLevel::CRITICAL || safety_level_ == SafetyLevel::EMERGENCY)
    {
      RCLCPP_ERROR(this->get_logger(), "Safety violation detected! Level: %d", 
                   static_cast<int>(safety_level_));
      
      // Could trigger emergency stop via service call to transformation_state_machine
      // For now, we rely on the monitoring node's status
    }
  }

  void publish_safety_status()
  {
    bool is_safe = (safety_level_ == SafetyLevel::NORMAL);
    
    auto msg = std::make_unique<std_msgs::msg::Bool>();
    msg->data = is_safe;
    safety_pub_->publish(std::move(msg));
    
    // Publish safety level as string
    auto level_msg = std::make_unique<std_msgs::msg::String>();
    switch (safety_level_)
    {
      case SafetyLevel::NORMAL:
        level_msg->data = "NORMAL";
        break;
      case SafetyLevel::WARNING:
        level_msg->data = "WARNING";
        break;
      case SafetyLevel::CRITICAL:
        level_msg->data = "CRITICAL";
        break;
      case SafetyLevel::EMERGENCY:
        level_msg->data = "EMERGENCY";
        break;
    }
    safety_level_pub_->publish(std::move(level_msg));
  }

  void publish_safety_violation(const std::string& violation)
  {
    auto msg = std::make_unique<std_msgs::msg::String>();
    msg->data = violation;
    safety_violation_pub_->publish(std::move(msg));
    RCLCPP_WARN(this->get_logger(), "Safety Violation: %s", violation.c_str());
  }

  void initialize_joint_limits()
  {
    // Initialize joint limits from parameters or hardcoded values
    // These should match the URDF limits
    
    joint_limits_ = {
      // Torso
      {"torso_yaw", {-1.57, 1.57, 1.0, 50.0}},
      {"torso_pitch", {-0.5, 0.5, 1.0, 50.0}},
      {"torso_roll", {-0.3, 0.3, 1.0, 50.0}},
      
      // Head
      {"head_pan", {-1.57, 1.57, 1.0, 20.0}},
      {"head_tilt", {-0.5, 0.5, 1.0, 20.0}},
      
      // Left arm
      {"left_shoulder_pitch", {-1.57, 1.57, 1.0, 30.0}},
      {"left_shoulder_roll", {-1.57, 1.57, 1.0, 30.0}},
      {"left_elbow", {-1.57, 0.0, 1.0, 20.0}},
      
      // Right arm
      {"right_shoulder_pitch", {-1.57, 1.57, 1.0, 30.0}},
      {"right_shoulder_roll", {-1.57, 1.57, 1.0, 30.0}},
      {"right_elbow", {-1.57, 0.0, 1.0, 20.0}},
      
      // Left leg
      {"left_hip_yaw", {-0.5, 0.5, 1.0, 60.0}},
      {"left_hip_pitch", {-1.57, 1.57, 1.0, 80.0}},
      {"left_knee", {0.0, 2.0, 1.0, 60.0}},
      
      // Right leg
      {"right_hip_yaw", {-0.5, 0.5, 1.0, 60.0}},
      {"right_hip_pitch", {-1.57, 1.57, 1.0, 80.0}},
      {"right_knee", {0.0, 2.0, 1.0, 60.0}},
      
      // Front wheels (continuous, but we can check velocity limits)
      {"front_left_wheel_joint", {-M_PI, M_PI, 10.0, 5.0}},
      {"front_right_wheel_joint", {-M_PI, M_PI, 10.0, 5.0}},
      {"rear_left_wheel_joint", {-M_PI, M_PI, 10.0, 5.0}},
      {"rear_right_wheel_joint", {-M_PI, M_PI, 10.0, 5.0}}
    };
    
    RCLCPP_INFO(this->get_logger(), "Initialized limits for %zu joints", joint_limits_.size());
  }
};

}  // namespace transformer_control

#include "rclcpp_components/register_node_macro.hpp"

RCLCPP_COMPONENTS_REGISTER_NODE(transformer_control::SafetyMonitor)

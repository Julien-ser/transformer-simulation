#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "transformer_control/msg/transformation_status.hpp"
#include "transformer_control/srv/transform_to_vehicular.hpp"
#include "transformer_control/srv/transform_to_humanoid.hpp"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <cmath>

using namespace std::chrono_literals;

namespace transformer_control
{

enum class MissionMode
{
  IDLE,
  NAVIGATING_TO_GOAL,
  AVOIDING_OBSTACLE,
  TRANSFORMING,
  COMPLETED
};

class AutonomousMission : public rclcpp::Node
{
public:
  AutonomousMission(const rclcpp::NodeOptions & options)
  : Node("autonomous_mission", options)
  {
    RCLCPP_INFO(this->get_logger(), "Initializing Autonomous Mission Node...");

    // Declare and get parameters
    declare_parameter("distance_threshold", 5.0);  // meters - use vehicular for distances > this
    declare_parameter("obstacle_range", 3.0);      // meters - consider obstacles within this range
    declare_parameter("humanoid_speed", 0.5);      // m/s
    declare_parameter("vehicular_speed", 2.0);     // m/s
    declare_parameter("goal_tolerance", 0.5);      // meters
    declare_parameter("transformation_cooldown", 10.0);  // seconds between transforms
    declare_parameter("mission_waypoints", std::vector<double>{});  // flat array [x1, y1, x2, y2, ...]

    distance_threshold_ = get_parameter("distance_threshold").as_double();
    obstacle_range_ = get_parameter("obstacle_range").as_double();
    humanoid_speed_ = get_parameter("humanoid_speed").as_double();
    vehicular_speed_ = get_parameter("vehicular_speed").as_double();
    goal_tolerance_ = get_parameter("goal_tolerance").as_double();
    transformation_cooldown_ = get_parameter("transformation_cooldown").as_double();
    
    auto waypoints = get_parameter("mission_waypoints").as_double_array();
    parse_waypoints(waypoints);

    RCLCPP_INFO(this->get_logger(), "Mission parameters:");
    RCLCPP_INFO(this->get_logger(), "  Distance threshold: %.2f m", distance_threshold_);
    RCLCPP_INFO(this->get_logger(), "  Obstacle range: %.2f m", obstacle_range_);
    RCLCPP_INFO(this->get_logger(), "  Humanoid speed: %.2f m/s", humanoid_speed_);
    RCLCPP_INFO(this->get_logger(), "  Vehicular speed: %.2f m/s", vehicular_speed_);
    RCLCPP_INFO(this->get_logger(), "  Waypoints: %zu", waypoints_.size());

    // Initialize state
    current_form_ = "humanoid";  // Default assumption
    current_pose_ = nullptr;
    last_transformation_time_ = this->now();
    mission_mode_ = MissionMode::IDLE;
    current_waypoint_index_ = 0;

    // Create subscribers
    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
      "/odom", 10,
      std::bind(&AutonomousMission::odom_callback, this, std::placeholders::_1));

    transformation_status_sub_ = this->create_subscription<transformer_control::msg::TransformationStatus>(
      "/transformer/status", 10,
      std::bind(&AutonomousMission::transformation_status_callback, this, std::placeholders::_1));

    scan_sub_ = this->create_subscription<sensor_msgs::msg::LaserScan>(
      "/scan", 10,
      std::bind(&AutonomousMission::scan_callback, this, std::placeholders::_1));

    // Create service clients
    transform_to_vehicular_client_ = this->create_client<transformer_control::srv::TransformToVehicular>(
      "/transformer/transform_to_vehicular");
    transform_to_humanoid_client_ = this->create_client<transformer_control::srv::TransformToHumanoid>(
      "/transformer/transform_to_humanoid");

    // Wait for services (non-blocking with warning)
    RCLCPP_INFO(this->get_logger(), "Waiting for transformation services...");
    if (!transform_to_vehicular_client_->wait_for_service(5s))
    {
      RCLCPP_WARN(this->get_logger(), "Transform to vehicular service not available");
    }
    if (!transform_to_humanoid_client_->wait_for_service(5s))
    {
      RCLCPP_WARN(this->get_logger(), "Transform to humanoid service not available");
    }

    // Create mission timer
    mission_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(0.5),  // 2Hz decision making
      std::bind(&AutonomousMission::mission_loop, this));

    RCLCPP_INFO(this->get_logger(), "Autonomous Mission Node initialized");
  }

private:
  // Parameters
  double distance_threshold_;
  double obstacle_range_;
  double humanoid_speed_;
  double vehicular_speed_;
  double goal_tolerance_;
  double transformation_cooldown_;
  std::vector<std::vector<double>> waypoints_;  // Each waypoint is [x, y]

  // State
  MissionMode mission_mode_;
  std::string current_form_;
  rclcpp::Time last_transformation_time_;
  size_t current_waypoint_index_;
  
  // Current sensor data
  nav_msgs::msg::Odometry::SharedPtr current_pose_;
  sensor_msgs::msg::LaserScan::SharedPtr current_scan_;
  std::string current_transformation_state_;
  std::mutex data_mutex_;

  // ROS entities
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Subscription<transformer_control::msg::TransformationStatus>::SharedPtr transformation_status_sub_;
  
  rclcpp::Client<transformer_control::srv::TransformToVehicular>::SharedPtr transform_to_vehicular_client_;
  rclcpp::Client<transformer_control::srv::TransformToHumanoid>::SharedPtr transform_to_humanoid_client_;
  
  rclcpp::TimerBase::SharedPtr mission_timer_;

  // Callback functions
  void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_pose_ = msg;
  }

  void scan_callback(const sensor_msgs::msg::LaserScan::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_scan_ = msg;
  }

  void transformation_status_callback(const transformer_control::msg::TransformationStatus::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_form_ = msg->current_form;
    current_transformation_state_ = msg->transformation_state;
    
    if (msg->transformation_state == "IDLE")
    {
      mission_mode_ = MissionMode::NAVIGATING_TO_GOAL;
    }
    else if (msg->transformation_state == "TRANSFORMING_TO_VEHICULAR" ||
             msg->transformation_state == "TRANSFORMING_TO_HUMANOID")
    {
      mission_mode_ = MissionMode::TRANSFORMING;
    }
  }

  void parse_waypoints(const std::vector<double>& flat_waypoints)
  {
    waypoints_.clear();
    for (size_t i = 0; i < flat_waypoints.size(); i += 2)
    {
      if (i + 1 < flat_waypoints.size())
      {
        waypoints_.push_back({flat_waypoints[i], flat_waypoints[i + 1]});
      }
    }
    
    if (waypoints_.empty())
    {
      // Default mission: a simple square pattern
      waypoints_ = {
        {5.0, 0.0},   // Go 5m forward
        {5.0, 5.0},   // Then 5m right
        {0.0, 5.0},   // Then 5m back
        {0.0, 0.0}    // Return to start
      };
      RCLCPP_INFO(this->get_logger(), "Using default waypoint mission (square pattern)");
    }
  }

  void mission_loop()
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    if (!current_pose_)
    {
      RCLCPP_WARN(this->get_logger(), "No odometry data available yet");
      return;
    }

    if (current_waypoint_index_ >= waypoints_.size())
    {
      if (mission_mode_ != MissionMode::COMPLETED)
      {
        RCLCPP_INFO(this->get_logger(), "Mission completed! All waypoints reached.");
        mission_mode_ = MissionMode::COMPLETED;
      }
      return;
    }

    // Get current position
    double current_x = current_pose_->pose.pose.position.x;
    double current_y = current_pose_->pose.pose.position.y;
    
    // Get target waypoint
    auto& target = waypoints_[current_waypoint_index_];
    double target_x = target[0];
    double target_y = target[1];
    
    // Calculate distance to current waypoint
    double dx = target_x - current_x;
    double dy = target_y - current_y;
    double distance_to_waypoint = std::sqrt(dx * dx + dy * dy);
    
    RCLCPP_DEBUG(this->get_logger(), "Waypoint %zu: (%.2f, %.2f), Distance: %.2f m, Form: %s",
                 current_waypoint_index_ + 1, target_x, target_y, distance_to_waypoint,
                 current_form_.c_str());

    // Check if waypoint reached
    if (distance_to_waypoint <= goal_tolerance_)
    {
      RCLCPP_INFO(this->get_logger(), "Waypoint %zu reached! (%.2f m from target)",
                   current_waypoint_index_ + 1, distance_to_waypoint);
      current_waypoint_index_++;
      return;
    }

    // Determine if we should transform based on conditions
    evaluate_transformation(distance_to_waypoint);
  }

  void evaluate_transformation(double distance_to_waypoint)
  {
    // Don't transform if already transforming or too soon since last transform
    if (mission_mode_ == MissionMode::TRANSFORMING)
    {
      return;
    }

    auto now = this->now();
    double time_since_last_transform = (now - last_transformation_time_).seconds();
    if (time_since_last_transform < transformation_cooldown_)
    {
      return;  // Cooldown period active
    }

    // Check for obstacles if in vehicular form
    bool obstacle_detected = false;
    if (current_form_ == "vehicular" && current_scan_)
    {
      obstacle_detected = detect_obstacle();
      if (obstacle_detected)
      {
        RCLCPP_INFO(this->get_logger(), "Obstacle detected within %.2f m, considering humanoid form",
                    obstacle_range_);
      }
    }

    // Decision logic:
    // 1. If in humanoid form and have far to go with no obstacles → switch to vehicular
    // 2. If in vehicular form and obstacle detected or need precise navigation → switch to humanoid
    
    if (current_form_ == "humanoid")
    {
      // Consider switching to vehicular if:
      // - Distance to waypoint is large (long-distance travel)
      // - No obstacles detected ahead
      // - We're not already close to the goal (where maneuverability matters)
      if (distance_to_waypoint > distance_threshold_ && !obstacle_detected)
      {
        RCLCPP_INFO(this->get_logger(), "Long-distance travel (%.2f m) with clear path - switching to vehicular",
                    distance_to_waypoint);
        request_transformation("vehicular");
      }
      else if (obstacle_detected)
      {
        RCLCPP_DEBUG(this->get_logger(), "Obstacle ahead but in humanoid form - can maneuver around");
      }
    }
    else if (current_form_ == "vehicular")
    {
      // Consider switching to humanoid if:
      // - Obstacle detected within range
      // - Distance to waypoint is small (close to goal, need precision)
      // - Mission involves complex navigation (e.g., sharp turns)
      if (obstacle_detected)
      {
        RCLCPP_INFO(this->get_logger(), "Obstacle detected - switching to humanoid for maneuverability");
        request_transformation("humanoid");
      }
      else if (distance_to_waypoint < distance_threshold_ * 0.5)
      {
        RCLCPP_INFO(this->get_logger(), "Approaching waypoint (%.2f m) - switching to humanoid for precision",
                    distance_to_waypoint);
        request_transformation("humanoid");
      }
    }
  }

  bool detect_obstacle()
  {
    if (!current_scan_)
    {
      return false;
    }

    // Check laser scan for obstacles within range
    // Assumes standard 2D LIDAR with 0 angle forward
    const auto& ranges = current_scan_->ranges;
    double range_min = current_scan_->range_min;
    double range_max = current_scan_->range_max;
    
    // Find minimum distance in forward direction (±15 degrees)
    size_t num_points = ranges.size();
    size_t center_idx = num_points / 2;
    size_t fov_half_width = static_cast<size_t>(num_points * 0.083);  // ~15 degrees each side
    
    double min_distance = range_max;
    for (size_t i = center_idx - fov_half_width; i <= center_idx + fov_half_width; ++i)
    {
      if (i < ranges.size())
      {
        double range = ranges[i];
        if (range >= range_min && range <= range_max && range < min_distance)
        {
          min_distance = range;
        }
      }
    }
    
    bool obstacle_ahead = (min_distance < obstacle_range_ && min_distance < range_max);
    
    if (obstacle_ahead)
    {
      RCLCPP_DEBUG(this->get_logger(), "Obstacle detected at %.2f m", min_distance);
    }
    
    return obstacle_ahead;
  }

  void request_transformation(const std::string& target_form)
  {
    if (target_form == current_form_)
    {
      RCLCPP_DEBUG(this->get_logger(), "Already in %s form, skipping transformation", target_form.c_str());
      return;
    }

    rclcpp::Client<transformer_control::srv::TransformToVehicular>::SharedPtr client;
   auto request = std::make_shared<std::string>();  // Request is empty
    
    if (target_form == "vehicular")
    {
      client = transform_to_vehicular_client_;
      RCLCPP_INFO(this->get_logger(), "Requesting transformation to VEHICULAR form");
    }
    else if (target_form == "humanoid")
    {
      client = transform_to_humanoid_client_;
      RCLCPP_INFO(this->get_logger(), "Requesting transformation to HUMANOID form");
    }
    else
    {
      RCLCPP_ERROR(this->get_logger(), "Unknown target form: %s", target_form.c_str());
      return;
    }

    if (!client)
    {
      RCLCPP_ERROR(this->get_logger(), "Transformation client not initialized");
      return;
    }

    if (!client->service_is_ready())
    {
      RCLCPP_ERROR(this->get_logger(), "Transformation service not ready");
      return;
    }

    auto future = client->async_send_request(request);
    
    // Set callback for response
    future.add_callback(
      [this, target_form](rclcpp::Client<transformer_control::srv::TransformToVehicular>::SharedFuture future)
      {
        try
        {
          auto response = future.get();
          if (response->success)
          {
            RCLCPP_INFO(this->get_logger(), "Transformation to %s accepted: %s",
                        target_form.c_str(), response->message.c_str());
            mission_mode_ = MissionMode::TRANSFORMING;
            last_transformation_time_ = this->now();
          }
          else
          {
            RCLCPP_WARN(this->get_logger(), "Transformation to %s rejected: %s",
                        target_form.c_str(), response->message.c_str());
            mission_mode_ = MissionMode::NAVIGATING_TO_GOAL;
          }
        }
        catch (const std::exception& e)
        {
          RCLCPP_ERROR(this->get_logger(), "Transformation request failed: %s", e.what());
          mission_mode_ = MissionMode::NAVIGATING_TO_GOAL;
        }
      });
  }
};

}  // namespace transformer_control

#include "rclcpp_components/register_node_macro.hpp"

RCLCPP_COMPONENTS_REGISTER_NODE(transformer_control::AutonomousMission)

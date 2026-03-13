#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "rclcpp/lifecycle/lifecycle_publisher.hpp"
#include "rclcpp/lifecycle/lifecycle_subscription.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "transformer_control/msg/transformation_status.hpp"
#include "transformer_control/srv/transform_to_vehicular.hpp"
#include "transformer_control/srv/transform_to_humanoid.hpp"
#include "transformer_control/srv/emergency_stop.hpp"
#include "control_msgs/action/joint_trajectory.hpp"
#include <memory>
#include <string>
#include <map>
#include <atomic>

using namespace std::chrono_literals;

namespace transformer_control
{

enum class TransformationState
{
  IDLE,
  TRANSFORMING_TO_VEHICULAR,
  TRANSFORMING_TO_HUMANOID,
  EMERGENCY_STOP,
  ERROR
};

class TransformationStateMachine : public rclcpp_lifecycle::LifecycleNode
{
public:
  TransformationStateMachine(const rclcpp::NodeOptions & options)
  : LifecycleNode("transformation_state_machine", options)
  {
    // Declare parameters
    this->declare_parameter("humanoid_controller_name", "humanoid_trajectory_controller");
    this->declare_parameter("vehicular_controllers", std::vector<std::string>{
      "wheel_velocity_controller", "arm_head_position_controller"});
    this->declare_parameter("joint_state_broadcaster", "joint_state_broadcaster");
    this->declare_parameter("transformation_duration", 5.0);  // seconds
    this->declare_parameter("current_form", "humanoid");
    this->declare_parameter("safety_check_interval", 0.1);  // seconds

    // Get parameters
    humanoid_controller_name_ = this->get_parameter("humanoid_controller_name").as_string();
    auto vehicular_controllers = this->get_parameter("vehicular_controllers").as_string_array();
    vehicular_controller_names_.assign(vehicular_controllers.begin(), vehicular_controllers.end());
    joint_state_broadcaster_name_ = this->get_parameter("joint_state_broadcaster").as_string();
    transformation_duration_ = this->get_parameter("transformation_duration").as_double();
    safety_check_interval_ = this->get_parameter("safety_check_interval").as_double();
    current_form_ = this->get_parameter("current_form").as_string();

    RCLCPP_INFO(this->get_logger(), "Transformation State Machine initialized");
    RCLCPP_INFO(this->get_logger(), "Humanoid controller: %s", humanoid_controller_name_.c_str());
    RCLCPP_INFO(this->get_logger(), "Vehicular controllers: %s", 
                std::accumulate(vehicular_controller_names_.begin(), vehicular_controller_names_.end(), 
                std::string(), [](std::string a, const std::string& b) { return a.empty() ? b : a + ", " + b; }).c_str());
  }

  ~TransformationStateMachine()
  {
    if (status_pub_ && status_pub_->get_subscription_count() > 0)
    {
      // Publish final status before shutdown
      auto msg = std::make_unique<transformer_control::msg::TransformationStatus>();
      msg->current_form = current_form_;
      msg->transformation_state = "SHUTDOWN";
      msg->progress = 0.0;
      msg->is_safe = true;
      status_pub_->publish(std::move(msg));
    }
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Configuring transformation state machine...");

    // Initialize state
    transformation_state_ = TransformationState::IDLE;
    emergency_stop_flag_ = false;
    trajectory_published_ = false;

    // Create publishers
    status_pub_ = this->create_publisher<transformer_control::msg::TransformationStatus>(
      "transformation_status", 10);

    // Create services
    transform_to_vehicular_srv_ = this->create_service<transformer_control::srv::TransformToVehicular>(
      "transform_to_vehicular", 
      std::bind(&TransformationStateMachine::transform_to_vehicular_callback, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    transform_to_humanoid_srv_ = this->create_service<transformer_control::srv::TransformToHumanoid>(
      "transform_to_humanoid",
      std::bind(&TransformationStateMachine::transform_to_humanoid_callback, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    emergency_stop_srv_ = this->create_service<transformer_control::srv::EmergencyStop>(
      "emergency_stop",
      std::bind(&TransformationStateMachine::emergency_stop_callback, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Create controller management clients
    controller_manager_node_ = rclcpp::Node::make_shared("controller_manager_client");
    controller_manager_client_ = controller_manager_node_->create_client<lifecycle_msgs::srv::ChangeState>(
      "/controller_manager/change_state");
    controller_state_client_ = controller_manager_node_->create_client<lifecycle_msgs::srv::GetState>(
      "/controller_manager/get_state");

    // Wait for controller manager services
    RCLCPP_INFO(this->get_logger(), "Waiting for controller manager services...");
    if (!controller_manager_client_->wait_for_service(5s))
    {
      RCLCPP_ERROR(this->get_logger(), "Controller manager change_state service not available");
      return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::ERROR;
    }
    if (!controller_state_client_->wait_for_service(5s))
    {
      RCLCPP_ERROR(this->get_logger(), "Controller manager get_state service not available");
      return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::ERROR;
    }

    RCLCPP_INFO(this->get_logger(), "Transformation state machine configured successfully");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Activating transformation state machine...");

    // Start safety monitoring timer
    safety_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(safety_check_interval_),
      std::bind(&TransformationStateMachine::safety_check_callback, this));

    // Publish initial status
    publish_status();
    
    RCLCPP_INFO(this->get_logger(), "Transformation state machine active. Current form: %s", 
                current_form_.c_str());
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_deactivate(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Deactivating transformation state machine...");
    
    if (transformation_timer_)
    {
      transformation_timer_->cancel();
      transformation_timer_.reset();
    }
    
    if (safety_timer_)
    {
      safety_timer_->cancel();
      safety_timer_.reset();
    }

    transformation_state_ = TransformationState::IDLE;
    publish_status();
    
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Cleaning up transformation state machine...");

    // Clean up publishers
    status_pub_.reset();
    
    // Clean up services
    transform_to_vehicular_srv_.reset();
    transform_to_humanoid_srv_.reset();
    emergency_stop_srv_.reset();

    // Clean up timers
    if (transformation_timer_)
    {
      transformation_timer_->cancel();
      transformation_timer_.reset();
    }
    if (safety_timer_)
    {
      safety_timer_->cancel();
      safety_timer_.reset();
    }

    // Reset state
    transformation_state_ = TransformationState::IDLE;
    emergency_stop_flag_ = false;
    trajectory_published_ = false;

    RCLCPP_INFO(this->get_logger(), "Transformation state machine cleaned up");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_shutdown(const rclcpp_lifecycle::State &)
  {
    RCLCPP_INFO(this->get_logger(), "Shutting down transformation state machine...");
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }

private:
  // Parameters
  std::string humanoid_controller_name_;
  std::vector<std::string> vehicular_controller_names_;
  std::string joint_state_broadcaster_name_;
  double transformation_duration_;
  double safety_check_interval_;
  std::string current_form_;

  // State
  TransformationState transformation_state_;
  std::atomic<bool> emergency_stop_flag_;
  bool trajectory_published_;

  // ROS entities
  rclcpp::Publisher<transformer_control::msg::TransformationStatus>::SharedPtr status_pub_;
  
  rclcpp::Service<transformer_control::srv::TransformToVehicular>::SharedPtr transform_to_vehicular_srv_;
  rclcpp::Service<transformer_control::srv::TransformToHumanoid>::SharedPtr transform_to_humanoid_srv_;
  rclcpp::Service<transformer_control::srv::EmergencyStop>::SharedPtr emergency_stop_srv_;

  rclcpp::TimerBase::SharedPtr safety_timer_;
  rclcpp::TimerBase::SharedPtr transformation_timer_;

  // Controller manager clients
  rclcpp::Node::SharedPtr controller_manager_node_;
  rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedPtr controller_manager_client_;
  rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr controller_state_client_;

  // Callback functions
  void transform_to_vehicular_callback(
    const std::shared_ptr<transformer_control::srv::TransformToVehicular::Request> request,
    std::shared_ptr<transformer_control::srv::TransformToVehicular::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Received transform_to_vehicular request");
    
    if (emergency_stop_flag_)
    {
      response->success = false;
      response->message = "Emergency stop active. Cannot transform.";
      return;
    }

    if (transformation_state_ != TransformationState::IDLE)
    {
      response->success = false;
      response->message = "Transformation already in progress. Current state: " + 
                          transformation_state_to_string(transformation_state_);
      return;
    }

    if (current_form_ == "vehicular")
    {
      response->success = false;
      response->message = "Already in vehicular form";
      return;
    }

    // Start transformation
    response->success = true;
    response->message = "Starting transformation to vehicular form";
    transformation_state_ = TransformationState::TRANSFORMING_TO_VEHICULAR;
    start_transformation("vehicular");
  }

  void transform_to_humanoid_callback(
    const std::shared_ptr<transformer_control::srv::TransformToHumanoid::Request> request,
    std::shared_ptr<transformer_control::srv::TransformToHumanoid::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Received transform_to_humanoid request");
    
    if (emergency_stop_flag_)
    {
      response->success = false;
      response->message = "Emergency stop active. Cannot transform.";
      return;
    }

    if (transformation_state_ != TransformationState::IDLE)
    {
      response->success = false;
      response->message = "Transformation already in progress. Current state: " + 
                          transformation_state_to_string(transformation_state_);
      return;
    }

    if (current_form_ == "humanoid")
    {
      response->success = false;
      response->message = "Already in humanoid form";
      return;
    }

    // Start transformation
    response->success = true;
    response->message = "Starting transformation to humanoid form";
    transformation_state_ = TransformationState::TRANSFORMING_TO_HUMANOID;
    start_transformation("humanoid");
  }

  void emergency_stop_callback(
    const std::shared_ptr<transformer_control::srv::EmergencyStop::Request> request,
    std::shared_ptr<transformer_control::srv::EmergencyStop::Response> response)
  {
    RCLCPP_WARN(this->get_logger(), "EMERGENCY STOP TRIGGERED: %s", 
                request->reason.empty() ? "No reason given" : request->reason.c_str());
    
    emergency_stop_flag_ = true;
    
    if (transformation_timer_)
    {
      transformation_timer_->cancel();
      transformation_timer_.reset();
    }
    
    transformation_state_ = TransformationState::EMERGENCY_STOP;
    
    response->success = true;
    response->message = "Emergency stop activated. Transformation halted.";
    
    publish_status();
  }

  void start_transformation(const std::string& target_form)
  {
    RCLCPP_INFO(this->get_logger(), "Beginning transformation from %s to %s", 
                current_form_.c_str(), target_form.c_str());
    
    // 1. Deactivate current form's controllers
    if (!deactivate_current_controllers())
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to deactivate current controllers");
      transformation_state_ = TransformationState::ERROR;
      publish_status();
      return;
    }

    // 2. Activate joint state broadcaster if not already
    if (!ensure_controller_active(joint_state_broadcaster_name_))
    {
      RCLCPP_ERROR(this->get_logger(), "Failed to activate joint state broadcaster");
      transformation_state_ = TransformationState::ERROR;
      publish_status();
      return;
    }

    // 3. Start transformation progression
    transformation_start_time_ = this->now();
    transformation_progress_ = 0.0;
    trajectory_published_ = false;

    // Create timer for transformation progression
    transformation_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(0.1),  // 10Hz update
      std::bind(&TransformationStateMachine::transformation_timer_callback, this));
    
    publish_status();
  }

  bool deactivate_current_controllers()
  {
    RCLCPP_INFO(this->get_logger(), "Deactivating current form controllers...");
    
    if (current_form_ == "humanoid")
    {
      // Deactivate humanoid controller
      if (!deactivate_controller(humanoid_controller_name_))
      {
        return false;
      }
    }
    else if (current_form_ == "vehicular")
    {
      // Deactivate all vehicular controllers
      for (const auto& controller : vehicular_controller_names_)
      {
        if (!deactivate_controller(controller))
        {
          return false;
        }
      }
    }
    
    return true;
  }

  bool deactivate_controller(const std::string& controller_name)
  {
    auto request = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
    request->transition.id = lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE;
    
    auto future = controller_manager_client_->async_send_request(request);
    
    // Wait for response
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

  bool ensure_controller_active(const std::string& controller_name)
  {
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

  void transformation_timer_callback()
  {
    if (emergency_stop_flag_)
    {
      if (transformation_timer_)
      {
        transformation_timer_->cancel();
        transformation_timer_.reset();
      }
      return;
    }

    // Update progress
    auto now = this->now();
    auto elapsed = (now - transformation_start_time_).seconds();
    transformation_progress_ = std::min(elapsed / transformation_duration_, 1.0);

    RCLCPP_DEBUG(this->get_logger(), "Transformation progress: %.2f%%", 
                 transformation_progress_ * 100.0);

    // Check if transformation is complete
    if (transformation_progress_ >= 1.0)
    {
      transformation_complete();
      return;
    }

    // Safety check during transformation
    if (!perform_safety_check())
    {
      RCLCPP_ERROR(this->get_logger(), "Safety check failed during transformation");
      transformation_state_ = TransformationState::ERROR;
      if (transformation_timer_)
      {
        transformation_timer_->cancel();
        transformation_timer_.reset();
      }
    }

    publish_status();
  }

  void transformation_complete()
  {
    if (transformation_timer_)
    {
      transformation_timer_->cancel();
      transformation_timer_.reset();
    }

    // Determine final state based on transformation direction
    if (transformation_state_ == TransformationState::TRANSFORMING_TO_VEHICULAR)
    {
      current_form_ = "vehicular";
      if (!activate_target_controllers(vehicular_controller_names_))
      {
        RCLCPP_ERROR(this->get_logger(), "Failed to activate vehicular controllers");
        transformation_state_ = TransformationState::ERROR;
      }
      else
      {
        transformation_state_ = TransformationState::IDLE;
        RCLCPP_INFO(this->get_logger(), "Successfully transformed to vehicular form");
      }
    }
    else if (transformation_state_ == TransformationState::TRANSFORMING_TO_HUMANOID)
    {
      current_form_ = "humanoid";
      if (!activate_target_controllers({humanoid_controller_name_}))
      {
        RCLCPP_ERROR(this->get_logger(), "Failed to activate humanoid controller");
        transformation_state_ = TransformationState::ERROR;
      }
      else
      {
        transformation_state_ = TransformationState::IDLE;
        RCLCPP_INFO(this->get_logger(), "Successfully transformed to humanoid form");
      }
    }

    transformation_progress_ = 1.0;
    publish_status();
  }

  bool activate_target_controllers(const std::vector<std::string>& controllers)
  {
    RCLCPP_INFO(this->get_logger(), "Activating target controllers...");
    
    for (const auto& controller : controllers)
    {
      if (!ensure_controller_active(controller))
      {
        RCLCPP_ERROR(this->get_logger(), "Failed to activate controller: %s", 
                     controller.c_str());
        return false;
      }
    }
    
    RCLCPP_INFO(this->get_logger(), "All target controllers activated");
    return true;
  }

  void safety_check_callback()
  {
    // Periodic safety check
    if (!perform_safety_check())
    {
      RCLCPP_WARN(this->get_logger(), "Safety check failed in periodic check");
      // Don't set to error state here, let transformation timer handle it during transformation
    }
  }

  bool perform_safety_check()
  {
    // Check joint limits (would need actual joint state data)
    // This is a stub - in real implementation would read from /joint_states
    
    // Check for emergency stop
    if (emergency_stop_flag_)
    {
      RCLCPP_WARN(this->get_logger(), "Safety check: Emergency stop is active");
      return false;
    }

    // Check if in error state
    if (transformation_state_ == TransformationState::ERROR)
    {
      return false;
    }

    // Add more safety checks:
    // - Joint position limits
    // - Self-collision detection
    // - Sensor故障检测
    // - Power status
    
    return true;
  }

  void publish_status()
  {
    auto msg = std::make_unique<transformer_control::msg::TransformationStatus>();
    msg->current_form = current_form_;
    msg->transformation_state = transformation_state_to_string(transformation_state_);
    msg->progress = transformation_progress_;
    msg->is_safe = perform_safety_check();
    
    status_pub_->publish(std::move(msg));
  }

  std::string transformation_state_to_string(TransformationState state)
  {
    switch (state)
    {
      case TransformationState::IDLE:
        return "IDLE";
      case TransformationState::TRANSFORMING_TO_VEHICULAR:
        return "TRANSFORMING_TO_VEHICULAR";
      case TransformationState::TRANSFORMING_TO_HUMANOID:
        return "TRANSFORMING_TO_HUMANOID";
      case TransformationState::EMERGENCY_STOP:
        return "EMERGENCY_STOP";
      case TransformationState::ERROR:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
  }
};

}  // namespace transformer_control

#include "rclcpp_components/register_node_macro.hpp"

// Register the component
RCLCPP_COMPONENTS_REGISTER_NODE(transformer_control::TransformationStateMachine)

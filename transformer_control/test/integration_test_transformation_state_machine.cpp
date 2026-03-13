#include <gtest/gtest.h>
#include <launch_testing/launch_test.hpp>
#include <launch_testing_utils/assert_helpers.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/client.hpp>
#include <rclcpp/service.hpp>
#include <lifecycle_msgs/srv/change_state.hpp>
#include <transformer_control/srv/transform_to_vehicular.hpp>
#include <transformer_control/srv/transform_to_humanoid.hpp>
#include <transformer_control/msg/transformation_status.hpp>
#include <memory>
#include <string>

using namespace std::chrono_literals;

class TransformationStateMachineIntegrationTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }

  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("test_integration");
    
    // Create clients for services
    transform_to_vehicular_client_ = node_->create_client<transformer_control::srv::TransformToVehicular>(
      "/transformation_state_machine/transform_to_vehicular");
    transform_to_humanoid_client_ = node_->create_client<transformer_control::srv::TransformToHumanoid>(
      "/transformation_state_machine/transform_to_humanoid");
    
    // Create subscription for status
    status_sub_ = node_->create_subscription<transformer_control::msg::TransformationStatus>(
      "/transformation_state_machine/transformation_status",
      10,
      [this](const transformer_control::msg::TransformationStatus::SharedPtr msg) {
        last_status_ = *msg;
        status_received_ = true;
      });
  }

  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::Client<transformer_control::srv::TransformToVehicular>::SharedPtr transform_to_vehicular_client_;
  rclcpp::Client<transformer_control::srv::TransformToHumanoid>::SharedPtr transform_to_humanoid_client_;
  rclcpp::Subscription<transformer_control::msg::TransformationStatus>::SharedPtr status_sub_;
  
  transformer_control::msg::TransformationStatus last_status_;
  bool status_received_ = false;
};

TEST_F(TransformationStateMachineIntegrationTest, ServiceAvailability)
{
  // Wait for services to become available
  ASSERT_TRUE(transform_to_vehicular_client_->wait_for_service(5s));
  ASSERT_TRUE(transform_to_humanoid_client_->wait_for_service(5s));
}

TEST_F(TransformationStateMachineIntegrationTest, InitialState)
{
  // Wait for initial status
  auto start = std::chrono::steady_clock::now();
  while (!status_received_ && 
         std::chrono::steady_clock::now() - start < 5s) {
    rclcpp::spin_some(node_);
    std::this_thread::sleep_for(50ms);
  }
  
  ASSERT_TRUE(status_received_);
  EXPECT_EQ(last_status_.current_form, "humanoid");  // Default form
  EXPECT_EQ(last_status_.transformation_state, "IDLE");
  EXPECT_FLOAT_EQ(last_status_.progress, 0.0);
  EXPECT_TRUE(last_status_.is_safe);
}

TEST_F(TransformationStateMachineIntegrationTest, TransformToVehicular)
{
  // Request transformation to vehicular
  auto request = std::make_shared<transformer_control::srv::TransformToVehicular::Request>();
  auto future = transform_to_vehicular_client_->async_send_request(request);
  
  // Wait for response
  auto result = rclcpp::spin_until_future_complete(node_, future, 5s);
  ASSERT_EQ(result, rclcpp::FutureReturnCode::SUCCESS);
  
  auto response = future.get();
  EXPECT_TRUE(response->success);
  
  // Wait for transformation to start
  auto start = std::chrono::steady_clock::now();
  while (last_status_.transformation_state != "TRANSFORMING_TO_VEHICULAR" &&
         std::chrono::steady_clock::now() - start < 2s) {
    rclcpp::spin_some(node_);
    std::this_thread::sleep_for(50ms);
  }
  
  ASSERT_EQ(last_status_.transformation_state, "TRANSFORMING_TO_VEHICULAR");
  EXPECT_NE(last_status_.progress, 0.0);
  
  // Wait for transformation to complete (default duration is 5s)
  start = std::chrono::steady_clock::now();
  while (last_status_.transformation_state != "IDLE" &&
         std::chrono::steady_clock::now() - start < 10s) {
    rclcpp::spin_some(node_);
    std::this_thread::sleep_for(50ms);
  }
  
  ASSERT_EQ(last_status_.transformation_state, "IDLE");
  EXPECT_EQ(last_status_.current_form, "vehicular");
  EXPECT_FLOAT_EQ(last_status_.progress, 1.0);
}

TEST_F(TransformationStateMachineIntegrationTest, TransformToHumanoid)
{
  // First ensure we're in vehicular form
  {
    auto request = std::make_shared<transformer_control::srv::TransformToVehicular::Request>();
    auto future = transform_to_vehicular_client_->async_send_request(request);
    rclcpp::spin_until_future_complete(node_, future, 5s);
    
    auto start = std::chrono::steady_clock::now();
    while (last_status_.current_form != "vehicular" &&
           std::chrono::steady_clock::now() - start < 10s) {
      rclcpp::spin_some(node_);
      std::this_thread::sleep_for(50ms);
    }
    EXPECT_EQ(last_status_.current_form, "vehicular");
  }
  
  // Now transform back to humanoid
  auto request = std::make_shared<transformer_control::srv::TransformToHumanoid::Request>();
  auto future = transform_to_humanoid_client_->async_send_request(request);
  
  auto result = rclcpp::spin_until_future_complete(node_, future, 5s);
  ASSERT_EQ(result, rclcpp::FutureReturnCode::SUCCESS);
  
  auto response = future.get();
  EXPECT_TRUE(response->success);
  
  // Wait for completion
  auto start = std::chrono::steady_clock::now();
  while (last_status_.transformation_state != "IDLE" &&
         last_status_.current_form != "humanoid" &&
         std::chrono::steady_clock::now() - start < 10s) {
    rclcpp::spin_some(node_);
    std::this_thread::sleep_for(50ms);
  }
  
  ASSERT_EQ(last_status_.current_form, "humanoid");
  EXPECT_EQ(last_status_.transformation_state, "IDLE");
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  
  // Check if we should run integration test with a launch file
  // This test can be run standalone or through launch_testing
  
  return RUN_ALL_TESTS();
}

cmake_minimum_required(VERSION 3.8)
project(transformer_control)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(control_msgs REQUIRED)
find_package(ros2_control REQUIRED)
find_package(ros2_controllers REQUIRED)
find_package(pluginlib REQUIRED)
find_package(transformer_description REQUIRED)

# TODO: Add executables when source files are created
# add_executable(transformation_state_machine src/transformation_state_machine.cpp)
# ament_target_dependencies(transformation_state_machine
#   rclcpp
#   rclcpp_lifecycle
#   std_msgs
#   std_srvs
#   sensor_msgs
#   trajectory_msgs
#   control_msgs
#   ros2_control
#   ros2_controllers
#   pluginlib
#   transformer_description
# )

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}/
)

# install(TARGETS
#   transformation_state_machine
#   transformation_controller
#   safety_monitor
#   autonomous_mission
#   DESTINATION lib/${PROJECT_NAME}
# )

ament_export_dependencies(
  rclcpp
  rclcpp_lifecycle
  std_msgs
  std_srvs
  sensor_msgs
  trajectory_msgs
  control_msgs
  ros2_control
  ros2_controllers
  pluginlib
  transformer_description
)

ament_package()

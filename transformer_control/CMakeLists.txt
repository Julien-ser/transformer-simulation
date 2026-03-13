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
find_package(rclcpp_action REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(control_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(ros2_controllers REQUIRED)
find_package(pluginlib REQUIRED)
find_package(transformer_description REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# Add test dependencies
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(gtest REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
endif()

# Generate custom messages and services
rosidl_generate_interfaces(${PROJECT_NAME}
  msg/TransformationStatus.msg
  srv/TransformToVehicular.srv
  srv/TransformToHumanoid.srv
  srv/EmergencyStop.srv
  DEPENDENCIES std_msgs std_srvs
)

# Transformation state machine executable
add_executable(transformation_state_machine src/transformation_state_machine.cpp)
ament_target_dependencies(transformation_state_machine
  rclcpp
  rclcpp_lifecycle
  std_msgs
  std_srvs
  trajectory_msgs
  control_msgs
  ros2_control
  ros2_controllers
  transformer_description
)
rosidl_target_interfaces(transformation_state_machine
  ${PROJECT_NAME} "rosidl_typesupport_cpp"
)

# Transformation controller executable
add_executable(transformation_controller src/transformation_controller.cpp)
target_include_directories(transformation_controller PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(transformation_controller
  rclcpp
  rclcpp_action
  rclcpp_components
  lifecycle_msgs
  sensor_msgs
  trajectory_msgs
  control_msgs
  transformer_description
)
rosidl_target_interfaces(transformation_controller
  ${PROJECT_NAME} "rosidl_typesupport_cpp"
)

# Safety monitor executable
add_executable(safety_monitor src/safety_monitor.cpp)
target_include_directories(safety_monitor PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(safety_monitor
  rclcpp
  rclcpp_lifecycle
  std_msgs
  std_srvs
  sensor_msgs
  transformer_description
)
rosidl_target_interfaces(safety_monitor
  ${PROJECT_NAME} "rosidl_typesupport_cpp"
)

# Autonomous mission executable
add_executable(autonomous_mission src/autonomous_mission.cpp)
target_include_directories(autonomous_mission PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(autonomous_mission
  rclcpp
  std_msgs
  std_srvs
  sensor_msgs
  nav_msgs
  trajectory_msgs
  control_msgs
  transformer_description
)
rosidl_target_interfaces(autonomous_mission
  ${PROJECT_NAME} "rosidl_typesupport_cpp"
)

# Unit tests for transformation state machine
if(BUILD_TESTING)
  ament_add_gtest(test_transformation_state_machine
    test/transformation_state_machine_test.cpp
    TIMEOUT 30
  )
  ament_target_dependencies(test_transformation_state_machine
    rclcpp
    rclcpp_lifecycle
    std_msgs
    std_srvs
    control_msgs
    transformer_description
  )
  target_include_directories(test_transformation_state_machine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
endif()

# Install executables
install(TARGETS
  transformation_state_machine
  transformation_controller
  safety_monitor
  autonomous_mission
  DESTINATION lib/${PROJECT_NAME}
)

# Install Python test monitor script
install(PROGRAMS
  src/test_monitor.py
  DESTINATION lib/${PROJECT_NAME}
)

ament_export_dependencies(
  rclcpp
  rclcpp_lifecycle
  rclcpp_action
  rclcpp_components
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

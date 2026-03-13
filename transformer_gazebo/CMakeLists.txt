cmake_minimum_required(VERSION 3.8)
project(transformer_gazebo)

find_package(ament_cmake REQUIRED)

# Install launch files, worlds, models, and config
install(
  DIRECTORY launch worlds models config
  DESTINATION share/${PROJECT_NAME}/
)

ament_package()

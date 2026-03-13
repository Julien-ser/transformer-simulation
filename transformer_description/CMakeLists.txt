cmake_minimum_required(VERSION 3.8)
project(transformer_description)

find_package(ament_cmake REQUIRED)

# Install URDF, meshes, rviz config, and launch files
install(
  DIRECTORY urdf meshes rviz launch
  DESTINATION share/${PROJECT_NAME}/
)

ament_package()

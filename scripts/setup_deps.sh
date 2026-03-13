#!/bin/bash

# Transformer Simulation - Dependency Installation Script
# Installs and configures ROS2 Humble and Gazebo dependencies

set -e  # Exit on error

echo "=========================================="
echo "Transformer Simulation - Dependency Setup"
echo "=========================================="
echo ""

# Check if running on Ubuntu
if [[ ! -f /etc/os-release ]] || ! grep -q "Ubuntu" /etc/os-release; then
    echo "ERROR: This script is designed for Ubuntu systems"
    exit 1
fi

# Check ROS2 environment
if ! command -v ros2 &> /dev/null; then
    echo "ERROR: ROS2 Humble not found. Please install ROS2 Humble first:"
    echo "  sudo apt update && sudo apt install -y ros-humble-ros-base"
    exit 1
fi

echo "✓ ROS2 detected: $(ros2 --version)"
echo ""

# Update package lists
echo "Updating apt package lists..."
sudo apt update -qq
echo ""

# Install core dependencies
echo "Installing core ROS2 simulation dependencies..."
sudo apt install -y \
    ros-humble-gazebo-* \
    ros-humble-ros2-control \
    ros-humble-ros2-controllers \
    ros-humble-joint-state-publisher \
    ros-humble-robot-state-publisher \
    ros-humble-rviz2 \
    gazebo11 \
    libgazebo11-dev \
    ros-humble-xacro \
    ros-humble-robot-localization \
    ros-humble-nav2-bringup \
    ros-humble-nav2-common \
    ros-humble-tf2-ros \
    ros-humble-ros2-controller-manager \
    ros-humble-ros2-control-test-assets \
    python3-pip \
    python3-colcon-common-extensions

echo ""
echo "✓ Core dependencies installed"
echo ""

# Optional: Install development tools
echo "Installing development tools..."
sudo apt install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    vim \
    ros-humble-ament-lint

echo ""
echo "✓ Development tools installed"
echo ""

# Verify installation
echo "Verifying installed ROS2 packages..."
echo ""

REQUIRED_PACKAGES=(
    "ros2_control"
    "ros2_controllers"
    "gazebo_ros"
    "joint_state_publisher"
    "robot_state_publisher"
    "rviz2"
    "xacro"
)

missing=0
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ros2 pkg list | grep -q "^${pkg}$"; then
        echo "✓ ${pkg}"
    else
        echo "✗ ${pkg} - NOT FOUND"
        missing=$((missing + 1))
    fi
done

echo ""
if [ $missing -eq 0 ]; then
    echo "=========================================="
    echo "✓ All required packages are installed!"
    echo "=========================================="
else
    echo "=========================================="
   	echo "⚠ ${missing} package(s) missing or not found"
    echo "=========================================="
   	exit 1
fi

echo ""
echo "Dependency setup complete!"
echo ""
echo "Next steps:"
echo "  1. Create ROS2 workspace: mkdir -p ~/ros2_ws/src/transformer_sim"
echo "  2. Copy these packages to ~/ros2_ws/src/transformer_sim"
echo "  3. Build: cd ~/ros2_ws && colcon build"
echo "  4. Source: source install/setup.bash"
echo ""

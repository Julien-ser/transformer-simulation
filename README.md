# Transformer Simulation

A ROS2-based robot simulation featuring a transformer-style robot that can morph between humanoid and vehicular configurations in Gazebo.

## Mission

Create a realistic simulation of a robot that transforms between humanoid and vehicular forms, complete with ROS2 control loops, autonomous mission logic, and comprehensive testing scenarios.

## Current Status

**Phase 1: Planning & Environment Setup** ✅ Complete

- ✅ Task 1.1: Technical architecture defined ([docs/architecture.md](docs/architecture.md))
- ✅ Task 1.2: ROS2 workspace structure setup
- ✅ Task 1.3: Dependencies installation script ([scripts/setup_deps.sh](scripts/setup_deps.sh))
- ✅ Task 1.4: Initial setup instructions (this document)

**Phase 2: World & Environment Configuration** ✅ Complete

- ✅ Task 2.1: Basic Gazebo world file created ([transformer_gazebo/worlds/transformer_world.world](transformer_gazebo/worlds/transformer_world.world))
- ✅ Task 2.2: Environment obstacles and test structures added
  - Created models: ramp, platform, wall, obstacle_course
  - Located in: `transformer_gazebo/models/`
  - Integrated into world file for transformation testing
- ✅ Task 2.3: Sensor placement configuration
  - LIDAR, cameras, and IMU sensors placed in world
  - Located in: `transformer_gazebo/config/sensors/`
- ✅ Task 2.4: Demo scenario spawn points documented
  - Configuration in `config/spawn_points.yaml`
  - Coordinates for humanoid and vehicular forms defined

**Phase 3: Robot Modeling & URDF Development** ✅ Complete

- ✅ Task 3.1: Humanoid URDF design complete ([transformer_description/urdf/humanoid.urdf](transformer_description/urdf/humanoid.urdf))
  - 17 degrees of freedom: torso (3), head (2), arms (3 each), legs (3 each)
  - Visual and collision elements with STL references
  - ROS2 control transmissions configured
- ✅ Task 3.2: Vehicular URDF design complete ([transformer_description/urdf/vehicular.urdf](transformer_description/urdf/vehicular.urdf))
  - 6 degrees of freedom: 4 wheel joints (continuous rotation) + head (pan/tilt)
  - Compact body geometry with folded arm configuration
  - Wheel transmissions using VelocityJointInterface for drive control
- ✅ Task 3.3: Unified URDF with transformation definitions complete ([transformer_description/urdf/transformer_complete.urdf.xacro](transformer_description/urdf/transformer_complete.urdf.xacro))
  - Single xacro file with conditional form selection via `form` argument
  - Shared components: base_link, head, arms, materials, transmission macros
  - Humanoid-specific: torso and leg macros with 9+ DOF
  - Vehicular-specific: body and wheels with 4 continuous joints
  - Usage: `ros2 run xacro xacro transformer_complete.urdf.xacro form:=humanoid`
- ✅ Task 3.4: Build 3D mesh assets (STL/Collada)
  - All mesh files created in `transformer_description/meshes/`
  - Optimized triangle counts (<500 per mesh, well under 5k limit)
  - Copyright headers included on all mesh files
  - Components: torso_base, torso_mid, torso_top, head_base, head_dome, shoulder_joint, upper_arm, lower_arm, hip_joint, upper_leg, lower_leg

## Architecture

### Technology Stack

- **ROS2 Distribution:** Humble Hawksbill (LTS) - stable with long-term support
- **Simulation Framework:** Gazebo Classic (Fortress) - mature integration with ros2_control
- **Robot Control:** ros2_control + ros2_controllers
- **Robot Description:** URDF/Xacro with modular component architecture

### Core Packages

1. **transformer_gazebo** - Gazebo world, models, and launch configuration
2. **transformer_control** - Transformation state machine, safety monitor, autonomous mission logic
3. **transformer_description** - URDF models, mesh assets, RViz configuration

### Transformation Mechanism

The robot operates as a finite state machine:

- **HUMANOID**: 15+ degree-of-freedom walking form (head, arms, legs)
- **VEHICULAR**: 6 DOF wheeled form (steering + drive wheels)
- **TRANSFORMING**: Intermediate state executing joint trajectory interpolation

Transformation is triggered autonomously based on mission objectives or manually via services:
- Switch to vehicular for long-distance travel (>5m)
- Switch to humanoid for obstacle negotiation or precision tasks

## Prerequisites

### System Requirements

- Ubuntu 22.04 (Jammy)
- ROS2 Humble Hawksbill
- Gazebo 11 (classic)
- Git, CMake, build-essential, Python3

### Install Dependencies

```bash
# Source ROS2
source /opt/ros/humble/setup.bash

# Create workspace
mkdir -p ~/ros2_ws/src/transformer_sim
cd ~/ros2_ws

# Install system dependencies (run scripts/setup_deps.sh when available)
sudo apt update
sudo apt install -y \
  ros-humble-gazebo-* \
  ros-humble-ros2-control \
  ros-humble-ros2-controllers \
  ros-humble-joint-state-publisher \
  ros-humble-robot-state-publisher \
  ros-humble-rviz2 \
  gazebo \
  libgazebo11-dev
```

## Building the Project

```bash
cd ~/ros2_ws
colcon build --packages-select transformer_gazebo transformer_control transformer_description
source install/setup.bash
```

## Running the Simulation

### Basic Launch

```bash
# Launch with humanoid form (default)
ros2 launch transformer_gazebo transformer_simulation.launch.py

# Launch directly in vehicular form
ros2 launch transformer_gazebo transformer_simulation.launch.py initial_form:=vehicular
```

### Autonomous Mission Demo

```bash
# Run test scenario: humanoid → vehicular → humanoid cycle
ros2 launch transformer_gazebo test_transformation_scenario.launch.py
```

### Manual Control

```bash
# Trigger transformation to humanoid
ros2 service call /transformer/transform_to_humanoid std_srvs/srv/Trigger {}

# Trigger transformation to vehicular
ros2 service call /transformer/transform_to_vehicular std_srvs/srv/Trigger {}

# Check current state
ros2 topic echo /transformer/state

# Monitor transformation progress
ros2 topic echo /transformer/progress
```

## Topics

| Topic | Message Type | Description |
|-------|--------------|-------------|
| `/joint_states` | sensor_msgs/msg/JointState | Current joint positions, velocities, efforts |
| `/transformer/state` | std_msgs/msg/String | Current form: `HUMANOID`, `VEHICULAR`, `TRANSFORMING` |
| `/transformer/progress` | std_msgs/msg/Float32 | Transformation progress 0.0-1.0 |
| `/mission/decision` | std_msgs/msg/String | Autonomous decision: `WALK`, `DRIVE`, `TRANSFORM_H2V`, `TRANSFORM_V2H` |

## Services

| Service | Type | Purpose |
|---------|------|---------|
| `/transformer/transform_to_humanoid` | std_srvs/srv/Trigger | Request humanoid configuration |
| `/transformer/transform_to_vehicular` | std_srvs/srv/Trigger | Request vehicular configuration |
| `/transformer/cancel` | std_srvs/srv/Trigger | Cancel ongoing transformation |
| `/safety/emergency_stop` | std_srvs/srv/Trigger | Immediate stop and freeze |

## Project Structure

```
~/ros2_ws/src/transformer_sim/
├── transformer_gazebo/
│   ├── launch/
│   ├── worlds/
│   ├── models/
│   └── config/
├── transformer_control/
│   ├── src/
│   ├── config/
│   └── launch/
├── transformer_description/
│   ├── urdf/
│   ├── meshes/
│   ├── rviz/
│   └── launch/
└── scripts/
    ├── setup_deps.sh
    ├── build.sh
    └── run_simulation.sh
```

## Expected Behavior

### Humanoid Mode
- Robot stands upright on two legs
- 15+ controllable joints: head (pan/tilt), arms (shoulder, elbow), legs (hip, knee, ankle)
- Can walk on flat terrain (inverse kinematics simulation)
- Compact footprint,低速运动

### Vehicular Mode
- Robot transforms to low-profile car-like form
- Limbs retract into body
- 4 wheels with steering (front) and drive (all)
- Optimized for speed and stability on flat surfaces
- Fast movement, tight turning radius

### Transformation Process (~6 seconds)
1. **Safety check**: Verify stationary, validate joint limits, collision detection
2. **Sequential actuation**: Joint trajectories interpolate all moving parts
3. **Transition state**: Intermediate poses avoid self-collision
4. **Configuration swap**: Lock unused joints, enable appropriate controllers
5. **Validation**: All joints at target, publish status

### Autonomous Mission Demo
The test scenario demonstrates complete functionality:
1. Spawns robot as humanoid at starting position
2. Humanoid walks ~5m to waypoint
3. Transforms to vehicular form
4. Drives ~10m to destination
5. Transforms back to humanoid
6. Completes final approach to goal

## Environment & Obstacles

The simulation world includes various obstacles to test transformation capabilities:

- **Ramp** (3m long, incline ~5.7°): Tests climbing ability for both vehicular and humanoid forms
- **Platform** (2m×2m, elevated 1m): Requires climbing or transformation to access elevated surfaces
- **Wall** (2m high): Barrier that tests navigation, obstacle avoidance, and transformation for overcoming
- **Obstacle Course**: Combined challenges including:
  - Central wall obstacle
  - Two-step platform progression (0.3m each step)
  - Narrow elevated platform (1m×0.4m)
  - Slope element for incline testing

The obstacles are positioned to create a comprehensive test environment that requires different robot configurations for optimal navigation.

## Documentation

- **Architecture**: [docs/architecture.md](docs/architecture.md) - Complete technical specifications
- **ROS2 Control**: [ros2_control documentation](http://control.ros.org/master/index.html)
- **Gazebo**: [Gazebo Classic documentation](http://gazebosim.org/docs/classic)

## Contributing

This project follows ROS2 coding standards:
- C++: Follow ROS2 style guide (clang-format)
- Python: PEP 8 with ROS2 modifications
- URDF/Xacro: Validate with `check_urdf`
- Launch files: Python with proper argparse

## Troubleshooting

### Controller Errors
```bash
# Check controller manager status
ros2 control list_controllers

# Reload controller configuration
ros2 control load_start_controller joint_trajectory_controller
```

### Gazebo Physics Issues
```bash
# Reset simulation
ros2 service call /gazebo/reset_world std_srvs/srv/Empty {}
```

### Joint State Not Publishing
```bash
# Verify robot description loaded
rosparam get /robot_description | head -n 20

# Check state broadcaster
ros2 control list_controllers | grep joint_state
```

## License

TBD - Project in development.

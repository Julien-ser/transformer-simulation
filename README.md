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

**Phase 4: ROS2 Control & Transformation Logic** ✅ Complete

- ✅ Task 4.1: Implement ROS2 control configuration for both robot forms ([transformer_control/config/](transformer_control/config/))
  - Separate controller configurations: `humanoid_controllers.yaml` and `vehicular_controllers.yaml`
  - Joint state broadcaster always active
  - Humanoid: single trajectory controller for all 15+ joints
  - Vehicular: wheel velocity controller + position controller for arms/head
- ✅ **Task 4.2: Develop transformation state machine node** ([src/transformation_state_machine.cpp](transformer_control/src/transformation_state_machine.cpp))
  - ROS2 lifecycle node with proper state management
  - Transformation states: IDLE, TRANSFORMING_TO_VEHICULAR, TRANSFORMING_TO_HUMANOID, EMERGENCY_STOP, ERROR
  - ROS2 services: `transform_to_vehicular`, `transform_to_humanoid`, `emergency_stop`
  - Controller lifecycle management (deactivate current, activate target)
  - Joint locking/releasing through controller switching
  - Safety checks with periodic monitoring
  - Status publisher on `/transformation_status` topic
  - Comprehensive unit and integration tests ([test/](transformer_control/test/))
- ✅ **Task 4.3: Create transformation execution controller** ([src/transformation_controller.cpp](transformer_control/src/transformation_controller.cpp))
  - Spline-based trajectory generation with collision avoidance and joint limit checking
  - Multi-controller coordination for complex morphing sequences
  - Interpolation with collision risk assessment
  - Real-time trajectory execution monitoring
- ✅ **Task 4.4: Implement safety monitoring node** ([src/safety_monitor.cpp](transformer_control/src/safety_monitor.cpp))
  - ROS2 lifecycle node with continuous safety oversight
  - Joint limit validation against URDF-defined boundaries
  - Self-collision detection with geometric heuristics
  - Sensor health monitoring with timeout detection
  - Emergency stop service integration
  - Safety status publishing (`/safety_status`, `/safety_level`, `/safety_violation`)
  - Configurable check rates and thresholds via parameters

**Phase 5: Integration & Autonomous Loop** 🔄 In Progress (3/4 complete)

- [x] **Task 5.1:** Build unified launch file ([launch/transformer_simulation.launch.py](transformer_gazebo/launch/transformer_simulation.launch.py))
  - Unified launch with arguments for initial form, world selection, and controller configuration
  - Supports RViz integration via `rviz:=true` argument
- [x] **Task 5.2:** Create autonomous demonstration node ([src/autonomous_mission.cpp](transformer_control/src/autonomous_mission.cpp))
  - Autonomous decision-making for transformation based on mission objectives
  - Waypoint navigation with distance-based form selection
  - Obstacle detection using LIDAR for real-time transformation decisions
- [x] **Task 5.3:** Develop RViz configuration for visualization ([rviz/transformer_visualization.rviz](transformer_description/rviz/transformer_visualization.rviz))
  - Complete visualization setup with robot model display
  - Separate joint state displays for humanoid and vehicular forms (toggleable)
  - TF tree visualization showing all coordinate frames
  - Real-time transformation status and safety monitoring panels
  - Waypoint markers display for autonomous mission tracking
- [ ] Task 5.4: Create comprehensive end-to-end test scenario

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

The autonomous mission node (`autonomous_mission`) demonstrates intelligent form selection based on mission objectives and environmental conditions.

```bash
# Launch simulation with autonomous mission enabled (default)
ros2 launch transformer_gazebo transformer_simulation.launch.py

# Or launch directly with specific initial form
ros2 launch transformer_gazebo transformer_simulation.launch.py initial_form:=humanoid
```

#### Autonomous Decision Logic

The node continuously evaluates whether to transform based on:

- **Humanoid → Vehicular**: Switches when distance to waypoint > 5.0m AND no obstacles detected within 3.0m
  - Optimal for long-distance travel with clear paths
  - Vehicular form provides 4x speed (2.0 m/s vs 0.5 m/s)

- **Vehicular → Humanoid**: Switches when:
  - Obstacle detected within 3.0m (maneuverability needed)
  - Or approaching waypoint (< 2.5m) for precision navigation

#### Mission Parameters

Configure via ROS2 parameters (can be passed as launch arguments or set via command line):

| Parameter | Default | Description |
|-----------|---------|-------------|
| `distance_threshold` | 5.0 | Switch to vehicular for distances greater than this (meters) |
| `obstacle_range` | 3.0 | Consider obstacles within this range for transformation (meters) |
| `humanoid_speed` | 0.5 | Assumed humanoid movement speed (m/s) |
| `vehicular_speed` | 2.0 | Assumed vehicular movement speed (m/s) |
| `goal_tolerance` | 0.5 | Distance within which waypoint is considered reached (meters) |
| `transformation_cooldown` | 10.0 | Minimum time between transformations (seconds) |
| `mission_waypoints` | [] | Optional flat array of waypoints [x1, y1, x2, y2, ...] |

**Example with custom parameters:**

```bash
ros2 launch transformer_gazebo transformer_simulation.launch.py \
  distance_threshold:=7.0 \
  obstacle_range:=4.0 \
  mission_waypoints:="[10.0, 0.0, 10.0, 10.0, 0.0, 10.0, 0.0, 0.0]"
```

#### Default Mission

If no waypoints are provided, the autonomous mission follows a square pattern:
1. (0,0) → (5,0) - 5m forward
2. (5,0) → (5,5) - 5m right
3. (5,5) → (0,5) - 5m back
4. (0,5) → (0,0) - 5m left (return to start)

#### Monitoring Mission Progress

```bash
# View mission status and decisions
ros2 topic echo /transformer/status

# Monitor waypoint progress
ros2 topic echo /odom | grep pose

# Watch transformation decisions
ros2 log --include autonomous_mission

# Check current active form
echo "Current form: $(ros2 topic echo -n 1 /transformer/status | grep current_form)"
```

#### Expected Behavior

1. **Start**: Robot spawns in specified form (default: humanoid)
2. **Navigation**: Autonomous mission node begins waypoint navigation
3. **Form Selection**: Node decides when to transform based on distance and obstacles
4. **Transformation**: State machine executes smooth morphing sequence (~5-6 seconds)
5. **Completion**: All waypoints reached → mission complete

The node demonstrates practical autonomous behavior: using humanoid form for precise navigation and obstacle negotiation, vehicular form for efficient long-distance travel.

### RViz Visualization

The simulation includes a comprehensive RViz configuration for monitoring robot state, transformation progress, and safety status.

```bash
# Launch simulation with RViz enabled
ros2 launch transformer_gazebo transformer_simulation.launch.py rviz:=true

# Or with specific initial form
ros2 launch transformer_gazebo transformer_simulation.launch.py initial_form:=vehicular rviz:=true
```

#### RViz Displays

The default configuration (`transformer_description/rviz/transformer_visualization.rviz`) includes:

**Robot Visualization:**
- **Robot Model** - Complete 3D visualization of the robot in its current form
- **TF** - Coordinate frame tree showing all transforms (toggled via "Show Names", "Show Axes")
- **Joint State Display (Humanoid)** - Visual joint frames for humanoid joints (15 DOF: torso, head, arms, legs)
- **Joint State Display (Vehicular)** - Visual joint frames for vehicular joints (4 wheels + steering + arms/head)

**Information Panels:**
- **Transformation Status** - Live updates on transformation state (IDLE, TRANSFORMING, etc.) and progress percentage
- **Safety Status** - Boolean safety flag (green=safe, red=unsafe)
- **Safety Level** - Current safety level: NORMAL, WARNING, CRITICAL, EMERGENCY
- **Waypoints** - 3D markers showing mission waypoints when using autonomous mission

#### Display Groups

To manage the two forms efficiently:
- Disable **Joint State Display (Humanoid)** when in vehicular form to reduce clutter
- Disable **Joint State Display (Vehicular)** when in humanoid form
- Both displays are present by default, use checkboxes in the "Displays" panel to toggle

#### Monitoring Transformation

During transformation:
1. Watch the **Transformation Status** panel for state changes
2. Observe joint positions moving via the colored joint frames
3. Check **Safety Level** - should remain NORMAL during safe transformations
4. Robot model will smoothly morph between configurations

### RViz Configuration File

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
| `/transformation_status` | transformer_control/msg/TransformationStatus | Transformation state, progress, and safety status |
| `/safety_status` | std_msgs/msg/Bool | Overall safety status (true=safe, false=unsafe) |
| `/safety_level` | std_msgs/msg/String | Current safety level: NORMAL, WARNING, CRITICAL, EMERGENCY |
| `/safety_violation` | std_msgs/msg/String | Detailed safety violation messages |
| `/tf` | tf2_msgs/msg/TFMessage | Robot coordinate transforms |
| `/robot_description` | std_msgs/msg/String | URDF robot model |

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
│   │   ├── transformation_state_machine.cpp
│   │   ├── transformation_controller.cpp
│   │   ├── safety_monitor.cpp
│   │   └── autonomous_mission.cpp
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

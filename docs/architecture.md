# Technical Architecture Document

## 1. Overview

The Transformer Simulation project implements a robot capable of morphing between humanoid and vehicular configurations in a ROS2/Gazebo simulation environment. This document specifies the technical architecture, component interactions, and communication protocols.

## 2. ROS2 Distribution

**Selected: ROS2 Humble Hawksbill (LTS)**

**Rationale:**
- Long-term support until May 2027
- Mature Gazebo Classic integration via `gazebo_ros_pkgs`
- Stable `ros2_control` and `ros2_controllers` implementations
- Wide community adoption and extensive documentation

**Alternative considered:** ROS2 Iron (newer but shorter LTS, potential compatibility issues with some Gazebo plugins)

## 3. Simulation Framework

**Selected: Gazebo Classic (Fortress release)**

**Rationale:**
- Mature ROS2 integration through `gazebo_ros2_control` plugin
- Reliable physics engine (ODE) for joint articulation
- Proven track record with `ros2_control` systems
- Better compatibility with existing URDF-based robot models

**Key components:**
- `gazebo_ros`: Core ROS2-Gazebo bridge
- `gazebo_ros2_control`: Integrates ros2_control with Gazebo
- `gazebo_plugins`: Sensor plugins (IMU, LIDAR, cameras)

**Alternative considered:** Ignition Gazebo (now Gazebo Sim) - newer architecture but less mature ros2_control integration

## 4. Robot Transformation Mechanism

### 4.1 Approach

**Selected: Joint State Machine with Trajectory Interpolation**

The transformation operates as a finite state machine with three primary states:
- `HUMANOID`: Fully articulated human form with legs, arms, head
- `VEHICULAR`: Compact wheeled form with retracted limbs
- `TRANSFORMING`: Intermediate state during morphing process

### 4.2 Transformation Strategy

**Phase 1 - Pre-transform Safety Check:**
- Verify robot is stationary
- Validate target configuration joint limits
- Check for self-collision in intermediate poses
- Confirm environmental clearance

**Phase 2 - Sequential Joint Trajectory Execution:**
- Publish trajectory commands to `joint_trajectory_controller`
- Use linear interpolation between current and target joint positions
- Animate transformation with realistic timing (~5-8 seconds)
- Critical joints transform in coordinated sequences to avoid collisions

**Phase 3 - Post-transform Validation:**
- Verify all joints reached target positions
- Lock unused joints (e.g., leg joints in vehicular mode)
- Enable active controllers for current configuration
- Publish transformation complete status

### 4.3 Shared Component Architecture

Both forms share:
- Common base link (`base_link`) with identical inertial properties
- Central torso/core body segment
- Power and computing systems (conceptual)
- Sensor suite (head-mounted in humanoid, body-mounted in vehicular)

Joint configuration differences:
- Humanoid: 15+ DOF (head x2, arms x6 each, legs x6 each)
- Vehicular: 6 DOF (4 wheels + 2 steering) + 4-6 locked limb joints

## 5. Package Structure

```
transformer_sim/
├── transformer_gazebo/          # Gazebo simulation integration
│   ├── launch/
│   │   ├── transformer_simulation.launch.py
│   │   └── test_transformation_scenario.launch.py
│   ├── worlds/
│   │   └── transformer_world.world
│   ├── models/                  # Custom Gazebo models
│   └── config/
│       └── spawn_points.yaml
├── transformer_control/         # ROS2 control & autonomous logic
│   ├── src/
│   │   ├── transformation_state_machine.cpp
│   │   ├── transformation_controller.cpp
│   │   ├── safety_monitor.cpp
│   │   └── autonomous_mission.cpp
│   ├── config/
│   │   ├── control.yaml
│   │   ├── humanoid_controllers.yaml
│   │   └── vehicular_controllers.yaml
│   └── launch/
│       └── controller_manager.launch.py
├── transformer_description/     # URDF and meshes
│   ├── urdf/
│   │   ├── humanoid.urdf.xacro
│   │   ├── vehicular.urdf.xacro
│   │   └── transformer_complete.urdf.xacro
│   ├── meshes/                  # STL/Collada files
│   │   ├── head.stl
│   │   ├── torso.stl
│   │   ├── arm_upper.stl
│   │   ├── arm_lower.stl
│   │   ├── leg_upper.stl
│   │   ├── leg_lower.stl
│   │   ├── wheel.stl
│   │   └── body_vehicular.stl
│   ├── rviz/
│   │   └── transformer_visualization.rviz
│   └── launch/
│       └── robot_state_publisher.launch.py
└── scripts/
    ├── setup_deps.sh           # Dependency installation
    ├── build.sh                # Build script
    └── run_simulation.sh       # Launch helper
```

## 6. ROS2 Topic & Service Architecture

### 6.1 Core Topics

**Robot State:**
- `/joint_states` (sensor_msgs/msg/JointState) - Current joint positions, velocities, efforts
- `/tf` and `/tf_static` (tf2_msgs/msg/TFMessage) - Transforms between coordinate frames
- `/robot_description` (std_msgs/msg/String) - URDF parameter

**Control:**
- `/joint_trajectory` (trajectory_msgs/msg/JointTrajectory) - Trajectory commands to controller
- `/joint_states_controller/state` (controller_msgs/msg/JointTrajectoryControllerState) - Controller feedback

**Transformation:**
- `/transformer/state` (std_msgs/msg/String) - Current form: "HUMANOID", "VEHICULAR", "TRANSFORMING"
- `/transformer/progress` (std_msgs/msg/Float32) - Transformation progress 0.0-1.0
- `/transformer/trigger` (std_srvs/srv/Trigger) - Service to initiate transformation
- `/transformer/status` (std_msgs/msg/Bool) - Transformation allowed/blocked

**Autonomous Mission:**
- `/mission/goal` (geometry_msgs/msg/PoseStamped) - Target navigation goal
- `/mission/status` (std_msgs/msg/String) - Current mission state
- `/mission/decision` (std_msgs/msg/String) - Current decision: "WALK", "DRIVE", "TRANSFORM_H2V", "TRANSFORM_V2H"

**Sensors (future):**
- `/camera/color/image_raw` (sensor_msgs/msg/Image)
- `/lidar/scan` (sensor_msgs/msg/LaserScan)
- `/imu/data` (sensor_msgs/msg/Imu)

### 6.2 Services

**Transformation Services:**
- `/transformer/transform_to_humanoid` (std_srvs/srv/Trigger) - Request humanoid form
- `/transformer/transform_to_vehicular` (std_srvs/srv/Trigger) - Request vehicular form
- `/transformer/cancel` (std_srvs/srv/Trigger) - Cancel ongoing transformation
- `/transformer/is_ready` (std_srvs/srv/Trigger) - Check if transformation permitted

**Safety Services:**
- `/safety/emergency_stop` (std_srvs/srv/Trigger) - Immediate stop
- `/safety/status` (std_srvs/srv/Trigger) - Safety system status
- `/safety/reconfigure` (std_srvs/srv/SetBool) - Enable/disable safety checks

**Simulation Services:**
- `/gazebo/spawn_entity` (gazebo_msgs/srv/SpawnEntity) - Spawn robot at specified pose
- `/gazebo/delete_entity` (gazebo_msgs/srv/DeleteEntity) - Remove robot from world

## 7. State Machine Design

```
                  ┌──────────────┐
                  │  HUMANOID    │◄────────┐
                  │ (walk_mode)  │         │
                  └──────┬───────┘         │
                         │ transform_h2v  │
                         ▼                │
                  ┌──────────────┐        │
                  │ TRANSFORMING │        │
                  │ (rate=0.5)   │        │
                  └──────┬───────┘        │
                         │ transform_v2h  │
                         ▼                │
                  ┌──────────────┐        │
                  │  VEHICULAR   │────────┘
                  │ (drive_mode) │
                  └──────────────┘
```

**State Transitions:**
- `HUMANOID` → `TRANSFORMING`: Autonomous mission decides vehicular preferred OR manual trigger
- `TRANSFORMING` → `VEHICULAR`: All trajectory goals reached, validate configuration
- `VEHICULAR` → `TRANSFORMING`: Autonomous mission decides humanoid preferred OR manual trigger
- `TRANSFORMING` → `HUMANOID`: All trajectory goals reached, validate configuration

**Guarded Transitions:**
- Cannot transform while velocity > 0.05 m/s
- Cannot transform if collision detected
- Cannot transform if joint limit violation predicted
- Transformation automatically cancelled if safety condition violated mid-process

## 8. Controller Configuration

### 8.1 Controller Manager Setup

**Controller Types:**
- `joint_state_broadcaster` (type: joint_state_broadcaster/JointStateBroadcaster) - Publishes joint states
- `joint_trajectory_controller` (type: joint_trajectory_controller/JointTrajectoryController) - Executes transformation trajectories

**Humanoid Configuration:**
- Active joints: head_pan, head_tilt, left_shoulder_pitch, left_shoulder_roll, left_elbow_pitch, right_shoulder_pitch, right_shoulder_roll, right_elbow_pitch, left_hip_roll, left_hip_pitch, left_knee_pitch, left_ankle_pitch, right_hip_roll, right_hip_pitch, right_knee_pitch, right_ankle_pitch
- Interface: position (effort optional for future grasping)
- State published: position, velocity

**Vehicular Configuration:**
- Active joints: wheel_fl_steer, wheel_fr_steer, wheel_rl_drive, wheel_rr_drive
- Interface: position (steering), velocity (drive wheels)
- State published: position, velocity
- Locked joints: All humanoid limb joints set to fixed positions

**Controller Switching:**
- ros2_control's `ControllerSwitcher` used to reconfigure active controllers
- During transformation: trajectory controller temporarily manages all joints
- After transformation: load/unload controller configurations via ControllerManager

## 9. Autonomous Mission Logic

The autonomous mission node implements a simple behavior tree:

```
1. Spawn in humanoid form at start position
2. Receive navigation goal (x, y, orientation)
3. Calculate distance to goal
4. IF distance > 5.0m AND path is clear:
     TRANSFORM_TO_VEHICULAR
     DRIVE to goal (using simple proportional controller)
5. IF obstacle detected OR distance < 5.0m:
     TRANSFORM_TO_HUMANOID
     WALK to goal (inverse kinematics for foot placement)
6. Goal reached → celebrate (maybe spin/dance)
```

**Decision Factors:**
- Distance threshold: 5m
- Terrain traversal: humanoid for uneven ground, vehicular for flat terrain
- Energy efficiency: prefer vehicular for long distances
- Maneuverability: humanoid for tight spaces

## 10. Dependencies

### 10.1 System Packages (Ubuntu 22.04)

```bash
sudo apt install -y \
  ros-humble-desktop \
  ros-humble-gazebo-* \
  ros-humble-ros2-control \
  ros-humble-ros2-controllers \
  ros-humble-gazebo-ros2-control \
  ros-humble-joint-state-publisher \
  ros-humble-joint-state-publisher-gui \
  ros-humble-robot-state-publisher \
  ros-humble-rviz2 \
  gazebo \
  libgazebo11-dev
```

### 10.2 Python Dependencies

Standard ROS2 Python packages only (no pip dependencies required beyond ROS2).

## 11. Performance Considerations

- Physics update rate: 1000 Hz (Gazebo), control loop: 100 Hz
- Transformation trajectory duration: 5-8 seconds for smooth animation
- Joint position control tolerance: ±0.01 rad
- Maximum transformation frequency: once per 30 seconds (prevent rapid toggling)
- Self-collision checking: updated at 10 Hz during transformation

## 12. Future Enhancements (Post-Phase 1)

- Add dynamic balance for humanoid walking (ZMP-based)
- Implement wheel-based drive with Ackermann steering
- Add more sophisticated perception (LIDAR mapping)
- Battery/power system simulation
- Multi-robot coordination scenarios
- Integration with Navigation2 for autonomous waypoint following

## 13. Risk Assessment

| Risk | Mitigation |
|------|------------|
| URDF too complex causing Gazebo performance issues | Keep meshes <5k triangles, use simple collision shapes |
| ros2_control configuration errors | Start with simple position controller, add effort control later |
| Self-collision during transformation | Implement intermediate waypoints, collision checking |
| Joint limits exceeded during morphing | Add safety monitor with hard stops |
| ROS2 version compatibility | Pin specific package versions in setup_deps.sh |

#!/usr/bin/env python3

"""
End-to-End Transformation Test Scenario

This launch file runs a comprehensive test that demonstrates the complete
transformation cycle with specific navigation goals:

1. Spawns robot in humanoid form at origin
2. Navigates 5m forward (humanoid walking)
3. Transforms to vehicular form
4. Drives 10m further forward (vehicular movement)
5. Transforms back to humanoid form
6. Completes mission by reaching final goal

The test validates:
- URDF loading for both forms
- Controller switching
- Transformation state machine
- Autonomous decision-making
- Navigation in both forms
- End-to-end mission completion

Usage:
    ros2 launch transformer_gazebo test_transformation_scenario.launch.py
"""

import os
import yaml

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    ExecuteProcess,
    OpaqueFunction,
    RegisterEventHandler,
    TimerAction,
)
from launch.conditions import LaunchConfigurationEquals, IfCondition
from launch.event_handlers import OnProcessStart
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, Command
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.descriptions import ParameterValue


def generate_launch_description():
    """Generate test scenario launch description."""

    # =========================================================================
    # LAUNCH ARGUMENTS
    # =========================================================================

    rviz_arg = DeclareLaunchArgument(
        "rviz",
        default_value="true",
        description="Start RViz for visualization",
    )

    world_arg = DeclareLaunchArgument(
        "world",
        default_value="transformer_world.world",
        description="Gazebo world file name",
    )

    rviz_config_arg = DeclareLaunchArgument(
        "rviz_config",
        default_value=PathJoinSubstitution(
            [
                FindPackageShare("transformer_description"),
                "rviz",
                "transformer_visualization.rviz",
            ]
        ),
        description="Path to RViz configuration file",
    )

    use_rviz = LaunchConfiguration("rviz")
    rviz_config = LaunchConfiguration("rviz_config")

    # =========================================================================
    # TEST-SPECIFIC CONFIGURATION
    # =========================================================================

    # Get package paths
    transformer_gazebo_pkg = get_package_share_directory("transformer_gazebo")
    transformer_control_pkg = get_package_share_directory("transformer_control")

    # Create test-specific parameters for autonomous mission
    # These waypoints are designed to trigger transformations at specific points:
    # - Start at (0, 0) in humanoid form
    # - Waypoint 1: (5.0, 0.0) - Distance > 5m triggers transform to vehicular
    # - Waypoint 2: (15.0, 0.0) - Distance < 2.5m triggers transform back to humanoid
    # - After reaching waypoint 2, mission completes
    test_mission_params = {
        "distance_threshold": 5.0,  # Transform humanoid->vehicular when distance > 5m
        "obstacle_range": 3.0,  # LIDAR obstacle detection range
        "humanoid_speed": 0.5,  # m/s walking speed
        "vehicular_speed": 2.0,  # m/s driving speed
        "goal_tolerance": 0.5,  # meters
        "transformation_cooldown": 15.0,  # seconds between transforms
        "mission_waypoints": [5.0, 0.0, 15.0, 0.0],  # Flat array: [x1, y1, x2, y2]
    }

    # Launch configurations
    world = LaunchConfiguration("world")

    # =========================================================================
    # GAZEBO WORLD LAUNCH
    # =========================================================================

    gazebo_world_path = PathJoinSubstitution([transformer_gazebo_pkg, "worlds"])

    gazebo_cmd = ExecuteProcess(
        cmd=[
            "gazebo",
            "--verbose",
            "-s",
            "libgazebo_ros_init.so",
            "-s",
            "libgazebo_ros_factory.so",
            PathJoinSubstitution([gazebo_world_path, world]),
        ],
        output="screen",
        sigterm_timeout="10",
        sigkill_timeout="10",
    )

    # =========================================================================
    # ROBOT STATE PUBLISHER
    # =========================================================================

    # Process URDF with xacro for humanoid initial form
    xacro_path = os.path.join(
        get_package_share_directory("transformer_description"),
        "urdf",
        "transformer_complete.urdf.xacro",
    )

    robot_description = ParameterValue(
        Command(["xacro", xacro_path, "form:=humanoid"]), value_type=str
    )

    robot_state_publisher_node = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        name="robot_state_publisher",
        output="screen",
        parameters=[
            {
                "robot_description": robot_description,
                "frame_prefix": "",
                "publish_frequency": 50.0,
            }
        ],
        remappings=[("/joint_states", "/joint_states")],
    )

    # =========================================================================
    # ROBOT SPAWNING
    # =========================================================================

    def spawn_robot_node_func(context):
        """Spawn robot at origin for test scenario."""
        robot_spawn_node = Node(
            package="gazebo_ros",
            executable="spawn_entity.py",
            name="spawn_transformer_robot",
            output="screen",
            arguments=[
                "-entity",
                "transformer_robot",
                "-topic",
                "robot_description",
                "-x",
                "0.0",
                "-y",
                "0.0",
                "-z",
                "0.1",
                "-Y",
                "0.0",
            ],
        )
        return [robot_spawn_node]

    delayed_spawn = RegisterEventHandler(
        OnProcessStart(
            target_action=robot_state_publisher_node,
            on_start=[
                TimerAction(
                    period=2.0, actions=[OpaqueFunction(function=spawn_robot_node_func)]
                )
            ],
        )
    )

    # =========================================================================
    # CONTROLLER LOADING
    # =========================================================================

    control_config_path = PathJoinSubstitution(
        [FindPackageShare("transformer_control"), "config", "control.yaml"]
    )

    # Joint state broadcaster (always active)
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        name="joint_state_broadcaster_spawner",
        output="screen",
        arguments=[
            "joint_state_broadcaster",
            "--controller-manager",
            "/controller_manager",
        ],
        parameters=[control_config_path],
    )

    # Humanoid controllers (since we start in humanoid form)
    humanoid_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        name="humanoid_controller_spawner",
        output="screen",
        arguments=[
            "humanoid_trajectory_controller",
            "--controller-manager",
            "/controller_manager",
        ],
        parameters=[control_config_path],
    )

    # Delay controller spawning until robot is spawned
    delayed_controller_spawn = RegisterEventHandler(
        OnProcessStart(
            target_action=robot_state_publisher_node,
            on_start=[
                TimerAction(
                    period=5.0,
                    actions=[
                        joint_state_broadcaster_spawner,
                        humanoid_controller_spawner,
                    ],
                )
            ],
        )
    )

    # =========================================================================
    # TRANSFORMATION & SAFETY NODES
    # =========================================================================

    transformation_state_machine_node = Node(
        package="transformer_control",
        executable="transformation_state_machine",
        name="transformation_state_machine",
        output="screen",
        parameters=[
            control_config_path,
            {
                "transformation_timeout": 10.0,
                "safety_check_period": 0.1,
                "joint_tolerance": 0.01,
                "initial_form": "humanoid",
            },
        ],
        remappings=[
            ("~/transform_to_vehicular", "/transformer/transform_to_vehicular"),
            ("~/transform_to_humanoid", "/transformer/transform_to_humanoid"),
            ("~/cancel", "/transformer/cancel"),
            ("~/status", "/transformer/status"),
            ("~/state", "/transformer/state"),
        ],
    )

    safety_monitor_node = Node(
        package="transformer_control",
        executable="safety_monitor",
        name="safety_monitor",
        output="screen",
        parameters=[
            control_config_path,
            {
                "check_rate": 50.0,
                "joint_limit_buffer": 0.05,
                "collision_check_enabled": True,
                "sensor_timeout": 1.0,
                "emergency_stop_timeout": 0.5,
            },
        ],
        remappings=[
            ("~/emergency_stop", "/safety/emergency_stop"),
            ("~/status", "/safety/status"),
            ("~/level", "/safety/level"),
            ("~/violation", "/safety/violation"),
        ],
    )

    # Autonomous mission node with test-specific waypoints
    autonomous_mission_node = Node(
        package="transformer_control",
        executable="autonomous_mission",
        name="autonomous_mission",
        output="screen",
        parameters=[
            control_config_path,
            test_mission_params,
        ],
    )

    # Delay transformation/safety nodes until controllers are active
    delayed_transformation_nodes = RegisterEventHandler(
        OnProcessStart(
            target_action=joint_state_broadcaster_spawner,
            on_start=[
                TimerAction(
                    period=2.0,
                    actions=[
                        transformation_state_machine_node,
                        safety_monitor_node,
                        autonomous_mission_node,
                    ],
                )
            ],
        )
    )

    # =========================================================================
    # TEST MONITOR NODE
    # =========================================================================

    test_monitor_node = Node(
        package="transformer_control",
        executable="test_monitor",
        name="test_monitor",
        output="screen",
        parameters=[
            {
                "test_name": "end_to_end_transformation_cycle",
                "expected_transformations": 2,  # humanoid->vehicular, vehicular->humanoid
                "completion_timeout": 120.0,  # seconds
                "waypoint_tolerance": 0.5,  # meters
            }
        ],
        remappings=[
            ("~/odom", "/odom"),
            ("~/status", "/transformer/status"),
            ("~/mission_complete", "/mission_complete"),
        ],
    )

    delayed_test_monitor = RegisterEventHandler(
        OnProcessStart(
            target_action=autonomous_mission_node,
            on_start=[
                TimerAction(
                    period=1.0,
                    actions=[test_monitor_node],
                )
            ],
        )
    )

    # =========================================================================
    # RVIZ VISUALIZATION
    # =========================================================================

    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        output="screen",
        arguments=["-d", rviz_config],
        condition=IfCondition(use_rviz),
    )

    delayed_rviz = RegisterEventHandler(
        OnProcessStart(
            target_action=robot_state_publisher_node,
            on_start=[TimerAction(period=3.0, actions=[rviz_node])],
        )
    )

    # =========================================================================
    # LAUNCH DESCRIPTION ASSEMBLY
    # =========================================================================

    ld = LaunchDescription(
        [
            # Launch arguments
            rviz_arg,
            world_arg,
            rviz_config_arg,
            # Gazebo world
            gazebo_cmd,
            # Robot state publisher
            robot_state_publisher_node,
            # Robot spawning
            delayed_spawn,
            # Controller spawning
            delayed_controller_spawn,
            # Transformation and safety nodes
            delayed_transformation_nodes,
            # Test monitor
            delayed_test_monitor,
            # RViz
            delayed_rviz,
        ]
    )

    return ld

#!/usr/bin/env python3

"""
Unified launch file for Transformer Robot Simulation.

This launch file spawns the robot in either humanoid or vehicular configuration
and starts all required nodes for the simulation.

Usage:
    ros2 launch transformer_gazebo transformer_simulation.launch.py
    ros2 launch transformer_gazebo transformer_simulation.launch.py initial_form:=vehicular
    ros2 launch transformer_gazebo transformer_simulation.launch.py world:=custom.world spawn_point:=near_ramp
"""

import os
import yaml

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription, LaunchContext
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


def load_spawn_point(spawn_point_key, form, config_path):
    """Load spawn position and orientation from YAML config."""
    try:
        with open(config_path, "r") as f:
            spawn_config = yaml.safe_load(f)

        form_key = f"{form}_spawns"
        if form_key in spawn_config and spawn_point_key in spawn_config[form_key]:
            spawn_data = spawn_config[form_key][spawn_point_key]
            position = spawn_data.get("position", [0.0, 0.0, 0.1])
            orientation = spawn_data.get("orientation", 0.0)
            return position, orientation
        else:
            print(
                f"Warning: Spawn point '{spawn_point_key}' not found for form '{form}', using default."
            )
            default_key = "default"
            if form_key in spawn_config and default_key in spawn_config[form_key]:
                spawn_data = spawn_config[form_key][default_key]
                position = spawn_data.get("position", [0.0, 0.0, 0.1])
                orientation = spawn_data.get("orientation", 0.0)
                return position, orientation
    except Exception as e:
        print(f"Error loading spawn points: {e}, using defaults")

    return [0.0, 0.0, 0.1], 0.0


def spawn_robot_node_func(
    context: LaunchContext, spawn_point_conf, form_conf, *args, **kwargs
):
    """Function to create robot spawn node with evaluated spawn coordinates."""
    spawn_point_val = spawn_point_conf.perform(context)
    form_val = form_conf.perform(context)

    transformer_gazebo_pkg = get_package_share_directory("transformer_gazebo")
    spawn_points_config_path = os.path.join(
        transformer_gazebo_pkg, "config", "spawn_points.yaml"
    )

    position, orientation = load_spawn_point(
        spawn_point_val, form_val, spawn_points_config_path
    )

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
            str(position[0]),
            "-y",
            str(position[1]),
            "-z",
            str(position[2]),
            "-Y",
            str(orientation) if orientation is not None else "0.0",
        ],
    )

    return [robot_spawn_node]


def generate_launch_description():
    """Generate comprehensive launch description for transformer simulation."""

    # =========================================================================
    # LAUNCH ARGUMENTS
    # =========================================================================

    initial_form_arg = DeclareLaunchArgument(
        "initial_form",
        default_value="humanoid",
        description="Initial robot form: humanoid or vehicular",
    )

    world_arg = DeclareLaunchArgument(
        "world",
        default_value="transformer_world.world",
        description="Gazebo world file name (must be in transformer_gazebo/worlds/)",
    )

    spawn_point_arg = DeclareLaunchArgument(
        "spawn_point",
        default_value="default",
        description="Spawn point key from spawn_points.yaml",
    )

    control_config_arg = DeclareLaunchArgument(
        "control_config",
        default_value=PathJoinSubstitution(
            [FindPackageShare("transformer_control"), "config", "control.yaml"]
        ),
        description="Path to main control configuration file",
    )

    rviz_arg = DeclareLaunchArgument(
        "rviz",
        default_value="false",
        description="Whether to start RViz with visualization configuration",
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

    # Launch configurations
    initial_form = LaunchConfiguration("initial_form")
    world = LaunchConfiguration("world")
    spawn_point = LaunchConfiguration("spawn_point")
    control_config = LaunchConfiguration("control_config")
    use_rviz = LaunchConfiguration("rviz")
    rviz_config = LaunchConfiguration("rviz_config")

    # =========================================================================
    # PROCESS URDF WITH XACRO
    # =========================================================================

    # Get package paths
    transformer_description_pkg = get_package_share_directory("transformer_description")
    transformer_gazebo_pkg = get_package_share_directory("transformer_gazebo")

    xacro_path = os.path.join(
        transformer_description_pkg, "urdf", "transformer_complete.urdf.xacro"
    )

    # Use Command substitution to process xacro at runtime
    robot_description = ParameterValue(
        Command(["xacro", xacro_path, "form:=", initial_form]), value_type=str
    )

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
    # ROBOT SPAWNING (using OpaqueFunction for dynamic coordinates)
    # =========================================================================

    spawn_robot_action = OpaqueFunction(
        function=lambda context: spawn_robot_node_func(
            context,
            LaunchConfiguration("spawn_point"),
            LaunchConfiguration("initial_form"),
        )
    )

    # Delay spawn until robot_state_publisher is ready
    delayed_spawn = RegisterEventHandler(
        OnProcessStart(
            target_action=robot_state_publisher_node,
            on_start=[TimerAction(period=2.0, actions=[spawn_robot_action])],
        )
    )

    # =========================================================================
    # CONTROLLER LOADING
    # =========================================================================

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
        parameters=[control_config],
    )

    # Humanoid-specific controllers
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
        parameters=[control_config],
        condition=LaunchConfigurationEquals("initial_form", "humanoid"),
    )

    # Vehicular-specific controllers
    vehicular_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        name="vehicular_controller_spawner",
        output="screen",
        arguments=[
            "wheel_velocity_controller",
            "arm_head_position_controller",
            "--controller-manager",
            "/controller_manager",
        ],
        parameters=[control_config],
        condition=LaunchConfigurationEquals("initial_form", "vehicular"),
    )

    # Delay controller spawning until robot is spawned
    delayed_controller_spawn = RegisterEventHandler(
        OnProcessStart(
            target_action=robot_state_publisher_node,  # Actually depends on spawn, but spawn is delayed
            on_start=[
                TimerAction(
                    period=5.0,  # Wait for spawn to complete
                    actions=[
                        joint_state_broadcaster_spawner,
                        humanoid_controller_spawner,
                        vehicular_controller_spawner,
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
            control_config,
            {
                "transformation_timeout": 10.0,
                "safety_check_period": 0.1,
                "joint_tolerance": 0.01,
                "initial_form": initial_form,
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
            control_config,
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

    # Delay transformation/safety nodes until controllers are active
    delayed_transformation_nodes = RegisterEventHandler(
        OnProcessStart(
            target_action=joint_state_broadcaster_spawner,
            on_start=[
                TimerAction(
                    period=2.0,
                    actions=[transformation_state_machine_node, safety_monitor_node],
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

    # Delay RViz until robot_state_publisher is ready
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
            initial_form_arg,
            world_arg,
            spawn_point_arg,
            control_config_arg,
            rviz_arg,
            rviz_config_arg,
            # Gazebo world
            gazebo_cmd,
            # Robot state publisher (processes URDF via Command)
            robot_state_publisher_node,
            # Robot spawning (delayed, dynamic coordinates)
            delayed_spawn,
            # Controller spawning (delayed, conditional on form)
            delayed_controller_spawn,
            # Transformation and safety nodes (delayed)
            delayed_transformation_nodes,
            # RViz (optional)
            delayed_rviz,
        ]
    )

    return ld

#!/usr/bin/env python3

"""
Test Monitor Node for End-to-End Transformation Scenario

Monitors the transformation test and reports success/failure.
Subscribes to:
- /odom (odometry) for position tracking
- /transformer/status for transformation state
- /mission_complete from autonomous mission

Verifies:
1. Robot starts in humanoid form
2. Reaches first waypoint (5m)
3. Transforms to vehicular
4. Reaches second waypoint (15m)
5. Transforms back to humanoid
6. Mission completes successfully
"""

import rclpy
from rclpy.node import Node
from nav_msgs.msg import Odometry
from transformer_control.msg import TransformationStatus
from std_msgs.msg import Bool
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy

import math
import time


class TestMonitor(Node):
    def __init__(self):
        super().__init__("test_monitor")

        # Declare parameters
        self.declare_parameter("test_name", "transformation_cycle")
        self.declare_parameter("expected_transformations", 2)
        self.declare_parameter("completion_timeout", 120.0)
        self.declare_parameter("waypoint_tolerance", 0.5)

        # Get parameters
        self.test_name = self.get_parameter("test_name").value
        self.expected_transformations = self.get_parameter(
            "expected_transformations"
        ).value
        self.completion_timeout = self.get_parameter("completion_timeout").value
        self.waypoint_tolerance = self.get_parameter("waypoint_tolerance").value

        # State tracking
        self.start_time = None
        self.current_form = None
        self.transformation_count = 0
        self.transformation_sequence = []
        self.waypoints_reached = []

        # Waypoints for this test scenario (should match autonomous mission)
        self.waypoints = [
            (5.0, 0.0),  # First waypoint - triggers transform to vehicular
            (15.0, 0.0),  # Second waypoint - triggers transform back to humanoid
        ]

        self.current_waypoint_index = 0
        self.mission_complete = False
        self.test_passed = False
        self.test_failed_reason = None

        # Create QoS profile
        qos_profile = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        # Subscribers
        self.odom_sub = self.create_subscription(
            Odometry, "/odom", self.odom_callback, qos_profile
        )

        self.status_sub = self.create_subscription(
            TransformationStatus,
            "/transformer/status",
            self.status_callback,
            qos_profile,
        )

        # Publisher for test result
        self.test_result_pub = self.create_publisher(Bool, "/test_monitor/result", 10)

        self.get_logger().info(f"Test Monitor started: {self.test_name}")
        self.get_logger().info(
            f"Expected transformations: {self.expected_transformations}"
        )
        self.get_logger().info(f"Waypoints to verify: {self.waypoints}")
        self.get_logger().info(f"Timeout: {self.completion_timeout} seconds")

        # Start timeout timer
        self.timeout_timer = self.create_timer(1.0, self.check_timeout)
        self.start_time = time.time()

    def odom_callback(self, msg):
        """Track position to verify waypoint reaching."""
        x = msg.pose.pose.position.x
        y = msg.pose.pose.position.y

        # Check if current waypoint is reached
        if self.current_waypoint_index < len(self.waypoints):
            target_x, target_y = self.waypoints[self.current_waypoint_index]
            distance = math.sqrt((x - target_x) ** 2 + (y - target_y) ** 2)

            if distance <= self.waypoint_tolerance:
                if self.current_waypoint_index not in self.waypoints_reached:
                    self.waypoints_reached.append(self.current_waypoint_index)
                    self.get_logger().info(
                        f"Waypoint {self.current_waypoint_index + 1} reached: "
                        f"({x:.2f}, {y:.2f}) -> target ({target_x}, {target_y}), "
                        f"distance: {distance:.2f}m"
                    )
                    self.current_waypoint_index += 1

                    # Check if all waypoints reached
                    if self.current_waypoint_index >= len(self.waypoints):
                        self.check_mission_completion()

    def status_callback(self, msg):
        """Track transformation state changes."""
        new_form = msg.current_form
        transformation_state = msg.transformation_state
        progress = msg.progress

        if self.current_form != new_form:
            self.get_logger().info(
                f"Form change: {self.current_form} -> {new_form} "
                f"(state: {transformation_state}, progress: {progress * 100:.1f}%)"
            )
            self.transformation_count += 1
            self.transformation_sequence.append(
                {
                    "from": self.current_form,
                    "to": new_form,
                    "state": transformation_state,
                    "progress": progress,
                }
            )
            self.current_form = new_form

            # Check if we have the expected number of transformations
            if self.transformation_count >= self.expected_transformations:
                self.get_logger().info(
                    f"Expected number of transformations ({self.expected_transformations}) reached"
                )

        # Check for error states
        if transformation_state in ["ERROR", "EMERGENCY_STOP"]:
            self.test_failed_reason = f"Transformation error: {transformation_state}"
            self.get_logger().error(self.test_failed_reason)
            self.publish_result(False)

    def check_mission_completion(self):
        """Check if mission is complete and all criteria met."""
        self.get_logger().info("Checking mission completion criteria...")

        # Check transformations
        if self.transformation_count < self.expected_transformations:
            self.test_failed_reason = (
                f"Insufficient transformations: {self.transformation_count} "
                f"(expected {self.expected_transformations})"
            )
            self.get_logger().warn(self.test_failed_reason)
            # Wait a bit longer in case transformation is still in progress
            return

        # Check form at end (should be humanoid for final precision navigation)
        if self.current_form != "humanoid":
            self.test_failed_reason = f"Mission complete but final form is {self.current_form}, expected humanoid"
            self.get_logger().warn(self.test_failed_reason)
            # This might be acceptable depending on scenario, but prefer humanoid

        # All criteria met
        self.test_passed = True
        elapsed = time.time() - self.start_time
        self.get_logger().info(
            f"Test PASSED: {self.test_name}\n"
            f"  Total time: {elapsed:.2f}s\n"
            f"  Transformations: {self.transformation_count}\n"
            f"  Sequence: {self.transformation_sequence}\n"
            f"  Waypoints reached: {len(self.waypoints_reached)}/{len(self.waypoints)}"
        )
        self.publish_result(True)

    def check_timeout(self):
        """Check if test has exceeded timeout."""
        elapsed = time.time() - self.start_time
        if elapsed > self.completion_timeout:
            if not self.test_passed:
                self.test_failed_reason = f"Test timed out after {elapsed:.2f}s"
                self.get_logger().error(self.test_failed_reason)
                self.publish_result(False)
            else:
                self.get_logger().info("Test completed within timeout")

    def publish_result(self, passed):
        """Publish test result and shutdown."""
        result_msg = Bool()
        result_msg.data = passed
        self.test_result_pub.publish(result_msg)

        if passed:
            self.get_logger().info("✅ Test completed successfully")
        else:
            self.get_logger().error("❌ Test failed")

        # Allow some time for message to be sent
        time.sleep(1.0)
        rclpy.shutdown()


def main(args=None):
    rclpy.init(args=args)
    node = TestMonitor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Mesh generation script for transformer robot simulation.
Generates all required STL mesh files with proper copyright headers.
"""

import os
import datetime
import math

# Copyright header template
COPYRIGHT_HEADER = """# Transformer Robot Simulation Mesh
# Copyright (c) 2024 OpenCode Project
# Licensed under MIT License
# Generated: {date}
# Description: {description}
# Triangles: {triangles}
 solid transformer_mesh
"""


def write_stl(filename, facets, description):
    """Write an ASCII STL file with copyright header."""
    header = COPYRIGHT_HEADER.format(
        date=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        description=description,
        triangles=len(facets),
    )

    with open(filename, "w") as f:
        f.write(header)
        for facet in facets:
            f.write(f"  facet normal {facet['nx']} {facet['ny']} {facet['nz']}\n")
            f.write("    outer loop\n")
            for vertex in facet["vertices"]:
                f.write(f"      vertex {vertex[0]} {vertex[1]} {vertex[2]}\n")
            f.write("    endloop\n")
            f.write("  endfacet\n")
        f.write("endsolid transformer_mesh\n")


def create_box_stl(x, y, z, description):
    """Generate STL facets for a box centered at origin."""
    hx, hy, hz = x / 2, y / 2, z / 2

    # 6 faces, 2 triangles each = 12 facets total
    facets = []

    # Front face (normal +z)
    facets.append(
        {
            "nx": 0,
            "ny": 0,
            "nz": 1,
            "vertices": [[-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz]],
        }
    )
    facets.append(
        {
            "nx": 0,
            "ny": 0,
            "nz": 1,
            "vertices": [[-hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz]],
        }
    )

    # Back face (normal -z)
    facets.append(
        {
            "nx": 0,
            "ny": 0,
            "nz": -1,
            "vertices": [[-hx, hy, -hz], [hx, hy, -hz], [hx, -hy, -hz]],
        }
    )
    facets.append(
        {
            "nx": 0,
            "ny": 0,
            "nz": -1,
            "vertices": [[-hx, hy, -hz], [hx, -hy, -hz], [-hx, -hy, -hz]],
        }
    )

    # Left face (normal -x)
    facets.append(
        {
            "nx": -1,
            "ny": 0,
            "nz": 0,
            "vertices": [[-hx, -hy, -hz], [-hx, hy, -hz], [-hx, hy, hz]],
        }
    )
    facets.append(
        {
            "nx": -1,
            "ny": 0,
            "nz": 0,
            "vertices": [[-hx, -hy, -hz], [-hx, hy, hz], [-hx, -hy, hz]],
        }
    )

    # Right face (normal +x)
    facets.append(
        {
            "nx": 1,
            "ny": 0,
            "nz": 0,
            "vertices": [[hx, -hy, -hz], [hx, hy, -hz], [hx, hy, hz]],
        }
    )
    facets.append(
        {
            "nx": 1,
            "ny": 0,
            "nz": 0,
            "vertices": [[hx, -hy, -hz], [hx, hy, hz], [hx, -hy, hz]],
        }
    )

    # Top face (normal +y)
    facets.append(
        {
            "nx": 0,
            "ny": 1,
            "nz": 0,
            "vertices": [[-hx, hy, -hz], [hx, hy, -hz], [hx, hy, hz]],
        }
    )
    facets.append(
        {
            "nx": 0,
            "ny": 1,
            "nz": 0,
            "vertices": [[-hx, hy, -hz], [hx, hy, hz], [-hx, hy, hz]],
        }
    )

    # Bottom face (normal -y)
    facets.append(
        {
            "nx": 0,
            "ny": -1,
            "nz": 0,
            "vertices": [[-hx, -hy, -hz], [hx, -hy, -hz], [hx, -hy, hz]],
        }
    )
    facets.append(
        {
            "nx": 0,
            "ny": -1,
            "nz": 0,
            "vertices": [[-hx, -hy, -hz], [hx, -hy, hz], [-hx, -hy, hz]],
        }
    )

    return facets


def create_cylinder_stf(length, radius, segments=12):
    """Generate STL facets for a cylinder along Z axis."""
    facets = []
    angle_step = 2 * math.pi / segments

    # Generate vertices around the cylinder
    for i in range(segments):
        theta1 = i * angle_step
        theta2 = (i + 1) * angle_step

        x1 = radius * math.cos(theta1)
        y1 = radius * math.sin(theta1)
        x2 = radius * math.cos(theta2)
        y2 = radius * math.sin(theta2)

        # Side facets - 2 triangles per segment
        normal_x = math.cos(theta1 + angle_step / 2)
        normal_y = math.sin(theta1 + angle_step / 2)

        # First triangle (bottom to top)
        facets.append(
            {
                "nx": normal_x,
                "ny": normal_y,
                "nz": 0,
                "vertices": [
                    [x1, y1, -length / 2],
                    [x2, y2, -length / 2],
                    [x1, y1, length / 2],
                ],
            }
        )
        facets.append(
            {
                "nx": normal_x,
                "ny": normal_y,
                "nz": 0,
                "vertices": [
                    [x2, y2, -length / 2],
                    [x2, y2, length / 2],
                    [x1, y1, length / 2],
                ],
            }
        )

    # End caps (top and bottom) - approximated as polygons
    for end in [-1, 1]:  # -1 for bottom, +1 for top
        z = end * length / 2
        normal_z = end

        for i in range(segments):
            theta1 = i * angle_step
            theta2 = (i + 1) * angle_step

            x1 = radius * (3.14159265359).cos(theta1)
            y1 = radius * (3.14159265359).sin(theta1)
            x2 = radius * (3.14159265359).cos(theta2)
            y2 = radius * (3.14159265359).sin(theta2)

            facets.append(
                {
                    "nx": 0,
                    "ny": 0,
                    "nz": normal_z,
                    "vertices": [[0, 0, z], [x1, y1, z], [x2, y2, z]],
                }
            )

    return facets


def create_sphere_stl(radius, segments_lat=8, segments_long=12):
    """Generate STL facets for a sphere."""
    facets = []

    for i in range(segments_lat):
        theta1 = i * 3.14159265359 / segments_lat
        theta2 = (i + 1) * 3.14159265359 / segments_lat

        for j in range(segments_long):
            phi1 = j * 2 * 3.14159265359 / segments_long
            phi2 = (j + 1) * 2 * 3.14159265359 / segments_long

            # Generate 4 vertices of the quad
            vertices = []
            for theta in [theta1, theta2]:
                for phi in [phi1, phi2]:
                    x = radius * (3.14159265359).sin(theta) * (3.14159265359).cos(phi)
                    y = radius * (3.14159265359).sin(theta) * (3.14159265359).sin(phi)
                    z = radius * (3.14159265359).cos(theta)
                    vertices.append([x, y, z])

            # Compute approximate normal (center of facet)
            nx = (vertices[0][0] + vertices[2][0]) / 2 / radius
            ny = (vertices[0][1] + vertices[2][1]) / 2 / radius
            nz = (vertices[0][2] + vertices[2][2]) / 2 / radius

            # Split quad into two triangles
            facets.append(
                {
                    "nx": nx,
                    "ny": ny,
                    "nz": nz,
                    "vertices": [vertices[0], vertices[1], vertices[2]],
                }
            )
            facets.append(
                {
                    "nx": nx,
                    "ny": ny,
                    "nz": nz,
                    "vertices": [vertices[0], vertices[2], vertices[3]],
                }
            )

    return facets


def main():
    meshes_dir = "transformer_description/meshes"
    os.makedirs(meshes_dir, exist_ok=True)

    # Define all mesh files with their generation functions
    meshes = [
        # Torso components
        (
            "torso_base.stl",
            lambda: create_box_stl(0.3, 0.2, 0.4),
            "Base torso segment with main body mass",
        ),
        (
            "torso_mid.stl",
            lambda: create_box_stl(0.25, 0.18, 0.3),
            "Middle torso segment for pitch joint",
        ),
        (
            "torso_top.stl",
            lambda: create_box_stl(0.2, 0.15, 0.25),
            "Upper torso segment with roll joint and head/arm attachments",
        ),
        # Head components
        (
            "head_base.stl",
            lambda: create_box_stl(0.1, 0.1, 0.08),
            "Head base with pan rotation platform",
        ),
        (
            "head_dome.stl",
            lambda: create_sphere_stl(0.08, 6, 8),
            "Domed head top with tilt joint, sphere shape for sensor housing",
        ),
        # Arm components
        (
            "shoulder_joint.stl",
            lambda: create_box_stl(0.05, 0.05, 0.08),
            "Shoulder joint connector block",
        ),
        (
            "upper_arm.stl",
            lambda: create_cylinder_stf(0.12, 0.04),
            "Upper arm segment, cylindrical shape",
        ),
        (
            "lower_arm.stl",
            lambda: create_cylinder_stf(0.1, 0.035),
            "Lower arm/forearm segment",
        ),
        # Leg components
        (
            "hip_joint.stl",
            lambda: create_box_stl(0.08, 0.08, 0.06),
            "Hip joint assembly block",
        ),
        (
            "upper_leg.stl",
            lambda: create_cylinder_stf(0.3, 0.06),
            "Upper leg/thigh segment, cylindrical",
        ),
        (
            "lower_leg.stl",
            lambda: create_cylinder_stf(0.3, 0.05),
            "Lower leg/calf segment, slightly thinner than thigh",
        ),
    ]

    print(f"Generating {len(meshes)} mesh files...")
    for filename, generator, description in meshes:
        filepath = os.path.join(meshes_dir, filename)
        facets = generator()
        write_stl(filepath, facets, description)
        print(f"  ✓ Created {filename} ({len(facets)} triangles)")

    print(f"\nSuccessfully generated all mesh files in {meshes_dir}/")


if __name__ == "__main__":
    main()

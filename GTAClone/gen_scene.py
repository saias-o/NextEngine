#!/usr/bin/env python3
"""Generate GTAClone/scenes/main.scene — a small blocky GTA-style city.

Run once from anywhere:  python GTAClone/gen_scene.py
Only built-in resources are used (the "cube" mesh), so no assets are required.
"""
import json, math, os

IDENT = [0.0, 0.0, 0.0, 1.0]   # quaternion x,y,z,w


def yaw_quat(deg):
    a = math.radians(deg) * 0.5
    return [0.0, math.sin(a), 0.0, math.cos(a)]


def xform(pos, rot=IDENT, scale=(1, 1, 1)):
    return {"position": [float(p) for p in pos],
            "rotation": [float(r) for r in rot],
            "scale": [float(s) for s in scale]}


def mesh_node(name, color, scale, pos=(0, 0, 0), shadows=True):
    return {
        "type": "MeshNode", "name": name, "enabled": True, "behaviours": [],
        "mesh": "cube", "baseColor": [float(c) for c in color],
        "meshEnabled": True, "castShadows": shadows, "includeInLightBaking": False,
        "transform": xform(pos, IDENT, scale),
    }


def box_shape(half, offset=(0, 0, 0)):
    return {
        "type": "CollisionShape", "name": "CollisionShape", "enabled": True,
        "behaviours": [], "shapeType": 1, "halfExtents": [float(h) for h in half],
        "radius": 0.5, "height": 1.0, "axis": 1, "offset": [float(o) for o in offset],
        "transform": xform((0, 0, 0)),
    }


def capsule_shape(radius, height):
    return {
        "type": "CollisionShape", "name": "CollisionShape", "enabled": True,
        "behaviours": [], "shapeType": 3,
        "halfExtents": [radius, height * 0.5, radius],
        "radius": radius, "height": height, "axis": 1, "offset": [0, 0, 0],
        "transform": xform((0, 0, 0)),
    }


def ground():
    # A dry island the city sits on; water surrounds it beyond the edges.
    return {
        "type": "StaticBody", "name": "Ground", "enabled": True, "behaviours": [],
        "friction": 0.95, "restitution": 0.0, "transform": xform((0, -0.5, 0)),
        "children": [
            mesh_node("GroundMesh", (0.22, 0.23, 0.26), (100, 1, 100), shadows=False),
            box_shape((50, 0.5, 50)),
        ],
    }


def invisible_wall(name, pos, half):
    # A StaticBody collider with NO mesh: blocks the player/cars without being seen.
    return {
        "type": "StaticBody", "name": name, "enabled": True, "behaviours": [],
        "friction": 0.4, "restitution": 0.0, "transform": xform(pos),
        "children": [box_shape(half)],
    }


def water():
    # NextEngine's default animated water. Surrounds the island; sits just below the
    # shore. Procedural waves + sky reflection — fully parameterized (see WaterNode).
    # Look/feel comes entirely from the WaterNode defaults (single source of truth);
    # the scene only places the plane. Tweak in the inspector, or edit the defaults.
    return {
        "type": "Water", "name": "Ocean", "enabled": True, "behaviours": [],
        "transform": xform((0.0, -0.5, 0.0)),
    }


def road_strip(name, pos, scale):
    # Flat cosmetic asphalt strip, slightly above ground; no collider needed.
    return mesh_node_root(name, (0.12, 0.12, 0.14), scale, pos)


def mesh_node_root(name, color, scale, pos):
    # A bare top-level MeshNode (no physics) for decorative road strips.
    return {
        "type": "MeshNode", "name": name, "enabled": True, "behaviours": [],
        "mesh": "cube", "baseColor": [float(c) for c in color],
        "meshEnabled": True, "castShadows": False, "includeInLightBaking": False,
        "transform": xform(pos, IDENT, scale),
    }


def building(name, center, w, d, h, color):
    cx, cz = center
    return {
        "type": "StaticBody", "name": name, "enabled": True, "behaviours": [],
        "friction": 0.8, "restitution": 0.0,
        "transform": xform((cx, h * 0.5, cz)),
        "children": [
            mesh_node(name + "Mesh", color, (w, h, d)),
            box_shape((w * 0.5, h * 0.5, d * 0.5)),
        ],
    }


def player(pos):
    return {
        "type": "CharacterBody", "name": "Player", "enabled": True,
        "groups": ["driver", "player"], "mass": 70.0, "maxSlopeAngle": 50.0,
        "friction": 0.5, "restitution": 0.0,
        "behaviours": [
            {
                "type": "Character", "enabled": True, "moveSpeed": 6.0,
                "sprintMultiplier": 1.8, "jumpForce": 6.0, "gravity": 18.0,
                "faceMovement": True, "turnSpeed": 14.0,
            },
            {
                "type": "Gun", "enabled": True, "damage": 60.0, "range": 120.0,
                "cooldown": 0.2, "hitRadius": 0.7, "targetGroup": "npc",
            },
        ],
        "transform": xform(pos),
        "children": [
            mesh_node("Body", (0.20, 0.55, 0.85), (0.8, 1.8, 0.8)),
            mesh_node("Head", (0.95, 0.80, 0.65), (0.5, 0.5, 0.5), pos=(0, 1.15, 0)),
            mesh_node("FacingNub", (1.0, 0.85, 0.10), (0.22, 0.22, 0.4), pos=(0, 0.4, -0.5)),
            mesh_node("Gun", (0.10, 0.10, 0.12), (0.18, 0.18, 0.7), pos=(0.45, 0.45, -0.45)),
            capsule_shape(0.4, 1.8),
        ],
    }


def car(name, pos, yaw_deg, body_color):
    return {
        "type": "CharacterBody", "name": name, "enabled": True,
        "groups": ["vehicle"], "mass": 400.0, "maxSlopeAngle": 55.0,
        "friction": 0.6, "restitution": 0.0,
        "behaviours": [{
            "type": "Vehicle", "enabled": True, "maxSpeed": 16.0,
            "reverseSpeed": 6.0, "accel": 14.0, "brakeDecel": 26.0,
            "turnRate": 120.0, "gravity": 22.0, "enterRadius": 4.0,
            "exitDistance": 2.2, "driverGroup": "driver", "cameraGroup": "player",
            "killSpeed": 4.0, "runOverRadius": 2.6, "runOverDamage": 1000.0,
            "npcGroup": "npc",
        }],
        "transform": xform(pos, yaw_quat(yaw_deg)),
        "children": [
            mesh_node("Chassis", body_color, (2.0, 0.8, 4.0)),
            mesh_node("Cabin", [c * 0.7 for c in body_color], (1.6, 0.7, 1.9),
                      pos=(0, 0.7, -0.1)),
            mesh_node("Hood", [c * 0.85 for c in body_color], (1.7, 0.3, 1.2),
                      pos=(0, 0.2, -1.3)),
            mesh_node("Headlight", (1.0, 0.95, 0.6), (1.5, 0.2, 0.2), pos=(0, 0.1, -1.95)),
            box_shape((1.0, 0.6, 2.0), offset=(0, 0.2, 0)),
        ],
    }


def npc(name, pos, color):
    return {
        "type": "CharacterBody", "name": name, "enabled": True,
        "groups": ["npc"], "mass": 65.0, "maxSlopeAngle": 50.0,
        "friction": 0.5, "restitution": 0.0,
        "behaviours": [
            {
                "type": "NpcWander", "enabled": True, "speed": 1.6,
                "changeInterval": 3.0, "turnSpeed": 6.0, "leashRadius": 14.0,
                "gravity": 22.0,
            },
            {
                "type": "Health", "enabled": True, "maxHealth": 100.0,
                "deathDelay": 4.0, "tipOverOnDeath": True,
            },
        ],
        "transform": xform(pos),
        "children": [
            mesh_node("Body", color, (0.6, 1.5, 0.4)),
            mesh_node("Head", (0.9, 0.75, 0.6), (0.45, 0.45, 0.45), pos=(0, 1.0, 0)),
            capsule_shape(0.35, 1.5),
        ],
    }


def sun():
    return {
        "type": "LightNode", "name": "Sun", "enabled": True, "behaviours": [],
        "lightType": 0, "color": [1.0, 0.96, 0.88], "intensity": 2.4,
        "direction": [-0.4, -0.85, -0.35], "range": 10.0,
        "spotInnerAngle": 25.0, "spotOuterAngle": 35.0, "castShadows": True,
        "bakeMode": 0, "transform": xform((0, 30, 0)),
    }


def camera():
    return {
        "type": "Camera", "name": "Follow Camera", "enabled": True,
        "fovDegrees": 60.0, "nearZ": 0.1, "farZ": 400.0, "priority": 10,
        "active": True,
        "behaviours": [{
            "type": "CameraFollow", "enabled": True, "targetGroup": "player",
            "distance": 7.0, "height": 1.8, "shoulderOffset": 0.0,
            "yawSensitivity": 0.18, "pitchSensitivity": 0.16,
            "minPitch": -25.0, "maxPitch": 65.0, "positionDamping": 12.0,
            "collisionMargin": 0.3, "minDistance": 1.0,
        }],
        "transform": xform((0, 5, 12)),
    }


def main():
    children = [sun(), ground()]

    # Water all around the island + an invisible wall ring so you can't reach it yet.
    children.append(water())
    wall_half_long = 50.0
    children.append(invisible_wall("Wall +X", (49.0, 3.0, 0.0), (0.5, 3.0, wall_half_long)))
    children.append(invisible_wall("Wall -X", (-49.0, 3.0, 0.0), (0.5, 3.0, wall_half_long)))
    children.append(invisible_wall("Wall +Z", (0.0, 3.0, 49.0), (wall_half_long, 3.0, 0.5)))
    children.append(invisible_wall("Wall -Z", (0.0, 3.0, -49.0), (wall_half_long, 3.0, 0.5)))

    # Cosmetic cross roads through the middle (dark strips on the ground).
    children.append(road_strip("RoadX", (0, 0.02, 0), (100, 0.04, 10)))
    children.append(road_strip("RoadZ", (0, 0.02, 0), (10, 0.04, 100)))

    # City blocks (leave ~10-wide streets along the axes).
    buildings = [
        ("Tower_NW", (-26, -26), 18, 18, 12, (0.45, 0.42, 0.40)),
        ("Tower_NE", (26, -26), 18, 18, 20, (0.40, 0.44, 0.50)),
        ("Tower_SW", (-26, 26), 18, 18, 9, (0.50, 0.40, 0.38)),
        ("Tower_SE", (26, 26), 18, 18, 16, (0.38, 0.46, 0.44)),
        ("Shop_N", (0, -34), 12, 8, 7, (0.55, 0.48, 0.35)),
        ("Shop_S", (0, 34), 12, 8, 7, (0.48, 0.50, 0.40)),
        ("Block_W", (-38, 0), 8, 14, 11, (0.42, 0.42, 0.48)),
        ("Block_E", (38, 0), 8, 14, 13, (0.46, 0.43, 0.41)),
    ]
    for b in buildings:
        children.append(building(*b))

    # The player at the central intersection.
    children.append(player((0, 2.0, 0)))

    # A couple of parked cars on the streets.
    children.append(car("Sedan", (5.5, 1.2, -6.0), 0.0, [0.75, 0.18, 0.18]))
    children.append(car("Coupe", (-5.5, 1.2, 9.0), 180.0, [0.18, 0.35, 0.72]))
    children.append(car("Taxi", (8.0, 1.2, 14.0), 90.0, [0.85, 0.70, 0.10]))

    # Wandering NPCs scattered along the sidewalks.
    npc_spots = [
        ("NPC_1", (3, 1.5, -12), (0.70, 0.30, 0.30)),
        ("NPC_2", (-4, 1.5, -8), (0.30, 0.55, 0.40)),
        ("NPC_3", (6, 1.5, 7), (0.35, 0.40, 0.65)),
        ("NPC_4", (-7, 1.5, 4), (0.60, 0.55, 0.30)),
        ("NPC_5", (12, 1.5, -3), (0.50, 0.35, 0.55)),
        ("NPC_6", (-12, 1.5, -2), (0.30, 0.50, 0.55)),
        ("NPC_7", (2, 1.5, 16), (0.65, 0.45, 0.35)),
    ]
    for n in npc_spots:
        children.append(npc(*n))

    children.append(camera())

    scene = {
        "version": 1,
        "scene": {
            "type": "Scene", "name": "main", "enabled": True, "behaviours": [],
            "transform": xform((0, 0, 0)),
            "settings": {
                "ambient": [0.30, 0.31, 0.36], "clearColor": [0.52, 0.66, 0.84],
                "postProcessing": True, "lightingMode": 0, "giEnabled": True,
                "giIntensity": 1.0, "skyboxTexture": 0, "skyboxExposure": 1.0,
                "skyboxRotation": 0.0, "iblEnabled": True, "aoEnabled": True,
                "fogEnabled": False, "bloomEnabled": True, "changeRenderingAtLoad": True,
            },
            "children": children,
        },
    }

    out = os.path.join(os.path.dirname(__file__), "scenes", "main.scene")
    with open(out, "w") as f:
        json.dump(scene, f, indent=2)
    print("wrote", out, "with", len(children), "top-level nodes")


if __name__ == "__main__":
    main()

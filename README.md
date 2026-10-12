<img width="1091" height="606" alt="image" src="https://github.com/user-attachments/assets/57784389-7fa0-42fd-a4e2-8169ef11a8cc" />
# Corridor of the Dead – Haunted Night

A first-person horror shooter built with **C++ and legacy OpenGL (GLUT)**.
Explore a haunted school floor, shoot robot-zombies, and ride the elevator up.
Clear all 3 floors to win.

All textures are generated procedurally in code (no image files needed), and the
whole world is built from OpenGL primitives.

## Gameplay

- You start in the corridor of a dark school building: 8 classrooms
  (abandoned asylum beds inside), a washroom with 3 toilet cubicles, and 2 elevators.
- 6 robot-zombies patrol the floor. If you come within **6.5 m** they chase you.
  If one reaches you (**< 0.9 m**), you die instantly.
- Each zombie takes **3 hits**. Killed zombies respawn after about 6 seconds.
- Kill quota per floor unlocks **Elevator A**:

| Floor | Kills needed |
|-------|--------------|
| 1     | 4            |
| 2     | 5            |
| 3     | 6 (win)      |

- When the quota is met, the lift opens. Step inside and press **E / Right Click**
  (or **O**) to close the doors, then again to ride to the next floor.
- A compass arrow on the HUD points toward Elevator A on floors 1 and 2.
- Killing the required number on floor 3 wins the game.

## Controls

| Input | Action |
|-------|--------|
| W A S D / Arrow keys | Move |
| Shift | Run |
| Mouse | Look around |
| Left Click (hold) / Space | Shoot (auto-fire, 150 ms between shots) |
| E / Right Click | Open/close nearest door, or use the lift |
| O | Use the lift |
| R | Restart after dying or winning |
| ESC | Quit |

## Features

**Graphics**
- Procedural textures: grimy checker floor, damask wallpaper, rusted metal,
  weathered wood, and a night-time graveyard window view (value noise / fBm)
- Fog (exponential squared) that "breathes" over time
- Four lights: flickering ceiling light, cool fill light, flashlight spotlight
  attached to the camera, and a point light from the nearest candle
- Flickering/dead ceiling tubes, animated candle flames, cobwebs,
  drifting dust particles, blood-scrawled wall text (GLUT stroke font)
- Old-film post effect: tint, vignette, scanlines, grain, flicker, scratches
- Hierarchical zombie model (hips → legs, torso → neck → head, arms with claws),
  glowing red eye via `GL_EMISSION`
- Animated hinged doors, sliding elevator doors, rotating ceiling fans,
  bobbing first-person pistol with muzzle flash and bullet tracers

**Game systems**
- Hitscan shooting (ray vs. bounding sphere)
- Circle vs. AABB collision for walls, furniture, and rotating door leaves
- Zombie AI: waypoint patrol per zone, aggro/chase, furniture-aware movement,
  death and respawn states
- Multi-floor progression with a locked/open/closed elevator state machine

## Build and Run

**Linux**
```bash
sudo apt install freeglut3-dev
g++ main.cpp -o corridor -lglut -lGLU -lGL -lm
./corridor
```

**Windows (Visual Studio)**
Install freeglut (e.g. via vcpkg: `vcpkg install freeglut`), create a C++
console/Win32 project, add `main.cpp`, and link against `freeglut`,
`opengl32`, and `glu32`.

## Code Structure

| Section | Description |
|---------|-------------|
| Collision | Rect tables for walls/furniture, door-leaf collision |
| Procedural textures | Noise functions and texture generation |
| Drawing helpers | `box`, `ball`, `texturedBox`, lamps, shadow blobs |
| Building geometry | Wall/obstacle/door tables built once at startup |
| Doors, elevators, fans, window | Animated scene objects |
| Classrooms and washroom | Interior drawing |
| Zombie AI | Patrol, chase, death/respawn |
| Zombie model | Hierarchical robot-zombie |
| Gun and game flow | Viewmodel, hitscan, floors, reset |
| Haunted atmosphere | Cobwebs, candles, scrawls, dust, film overlay |
| Render / input / main | Display loop, timer, callbacks, setup |

## Screenshots
<img width="1082" height="537" alt="image" src="https://github.com/user-attachments/assets/4dcfe492-609d-48bd-8f19-7abe8d459608" />
<img width="1082" height="661" alt="image" src="https://github.com/user-attachments/assets/bbf7d701-909d-4da8-ac62-00de48f83377" />
<img width="634" height="300" alt="image" src="https://github.com/user-attachments/assets/30e0dc9e-2d3a-40fd-ae77-6c0532df3d89" />

## Author
SUMYA AKTER ASHA

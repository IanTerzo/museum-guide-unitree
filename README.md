# museum-guide-unitree

A museum guide system for the Unitree Go2 robot. The robot navigates autonomously between exhibit areas using SLAM, and plays audio descriptions at each stop via a local TTS (Text-to-Speech) HTTP service.

---

## Repository structure

```
museum-guide-unitree/
├── museum-SLAM/
│   ├── example/
│   │   ├── src/keyDemo.cpp       # Main navigation controller (C++)
│   │   ├── build/
│   │   │   └── skola             # Pre-recorded task list for the "skola" map
│   │   ├── CMakeLists.txt        # Build config (links unitree_sdk2 + libcurl)
│   │   └── include/json.hpp      # nlohmann/json single-header
│   └── rviz2/
│       ├── mapping.rviz          # RViz2 config for SLAM mapping session
│       └── relocation.rviz       # RViz2 config for relocation/navigation session
│
└── museum-TTS/
    ├── player/
    │   ├── main.go               # HTTP audio server (plays .wav via paplay)
    │   ├── player.go             # Calls paplay --device=g1_speaker
    │   ├── config.json           # Sound library and listen address (:8080)
    │   └── sounds/               # Audio files for each location
    │       ├── Cafeterian.wav
    │       ├── Entren.wav
    │       ├── Korridoren.wav
    │       └── Teknikgrottan.wav
    └── ui/
        ├── main.go               # Thin web UI proxy (listen :62324 → player :8080)
        ├── static/index.html     # Browser interface: list and play sounds
        └── config.json           # UI config
```

> **SLAM attribution:** `museum-SLAM/example/` is a fork of
> https://github.com/blogdefotsec/Unitree_SDK_Extended/tree/9e6f67e3d731e27ad9a61d19636d04d53c79fec7/original_SDK/unitree_slam/20250806/module/unitree_slam/example
> Extended with libcurl-based feedback, named poses, task list save/load, and `"empty"` pose skipping.

---

## How it works

1. **keyDemo** connects to the robot over the network and controls SLAM navigation.
2. When the robot **arrives at a named pose**, it POSTs `{"pose_name": "<name>"}` to the TTS server's `/arrived` endpoint and **waits** for a 200 response before moving to the next pose.
3. The **TTS player** receives the arrival notification, plays the corresponding audio file, and returns 200 when playback finishes — releasing the robot to continue.
4. Poses named `"empty"` are skipped silently.
5. After completing the route the list is **reversed** and replayed, creating a continuous back-and-forth loop.

---

## Step-by-step guide

### 1. Prerequisites

- [unitree_sdk2](https://github.com/unitreerobotics/unitree_sdk2) installed on the machine connected to the robot
- Go ≥ 1.22 (for the TTS services)
- `paplay` available on the robot (part of `pulseaudio-utils`)
- The robot already has a saved map at `/home/unitree/test.pcd`

---

### 2. Build keyDemo

```bash
cd museum-SLAM/example
mkdir -p build && cd build
cmake ..
make
```

---

### 3. Start the TTS player (on the robot or a nearby machine)

```bash
cd museum-TTS/player
go run . -config config.json
# Listens on :8080
```

The sound files for each location are already included in `museum-TTS/player/sounds/` and registered in `config.json`.

#### (Optional) Start the web UI to verify playback

```bash
cd museum-TTS/ui
go run . -config config.json
# Open http://localhost:62324 in a browser
```

The web UI lets you manually trigger playback of any registered sound to confirm audio is working before starting the robot.

---

### 4. Run the robot guide with the "skola" map

Replace `eth0` with the network interface that has the robot on the 192.168.123.x segment.

#### 4a. Start SLAM relocation (locate the robot in the existing map)

```bash
cd museum-SLAM/example/build
./keyDemo eth0 --feedback_ip=<TTS_SERVER_IP> --feedback_port=8080
```

Inside the program, press:

| Key | Action |
|-----|--------|
| `a` | Start relocation against `/home/unitree/test.pcd` |

Wait until the robot reports a stable pose in the RViz2 relocation view (`rviz2 -d ../../rviz2/relocation.rviz`).

#### 4b. Load the skola task list

```bash
# Still inside the running keyDemo session:
i   # Import task list
```
When prompted for a filename, enter the full path to the skola file:
```
/home/unitree/slam-demo-feedback/.../example/build/skola
```
Or copy it first:
```bash
cp museum-SLAM/example/build/skola /home/unitree/task_list.json
```
Then just press Enter at the filename prompt to use the default `/home/unitree/task_list.json`.

The loaded route visits (in order):

| # | Pose name | Description |
|---|-----------|-------------|
| 1 | entren | Entrance |
| 2 | pose_2 | Intermediate |
| 3 | pose_3 | Intermediate |
| 4 | cafeterian | Cafeteria |
| 5 | pose_5 | Intermediate |
| 6–8 | pose_6 … pose_8 | Corridor approach |
| 9–11 | pose_9 … pose_11 | Wing transition |
| 12 | korrideren | Corridor |
| 13+ | korridoren / teknikgrottan | Tech cave area |

#### 4c. Start the tour

```bash
d   # Execute task list (starts the loop)
```

The robot will navigate pose by pose. At each **named** stop (entren, cafeterian, korridoren, teknikgrottan) it will wait for the TTS server to finish playing audio before continuing.

---

### 5. Pause and resume navigation

| Key | Action |
|-----|--------|
| `z` | Pause navigation |
| `x` | Resume navigation |
| Any other key | Stop SLAM node and halt |
| `Ctrl+C` | Exit the program |

---

### 6. Mapping a new area (optional)

If you need to remap:

```bash
./keyDemo eth0
```

| Key | Action |
|-----|--------|
| `q` | Start mapping |
| Walk robot around the space manually | — |
| `w` | End mapping (saves to `/home/unitree/test.pcd`) |
| `a` | Start relocation in the saved map |
| `s` | Add current pose to task list (you will be prompted for a name) |
| `e` | Export / save the task list |
| `d` | Run the task list |

Use `rviz2 -d museum-SLAM/rviz2/mapping.rviz` to visualise the map being built.

---

## TTS HTTP API

The player exposes a simple REST API on `:8080`:

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/sounds` | List all registered sounds (JSON array) |
| `POST` | `/play` | Play a file: `form: file=<path>` |
| `POST` | `/upload` | Upload a new sound: multipart `name` + `file` |
| `DELETE` | `/sounds/<name>` | Remove a sound from the registry |
| `POST` | `/arrived` | **Called by keyDemo** — plays matching sound, blocks until done |

The `/arrived` endpoint is the integration point between the robot and the audio system.

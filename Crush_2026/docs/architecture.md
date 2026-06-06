# Crush_2026 Architecture

## Mission-Level Intent

Crush_2026 uses three cameras and a split-compute topology:

- two front-facing cameras as a dedicated stereo pair for 3D mapping and depth estimation
- one bottom-facing camera for crab detection and visual support around depth-hold and loiter workflows
- BlueOS and ArduSub on the Raspberry Pi for vehicle-side control, stream publishing, and telemetry transport
- a driver computer for QGroundControl and pilot-facing mode control
- an analyst computer for the heavier perception and stereo-processing workloads

This keeps the vehicle computer focused on deterministic onboard tasks while the heavier vision stack runs on the analyst machine.

## Camera Roles

- `front_left`: left stereo stream for front-scene depth mapping
- `front_right`: right stereo stream for front-scene depth mapping
- `bottom_hflow`: downward-facing stream used for crab detection and to provide visual context around the hold stack

## Processing Pipelines

### 1. Bottom-Camera Crab Detection

Goal: run crab-detection logic on the downward-facing stream and expose an analyst-facing HUD plus local logs.

Flow:

1. BlueOS publishes the bottom camera stream from the ROV.
2. The analyst computer ingests that stream.
3. A crab-detection pipeline runs on the downward-facing frames.
4. The analyst HUD displays detections and count state.
5. Detection data is written to local logs and can be forwarded to the driver workflow if needed.

This is now the mission-facing perception path and should be treated separately from the front stereo pair.

### 2. Depth Hold and Loiter Support

Goal: use the depth sensor as the primary hold input while combining optical-flow or H-flow context and the bottom camera where useful.

Flow:

1. The depth sensor provides the primary vertical hold signal.
2. Optical-flow or H-flow data is transported through the BlueOS and MAVLink path.
3. The bottom camera provides contextual visual information for analyst-side monitoring and future assistive logic.
4. The driver selects operational mode in QGroundControl.

Operational notes:

- `Depth Hold` is the altitude-hold style mode that holds the Z axis only while X and Y remain manual.
- `Loiter` is the fuller hold mode that attempts to hold X, Y, and Z when confidence and sensor quality are sufficient.

This should remain a separate module because the control path, telemetry cadence, and failure modes are different from the crab-detection path.

### 3. Front Stereo Mapping and 3D Layout

Goal: use the two front cameras as a stereo pair to build a depth map and optionally a coarse 3D layout of the space.

Flow:

1. Calibrate the front camera pair with a chessboard-based stereo workflow.
2. Rectify the left and right views.
3. Compute disparity and depth estimates.
4. Display a colorized depth map or future point-cloud view on the analyst computer.

This is useful for relative range estimates, obstacle awareness, and operator context. It should remain independent from the bottom-camera detection path.

## Station Topology

- `ROV / Raspberry Pi`: publishes camera streams, runs BlueOS and ArduSub, and transports MAVLink telemetry.
- `Driver computer`: runs QGroundControl, receives status, and toggles modes such as depth hold or loiter.
- `Analyst computer`: runs the crab detector HUD and the stereo mapping feed, and keeps heavy processing local.

The analyst display concept is two-window oriented:

- `Window 1`: bottom-camera crab detector HUD
- `Window 2`: front-camera stereo depth or 3D mapping view

## Repository Boundaries

- `src/crush_2026/vehicle`: interfaces tied to BlueOS, ArduSub, and onboard-facing control boundaries
- `src/crush_2026/topside/streaming`: stream ingest, routing, and output surfaces
- `src/crush_2026/topside/perception`: bottom-camera crab detection and mission-specific object logic
- `src/crush_2026/topside/depth_hold`: depth-sensor, optical-flow, and bottom-camera assisted hold support
- `src/crush_2026/topside/mapping`: stereo calibration, depth, and scene reconstruction
- `src/crush_2026/shared`: configuration, camera roles, and shared contracts
- `experiments/`: prototypes kept separate from the production module tree

## Recommended Build Order

1. Finalize stream naming and host responsibilities.
2. Implement a reusable three-stream ingest layer.
3. Add the bottom-camera crab-detection pipeline.
4. Integrate the depth-sensor and optical-flow hold inputs.
5. Add stereo calibration and depth mapping on the front pair.

The important architectural decision is that the bottom camera and the front stereo pair now serve different jobs. The bottom stream is the crab-detection and hold-context surface, while the front pair is reserved for stereo mapping.
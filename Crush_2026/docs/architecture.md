# Crush_2026 Architecture

## Mission-Level Intent

Crush_2026 uses three cameras and a split-compute topology:

- two front-facing cameras for crab detection, stereo depth, and forward scene understanding
- one bottom-facing camera paired with the optical-flow sensor for depth-hold assistance
- BlueOS and ArduSub on the Raspberry Pi for vehicle-side control and stream publishing
- a separate inference station on the same router for perception and scene-processing workloads
- QGroundControl on the operator MacBook for piloting and supervision

This keeps the vehicle computer focused on deterministic onboard tasks while the heavier vision stack runs on the inference machine.

## Camera Roles

- `front_left`: primary front stream for crab detection and the left stereo view
- `front_right`: secondary front stream for crab detection and the right stereo view
- `bottom_flow`: bottom stream used alongside optical-flow data for depth-hold related logic

## Processing Pipelines

### 1. Front-Stream Crab Detection

Goal: run YOLOv26 on the two front streams and expose annotated video plus crab-specific mission outputs.

Flow:

1. BlueOS publishes RTSP streams from the ROV.
2. The inference station ingests the two front-facing feeds.
3. YOLOv26 runs on those streams.
4. A crab-specific logic layer filters detections and generates mission outputs.
5. Annotated frames and detection summaries are sent to the display layer.

This should be the first implementation target because it directly supports the mission task and validates the cross-host streaming setup.

### 2. Bottom-Camera Depth-Hold Assist

Goal: use the bottom camera with optical-flow and telemetry inputs to support depth-hold behavior.

Flow:

1. The bottom camera stream is published through BlueOS.
2. Optical-flow sensor data and telemetry are routed topside.
3. The depth-hold module estimates motion or stability cues.
4. The system exposes hold-state feedback or guidance back into the operator or control path.

This belongs in its own module because it has different inputs, timing requirements, and validation criteria than front-camera detection.

### 3. Stereo Depth and 3D Layout

Goal: use the two front cameras as a stereo pair to build a depth map and optionally a coarse 3D layout of the space.

Flow:

1. Calibrate the front camera pair.
2. Rectify the left and right views.
3. Compute disparity and depth estimates.
4. Optionally generate a point cloud or simplified scene representation.

This is useful for relative range estimates, obstacle awareness, and operator context. It is exploratory compared with the crab-detection path, so it should not block the first live-stream milestone.

## Repository Boundaries

- `src/crush_2026/vehicle`: interfaces tied to BlueOS, ArduSub, and onboard-facing control boundaries
- `src/crush_2026/topside/streaming`: stream ingest, routing, and output surfaces
- `src/crush_2026/topside/perception`: YOLOv26 inference and mission-specific object logic
- `src/crush_2026/topside/depth_hold`: bottom-camera stabilization and depth-hold support
- `src/crush_2026/topside/mapping`: stereo calibration, depth, and scene reconstruction
- `src/crush_2026/shared`: configuration, camera roles, and shared contracts
- `experiments/`: prototypes kept separate from the production module tree

## Recommended Build Order

1. Finalize stream naming and host responsibilities.
2. Implement a reusable three-stream ingest layer.
3. Add the front-camera YOLOv26 pipeline.
4. Add bottom-camera depth-hold support.
5. Add stereo calibration and depth mapping.

The important architectural decision is that all three capabilities share the same stream and host model, but they should remain separate modules because their inputs, outputs, and failure modes are different.
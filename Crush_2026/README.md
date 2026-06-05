# Crush_2026

Crush_2026 is now organized around the three topside-assisted capabilities you described instead of a flat collection of one-off scripts:

- front-camera crab detection on a separate inference computer
- bottom-camera depth-hold support using the camera paired with the optical-flow sensor
- stereo depth and optional 3D scene layout from the two front cameras

The Raspberry Pi running BlueOS and ArduSub stays responsible for vehicle control, telemetry, and video publishing. The heavier compute work lives on the separate inference machine so the operator MacBook can stay focused on QGroundControl and pilot-facing tools.

## Host Split

- ROV / Raspberry Pi: BlueOS, ArduSub, camera publishing, telemetry transport
- Operator MacBook: QGroundControl, pilot display, mission supervision
- Inference station: multi-stream ingest, YOLOv26 pipeline, crab logic, depth-hold assist, stereo mapping, annotated stream output

## Repository Layout

```text
Crush_2026/
    configs/
        streams.example.yaml
    docs/
        architecture.md
    experiments/
        classical_cv/
            dark_spot_detector.py
            hsv_crab_detector.py
        stream_debug/
            rtsp_yolo_probe.py
    src/
        crush_2026/
            shared/
            vehicle/
            topside/
                streaming/
                perception/
                depth_hold/
                mapping/
    README.md
    requirements.txt
```

## Why This Structure

- `src/crush_2026/vehicle`: onboard-facing integration points and future BlueOS or ArduSub interfaces
- `src/crush_2026/topside/streaming`: stream discovery, RTSP ingest, routing, and preview surfaces
- `src/crush_2026/topside/perception`: the future YOLOv26 crab-detection pipeline for the two front cameras
- `src/crush_2026/topside/depth_hold`: bottom-camera and optical-flow assisted depth-hold logic
- `src/crush_2026/topside/mapping`: stereo calibration, disparity, depth maps, and 3D layout experiments
- `src/crush_2026/shared`: stream roles, host config, and any shared data contracts
- `experiments/`: disposable prototypes and calibration utilities that should not define the final architecture

## Current Prototype Placement

- `experiments/stream_debug/rtsp_yolo_probe.py`: current single-stream BlueOS RTSP smoke test
- `experiments/classical_cv/dark_spot_detector.py`: generic dark-blob detector prototype
- `experiments/classical_cv/hsv_crab_detector.py`: HSV-based printed-crab detector prototype

## Development Order

1. Build a stable three-stream ingest layer from BlueOS into the inference station.
2. Replace the prototype front-camera logic with a modular YOLOv26 crab-detection pipeline.
3. Use the bottom camera plus optical-flow data for depth-hold assistance.
4. Add stereo calibration and disparity from the two front cameras for a depth map and optional 3D layout.

The 3D layout work is useful if you want range estimates, obstacle awareness, and richer scene context, but it is not the best first milestone. The first milestone should be reliable ingest and crab detection on the front streams.

See `docs/architecture.md` for the detailed system split and `configs/streams.example.yaml` for the three-camera role model.

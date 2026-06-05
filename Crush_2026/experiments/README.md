# Experiments

This folder holds prototypes, calibration utilities, and temporary tests that are useful for learning but should not define the final application structure.

- `classical_cv/dark_spot_detector.py`: blob-based detector for dark targets on a light background
- `classical_cv/hsv_crab_detector.py`: HSV-based detector for the printed crab task setup
- `stream_debug/rtsp_yolo_probe.py`: BlueOS RTSP smoke test with a simple YOLO overlay

When a prototype graduates into the real system, its logic should move into `src/crush_2026/...` under the correct module boundary instead of growing further inside `experiments/`.
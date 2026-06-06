# Experiments

This folder holds prototypes, calibration utilities, and temporary tests that are useful for learning but should not define the final application structure.

- `bottom_camera/dark_spot_detector.py`: blob-based detector for dark targets on the downward-facing task surface
- `bottom_camera/hsv_crab_detector.py`: HSV-based detector for the printed crab task setup on the bottom camera
- `front_stereo/stereo_depth_mapping.py`: front stereo-pair SGBM mapping prototype for the analyst computer
- `stream_debug/two_stream_smoke.py`: two-stream smoke test that can launch the stereo mapping window with the local MacBook host
- `stream_debug/rtsp_yolo_probe.py`: BlueOS RTSP smoke test with a simple YOLO overlay

When a prototype graduates into the real system, its logic should move into `src/crush_2026/...` under the correct module boundary instead of growing further inside `experiments/`.
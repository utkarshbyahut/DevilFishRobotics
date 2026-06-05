"""
Dark Spot Detector
==================
Masks everything that is NOT white/light, segments dark regions
as individual objects, and counts them.

Good for laminated printed crab images where crabs are dark
against a white/light background sheet.

Usage:
  python dark_spot_detector.py                  # webcam index 0
  python dark_spot_detector.py --camera 1       # different camera
  python dark_spot_detector.py --image crab.jpg # single image file
  python dark_spot_detector.py --calibrate      # tune threshold live
"""

import cv2
import numpy as np
import argparse
import sys


# ---------------------------------------------------------------------------
# Tuning parameters – adjust these or use --calibrate to find live values
# ---------------------------------------------------------------------------

# Pixels with brightness BELOW this are considered "dark" (0–255)
# On a bright white laminated sheet under ROV lights, 180–210 works well.
BRIGHTNESS_THRESHOLD = 100

# Ignore blobs smaller than this many pixels (removes noise/shadows/tape edges)
MIN_BLOB_AREA = 800

# Merge nearby blobs that are likely the same object (morphological closing radius)
# Increase if one crab is being split into multiple detections
CLOSING_RADIUS = 12

# ---------------------------------------------------------------------------

FONT       = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE = 0.6
FONT_THICK = 2


def build_dark_mask(frame: np.ndarray, threshold: int, closing_radius: int) -> np.ndarray:
    """
    1. Convert to grayscale
    2. Threshold: pixels darker than `threshold` → white (255) in mask
    3. Morphological close to merge fragmented blobs
    4. Morphological open to kill isolated noise
    """
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # Invert threshold: dark pixels → foreground (255)
    _, mask = cv2.threshold(gray, threshold, 255, cv2.THRESH_BINARY_INV)

    # Close gaps within an object (e.g. pale patches on crab body)
    close_k = cv2.getStructuringElement(
        cv2.MORPH_ELLIPSE, (closing_radius * 2 + 1, closing_radius * 2 + 1)
    )
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, close_k)

    # Remove small noise
    open_k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, open_k)

    return mask


def detect_objects(mask: np.ndarray, min_area: int) -> list[dict]:
    """
    Connected components on mask → filter by area → return blob info.
    """
    n, labels, stats, centroids = cv2.connectedComponentsWithStats(mask, connectivity=8)
    objects = []
    for i in range(1, n):  # skip background
        area = stats[i, cv2.CC_STAT_AREA]
        if area < min_area:
            continue
        objects.append({
            "id":   len(objects) + 1,
            "x":    stats[i, cv2.CC_STAT_LEFT],
            "y":    stats[i, cv2.CC_STAT_TOP],
            "w":    stats[i, cv2.CC_STAT_WIDTH],
            "h":    stats[i, cv2.CC_STAT_HEIGHT],
            "cx":   int(centroids[i][0]),
            "cy":   int(centroids[i][1]),
            "area": int(area),
        })
    # Sort left-to-right, top-to-bottom for consistent numbering
    objects.sort(key=lambda o: (o["cy"] // 80, o["cx"]))
    for idx, obj in enumerate(objects):
        obj["id"] = idx + 1
    return objects


def draw_results(frame: np.ndarray, mask: np.ndarray, objects: list[dict]) -> np.ndarray:
    """
    Overlay:
      - Tinted dark mask in semi-transparent red
      - Bounding box + ID number per object
      - Count panel top-left
    """
    output = frame.copy()

    # Tint masked region red
    tint = np.zeros_like(frame)
    tint[mask == 255] = (40, 40, 200)
    output = cv2.addWeighted(output, 0.75, tint, 0.25, 0)

    # Draw each detected object
    for obj in objects:
        x, y, w, h = obj["x"], obj["y"], obj["w"], obj["h"]
        cx, cy = obj["cx"], obj["cy"]

        # Bounding box
        cv2.rectangle(output, (x, y), (x + w, y + h), (0, 220, 255), 2)

        # ID badge at centroid
        label = str(obj["id"])
        (tw, th), _ = cv2.getTextSize(label, FONT, FONT_SCALE, FONT_THICK)
        bx, by = cx - tw // 2 - 4, cy - th // 2 - 4
        cv2.rectangle(output, (bx, by), (bx + tw + 8, by + th + 8), (0, 220, 255), -1)
        cv2.putText(output, label, (bx + 4, by + th + 2),
                    FONT, FONT_SCALE, (0, 0, 0), FONT_THICK, cv2.LINE_AA)

    # Count panel
    count = len(objects)
    panel_text = [
        f"Objects detected: {count}",
        f"Threshold: <{args_global.threshold}",
        f"Min area:  {args_global.min_area}px",
    ]
    px, py, lh = 10, 10, 22
    ph = 14 + lh * len(panel_text)
    cv2.rectangle(output, (px, py), (px + 260, py + ph), (20, 20, 20), -1)
    cv2.rectangle(output, (px, py), (px + 260, py + ph), (200, 200, 200), 1)
    for i, txt in enumerate(panel_text):
        color = (80, 240, 80) if i == 0 else (180, 180, 180)
        weight = FONT_THICK if i == 0 else 1
        cv2.putText(output, txt, (px + 8, py + 16 + lh * i),
                    FONT, 0.5, color, weight, cv2.LINE_AA)

    return output


def calibrate_mode(cap: cv2.VideoCapture) -> None:
    """
    Live threshold tuner.
    Left window: original feed.
    Right window: binary dark mask.
    Adjust sliders until each crab = one clean white blob.
    Press ESC to print final values.
    """
    print("\nCalibration mode — adjust sliders until crabs are clean white blobs.")
    print("Press ESC to exit and print values.\n")

    win = "Calibrate – Dark Mask"
    cv2.namedWindow(win)
    cv2.createTrackbar("Threshold",     win, BRIGHTNESS_THRESHOLD, 255, lambda v: None)
    cv2.createTrackbar("Close radius",  win, CLOSING_RADIUS,        40, lambda v: None)
    cv2.createTrackbar("Min area /10",  win, MIN_BLOB_AREA // 10,  500, lambda v: None)

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        thresh  = cv2.getTrackbarPos("Threshold",    win)
        radius  = cv2.getTrackbarPos("Close radius", win)
        min_a   = cv2.getTrackbarPos("Min area /10", win) * 10

        mask    = build_dark_mask(frame, thresh, radius)
        objects = detect_objects(mask, max(min_a, 1))

        preview = draw_results(frame, mask, objects)
        cv2.imshow(win, mask)
        cv2.imshow("Live preview", preview)

        if cv2.waitKey(5) == 27:
            print("# Paste these into the script or pass as args:")
            print(f"  BRIGHTNESS_THRESHOLD = {thresh}")
            print(f"  CLOSING_RADIUS       = {radius}")
            print(f"  MIN_BLOB_AREA        = {min_a}")
            break

    cv2.destroyAllWindows()


def run_on_image(path: str, threshold: int, closing_radius: int, min_area: int) -> None:
    """Process a single image file and display/save result."""
    frame = cv2.imread(path)
    if frame is None:
        print(f"Cannot read image: {path}")
        sys.exit(1)

    mask    = build_dark_mask(frame, threshold, closing_radius)
    objects = detect_objects(mask, min_area)
    result  = draw_results(frame, mask, objects)

    print(f"\nImage: {path}")
    print(f"Objects detected: {len(objects)}")
    for obj in objects:
        print(f"  #{obj['id']}  area={obj['area']}px  "
              f"bbox=({obj['x']},{obj['y']},{obj['w']},{obj['h']})")

    out_path = path.rsplit(".", 1)[0] + "_detected.jpg"
    cv2.imwrite(out_path, result)
    print(f"Result saved: {out_path}")

    cv2.imshow("Result", result)
    cv2.imshow("Mask",   mask)
    print("Press any key to close.")
    cv2.waitKey(0)
    cv2.destroyAllWindows()


# Global reference so draw_results can access current args for HUD text
args_global = None


def main() -> None:
    global args_global

    parser = argparse.ArgumentParser(description="Dark Spot Detector – count dark objects on light background")
    parser.add_argument("--camera",    type=int,   default=0)
    parser.add_argument("--image",     type=str,   default=None,
                        help="Path to a single image file instead of live camera")
    parser.add_argument("--calibrate", action="store_true",
                        help="Open live HSV/threshold tuning sliders")
    parser.add_argument("--threshold", type=int,   default=BRIGHTNESS_THRESHOLD,
                        help=f"Brightness cutoff for dark pixels (default {BRIGHTNESS_THRESHOLD})")
    parser.add_argument("--min-area",  type=int,   default=MIN_BLOB_AREA,
                        dest="min_area",
                        help=f"Minimum blob area in pixels (default {MIN_BLOB_AREA})")
    parser.add_argument("--closing",   type=int,   default=CLOSING_RADIUS,
                        help=f"Morphological closing radius (default {CLOSING_RADIUS})")

    args = parser.parse_args()
    args_global = args

    # --- Single image mode ---
    if args.image:
        run_on_image(args.image, args.threshold, args.closing, args.min_area)
        return

    # --- Camera mode ---
    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print(f"Cannot open camera {args.camera}")
        sys.exit(1)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if args.calibrate:
        calibrate_mode(cap)
        cap.release()
        return

    print("Dark Spot Detector running.")
    print("ESC = quit | 's' = snapshot | 'm' = show mask window\n")

    show_mask = False
    frame_n   = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            print("Camera read failed.")
            break

        mask    = build_dark_mask(frame, args.threshold, args.closing)
        objects = detect_objects(mask, args.min_area)
        output  = draw_results(frame, mask, objects)

        cv2.imshow("Dark Spot Detector", output)
        if show_mask:
            cv2.imshow("Mask", mask)

        if frame_n % 30 == 0:
            print(f"Frame {frame_n:05d} | Detected: {len(objects)} objects")

        key = cv2.waitKey(5)
        if key == 27:
            break
        elif key == ord('s'):
            fname = f"snapshot_{frame_n:05d}.jpg"
            cv2.imwrite(fname, output)
            print(f"Saved {fname}")
        elif key == ord('m'):
            show_mask = not show_mask
            if not show_mask:
                cv2.destroyWindow("Mask")

        frame_n += 1

    cap.release()
    cv2.destroyAllWindows()
    print(f"\nFinal frame object count: {len(objects)}")


if __name__ == "__main__":
    main()
"""
Devil Fish ROV - MATE 2026 Crab Detector
========================================
Task 2.1: Mitigate Invasive Species
  - COUNT: European Green Crab (Carcinus maenas)
  - IGNORE: Native Rock Crab (Cancer irroratus)
  - IGNORE: Native Jonah Crab (Cancer borealis)

Setup note:
  Crab images are printed and laminated onto a 50x50 cm sheet.
  You are detecting printed photos through water plus an ROV camera, not live crabs.
  Tune HSV ranges in-pool with --calibrate before a run.

Usage:
  python hsv_crab_detector.py
  python hsv_crab_detector.py --camera 0
  python hsv_crab_detector.py --calibrate
"""

import argparse
import sys

import cv2
import numpy as np


SPECIES = {
    "European Green Crab": {
        "lower": np.array([28, 25, 25]),
        "upper": np.array([88, 180, 180]),
        "color_bgr": (60, 200, 60),
        "min_area": 500,
        "count": True,
    },
    "Rock Crab (distractor)": {
        "lower": np.array([165, 40, 40]),
        "upper": np.array([14, 220, 220]),
        "color_bgr": (80, 80, 220),
        "min_area": 500,
        "count": False,
    },
    "Jonah Crab (distractor)": {
        "lower": np.array([0, 50, 50]),
        "upper": np.array([18, 230, 230]),
        "color_bgr": (60, 120, 220),
        "min_area": 500,
        "count": False,
    },
}

MORPH_KERNEL = np.ones((5, 5), np.uint8)
FONT = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE = 0.55
FONT_THICK = 1


def build_species_mask(hsv_frame: np.ndarray, lower: np.ndarray, upper: np.ndarray) -> np.ndarray:
    """Build a binary mask for one species' HSV range."""
    if lower[0] <= upper[0]:
        mask = cv2.inRange(hsv_frame, lower, upper)
    else:
        mask_a = cv2.inRange(lowerb:=hsv_frame, lower, np.array([179, upper[1], upper[2]]))
        mask_b = cv2.inRange(lowerb, np.array([0, lower[1], lower[2]]), upper)
        mask = cv2.bitwise_or(mask_a, mask_b)

    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, MORPH_KERNEL)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, MORPH_KERNEL)
    return mask


def detect_blobs(mask: np.ndarray, min_area: int) -> list[dict]:
    """Run connected components and filter by area."""
    n, labels, stats, centroids = cv2.connectedComponentsWithStats(mask, connectivity=4)
    blobs = []
    for i in range(1, n):
        area = stats[i, cv2.CC_STAT_AREA]
        if area < min_area:
            continue
        blobs.append(
            {
                "x": stats[i, cv2.CC_STAT_LEFT],
                "y": stats[i, cv2.CC_STAT_TOP],
                "w": stats[i, cv2.CC_STAT_WIDTH],
                "h": stats[i, cv2.CC_STAT_HEIGHT],
                "cx": int(centroids[i][0]),
                "cy": int(centroids[i][1]),
                "area": int(area),
            }
        )
    return blobs


def draw_hud(frame: np.ndarray, detections: dict[str, list[dict]]) -> np.ndarray:
    """Draw boxes and a count summary on the frame."""
    overlay = frame.copy()

    for species_name, blobs in detections.items():
        cfg = SPECIES[species_name]
        bgr = cfg["color_bgr"]
        for blob in blobs:
            x, y, w, h = blob["x"], blob["y"], blob["w"], blob["h"]
            thickness = 2 if cfg["count"] else 1
            cv2.rectangle(overlay, (x, y), (x + w, y + h), bgr, thickness)
            label = f"{species_name.split()[0]} {species_name.split()[1]}"
            cv2.putText(
                overlay,
                label,
                (x, max(y - 5, 14)),
                FONT,
                0.45,
                bgr,
                1,
                cv2.LINE_AA,
            )

    px, py = 10, 10
    line_height = 22
    panel_height = 14 + line_height * (len(detections) + 2)
    cv2.rectangle(overlay, (px, py), (px + 250, py + panel_height), (20, 20, 20), -1)
    cv2.rectangle(overlay, (px, py), (px + 250, py + panel_height), (180, 180, 180), 1)

    cv2.putText(
        overlay,
        "MATE 2026 - TASK 2.1",
        (px + 8, py + 16),
        FONT,
        0.45,
        (220, 220, 220),
        1,
        cv2.LINE_AA,
    )

    for idx, (species_name, blobs) in enumerate(detections.items()):
        cfg = SPECIES[species_name]
        bgr = cfg["color_bgr"]
        count = len(blobs)
        tag = " [SCORE]" if cfg["count"] else " [distractor]"
        text = f"{species_name.split()[0]} {species_name.split()[1]}: {count}{tag}"
        ty = py + 16 + line_height * (idx + 1)
        cv2.putText(
            overlay,
            text,
            (px + 8, ty),
            FONT,
            FONT_SCALE * 0.85,
            bgr,
            1,
            cv2.LINE_AA,
        )

    scored_count = sum(len(blobs) for name, blobs in detections.items() if SPECIES[name]["count"])
    cv2.putText(
        overlay,
        f"GREEN CRABS: {scored_count}",
        (px + 8, py + 16 + line_height * (len(detections) + 1)),
        FONT,
        0.65,
        (60, 240, 60),
        2,
        cv2.LINE_AA,
    )

    return cv2.addWeighted(overlay, 0.87, frame, 0.13, 0)


def calibrate_mode(cap: cv2.VideoCapture) -> None:
    """Interactive HSV tuning window for the printed-crab setup."""
    species_list = list(SPECIES.keys())
    print("\nAvailable species to calibrate:")
    for index, species_name in enumerate(species_list):
        print(f"  [{index}] {species_name}")
    idx = int(input("Select index: "))
    target = species_list[idx]

    print(f"\nCalibrating: {target}")
    print("Adjust sliders until only the target crab is white in the mask window. ESC to finish.\n")

    win = f"HSV Calibrate - {target}"
    cv2.namedWindow(win)
    init = SPECIES[target]
    cv2.createTrackbar("H low", win, int(init["lower"][0]), 179, lambda value: None)
    cv2.createTrackbar("S low", win, int(init["lower"][1]), 255, lambda value: None)
    cv2.createTrackbar("V low", win, int(init["lower"][2]), 255, lambda value: None)
    cv2.createTrackbar("H high", win, int(init["upper"][0]), 179, lambda value: None)
    cv2.createTrackbar("S high", win, int(init["upper"][1]), 255, lambda value: None)
    cv2.createTrackbar("V high", win, int(init["upper"][2]), 255, lambda value: None)

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        hl = cv2.getTrackbarPos("H low", win)
        sl = cv2.getTrackbarPos("S low", win)
        vl = cv2.getTrackbarPos("V low", win)
        hh = cv2.getTrackbarPos("H high", win)
        sh = cv2.getTrackbarPos("S high", win)
        vh = cv2.getTrackbarPos("V high", win)

        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, np.array([hl, sl, vl]), np.array([hh, sh, vh]))
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, MORPH_KERNEL)

        cv2.imshow(win, mask)
        cv2.imshow("Live Feed", frame)
        if cv2.waitKey(5) == 27:
            print(f"\n# Paste into SPECIES['{target}']:")
            print(f'    "lower": np.array([{hl}, {sl}, {vl}]),')
            print(f'    "upper": np.array([{hh}, {sh}, {vh}]),')
            break

    cv2.destroyAllWindows()


def main(args: argparse.Namespace) -> None:
    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print(f"Error: cannot open camera {args.camera}")
        sys.exit(1)

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if args.calibrate:
        calibrate_mode(cap)
        cap.release()
        return

    print("Devil Fish ROV - MATE 2026 Crab Detector")
    print("Target: European Green Crab | Distractors: Rock, Jonah")
    print("ESC to quit | 's' to snapshot\n")

    frame_n = 0
    detections = {}
    while True:
        ret, frame = cap.read()
        if not ret:
            print("Camera read failed.")
            break

        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        detections = {}
        for name, cfg in SPECIES.items():
            mask = build_species_mask(hsv, cfg["lower"], cfg["upper"])
            blobs = detect_blobs(mask, cfg["min_area"])
            detections[name] = blobs

        output = draw_hud(frame, detections)
        cv2.imshow("Devil Fish ROV - MATE 2026", output)

        if frame_n % 30 == 0:
            scored = sum(len(blobs) for name, blobs in detections.items() if SPECIES[name]["count"])
            summary = " | ".join(f"{name.split()[0]}: {len(blobs)}" for name, blobs in detections.items())
            print(f"Frame {frame_n:05d} | GREEN CRABS (score): {scored} | {summary}")

        key = cv2.waitKey(5)
        if key == 27:
            break
        if key == ord("s"):
            fname = f"snapshot_{frame_n:05d}.jpg"
            cv2.imwrite(fname, output)
            print(f"Saved {fname}")

        frame_n += 1

    cap.release()
    cv2.destroyAllWindows()

    final = sum(len(blobs) for name, blobs in detections.items() if SPECIES[name]["count"])
    print(f"\nFinal European Green Crab count: {final}")
    print("Upload this to the MATE Invasive Species Reporting Form.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Devil Fish ROV - MATE 2026 Crab Detector")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument(
        "--calibrate",
        action="store_true",
        help="Interactive HSV tuning for printed crab images",
    )
    main(parser.parse_args())
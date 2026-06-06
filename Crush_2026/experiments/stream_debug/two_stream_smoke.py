import argparse
import os
import tkinter as tk

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import numpy as np
except ImportError:
    np = None


DEFAULT_BLUEOS_IP = "192.168.2.2"
DEFAULT_RTSP_PORT = "8554"
DEFAULT_LEFT_STREAM = "video_stream__dev_video1"
DEFAULT_RIGHT_STREAM = "video_stream__dev_cam_right"
DEFAULT_WINDOW_WIDTH = 1280
DEFAULT_WINDOW_HEIGHT = 720
READ_FAILURE_LIMIT = 20
STREAM_GAP = 0


def configure_opencv_ffmpeg_capture_options() -> None:
    os.environ["OPENCV_LOG_LEVEL"] = "ERROR"
    os.environ["OPENCV_FFMPEG_LOGLEVEL"] = "8"
    os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = (
        "protocol_whitelist;file,http,https,tcp,tls,udp,rtp"
        "|analyzeduration;2000000"
        "|probesize;2000000"
        "|fflags;nobuffer+discardcorrupt"
        "|flags;low_delay"
    )


def require_dependencies() -> None:
    missing = []
    if cv2 is None:
        missing.append("OpenCV")
    if np is None:
        missing.append("numpy")
    if missing:
        raise SystemExit(f"Missing dependencies for this script: {', '.join(missing)}")
    configure_opencv_ffmpeg_capture_options()


def build_stream_url(blueos_ip: str, rtsp_port: str, stream_name: str, explicit_url: str | None) -> str:
    if explicit_url:
        return explicit_url
    return f"rtsp://{blueos_ip}:{rtsp_port}/{stream_name}"


def rotate_frame(frame, quarter_turns: int):
    if frame is None:
        return None

    turns = quarter_turns % 4
    if turns == 0:
        return frame
    if turns == 1:
        return cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)
    if turns == 2:
        return cv2.rotate(frame, cv2.ROTATE_180)
    return cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)


def fill_frame(frame, width: int, height: int):
    canvas = np.zeros((height, width, 3), dtype=np.uint8)
    if frame is None:
        return canvas

    source_h, source_w = frame.shape[:2]
    scale = max(width / max(source_w, 1), height / max(source_h, 1))
    target_w = max(1, int(source_w * scale))
    target_h = max(1, int(source_h * scale))
    interpolation = cv2.INTER_AREA if scale < 1.0 else cv2.INTER_LINEAR
    resized = cv2.resize(frame, (target_w, target_h), interpolation=interpolation)

    crop_x = max(0, (target_w - width) // 2)
    crop_y = max(0, (target_h - height) // 2)
    return resized[crop_y:crop_y + height, crop_x:crop_x + width]


class StreamCapture:
    def __init__(self, source: str):
        self.source = source
        self.capture = None
        self.last_frame = None
        self.read_failures = 0

    def open(self) -> None:
        self.close()
        self.capture = cv2.VideoCapture(self.source)
        if hasattr(cv2, "CAP_PROP_BUFFERSIZE"):
            self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        if not self.capture.isOpened():
            self.close()

    def read(self):
        if self.capture is None:
            self.open()

        if self.capture is None:
            return self.last_frame

        ok, frame = self.capture.read()
        if ok and frame is not None:
            self.last_frame = frame
            self.read_failures = 0
            return frame

        self.read_failures += 1
        if self.read_failures >= READ_FAILURE_LIMIT:
            self.open()
            self.read_failures = 0
        return self.last_frame

    def close(self) -> None:
        if self.capture is not None:
            self.capture.release()
        self.capture = None


class DualStreamApp:
    def __init__(self, args: argparse.Namespace):
        self.left_stream = StreamCapture(build_stream_url(args.blueos_ip, args.port, args.left_stream, args.left_url))
        self.right_stream = StreamCapture(build_stream_url(args.blueos_ip, args.port, args.right_stream, args.right_url))
        self.horizontal_ratio = max(0.1, args.horizontal_ratio)
        self.vertical_ratio = max(0.1, args.vertical_ratio)
        self.rotation_steps = 0
        self.frame_delay_ms = max(1, int(1000 / max(args.fps, 1)))

        self.root = tk.Tk()
        self.root.title(args.window_name)
        self.root.geometry(f"{args.width}x{args.height}")
        self.root.configure(bg="black")

        self.video_label = tk.Label(self.root, bg="black", bd=0, highlightthickness=0)
        self.video_label.pack(fill=tk.BOTH, expand=True)

        self.root.bind("<KeyPress-l>", self.rotate_left)
        self.root.bind("<KeyPress-L>", self.rotate_left)
        self.root.bind("<KeyPress-r>", self.rotate_right)
        self.root.bind("<KeyPress-R>", self.rotate_right)
        self.root.bind("<Escape>", self.close)
        self.root.bind("<KeyPress-q>", self.close)
        self.root.bind("<KeyPress-Q>", self.close)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.tk_image = None
        self.running = True

        print("Starting dual-stream viewer. Press q or esc to quit.")
        print("Controls: l rotates both streams left 90 deg, r rotates both streams right 90 deg.")
        print(f"Left stream:  {self.left_stream.source}")
        print(f"Right stream: {self.right_stream.source}")

    def rotate_left(self, _event=None) -> None:
        self.rotation_steps = (self.rotation_steps - 1) % 4
        self.apply_preferred_window_ratio()

    def rotate_right(self, _event=None) -> None:
        self.rotation_steps = (self.rotation_steps + 1) % 4
        self.apply_preferred_window_ratio()

    def close(self, _event=None) -> None:
        self.running = False
        self.left_stream.close()
        self.right_stream.close()
        self.root.destroy()

    def compute_canvas_geometry(self, width: int, height: int) -> tuple[str, int, int]:
        landscape = self.rotation_steps % 2 == 0
        # Requested behavior:
        # - Horizontal stream orientation => panes stacked up/down
        # - Vertical stream orientation => panes side-by-side
        layout = "vertical" if landscape else "horizontal"
        return layout, width, height

    def apply_preferred_window_ratio(self) -> None:
        current_w = max(self.root.winfo_width(), 320)
        current_h = max(self.root.winfo_height(), 240)
        target_ratio = self.horizontal_ratio if (self.rotation_steps % 2 == 0) else self.vertical_ratio

        if target_ratio <= 0:
            return

        new_w = max(320, int(current_h * target_ratio))
        self.root.geometry(f"{new_w}x{current_h}")

    def build_display(self) -> np.ndarray:
        win_w = max(self.root.winfo_width(), 320)
        win_h = max(self.root.winfo_height(), 240)
        layout, canvas_w, canvas_h = self.compute_canvas_geometry(win_w, win_h)

        left_frame = rotate_frame(self.left_stream.read(), self.rotation_steps)
        right_frame = rotate_frame(self.right_stream.read(), self.rotation_steps)

        if layout == "horizontal":
            pane_w = max(1, (canvas_w - STREAM_GAP) // 2)
            pane_h = canvas_h
            right_w = max(1, canvas_w - pane_w - STREAM_GAP)
            left_pane = fill_frame(left_frame, pane_w, pane_h)
            right_pane = fill_frame(right_frame, right_w, pane_h)
            composed = np.hstack([left_pane, np.zeros((pane_h, STREAM_GAP, 3), dtype=np.uint8), right_pane])
        else:
            pane_w = canvas_w
            pane_h = max(1, (canvas_h - STREAM_GAP) // 2)
            bottom_h = max(1, canvas_h - pane_h - STREAM_GAP)
            left_pane = fill_frame(left_frame, pane_w, pane_h)
            right_pane = fill_frame(right_frame, pane_w, bottom_h)
            composed = np.vstack([left_pane, np.zeros((STREAM_GAP, pane_w, 3), dtype=np.uint8), right_pane])
        return composed

    def render(self) -> None:
        if not self.running:
            return

        frame_bgr = self.build_display()
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        h, w = frame_rgb.shape[:2]

        # Tk PhotoImage accepts PPM bytes directly; this avoids extra image dependencies.
        ppm_data = f"P6 {w} {h} 255\n".encode("ascii") + frame_rgb.tobytes()
        self.tk_image = tk.PhotoImage(data=ppm_data, format="PPM")
        self.video_label.configure(image=self.tk_image)

        self.root.after(self.frame_delay_ms, self.render)

    def run(self) -> None:
        self.render()
        self.root.mainloop()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Minimal dual-stream Tkinter viewer")
    parser.add_argument("--blueos-ip", default=DEFAULT_BLUEOS_IP)
    parser.add_argument("--port", default=DEFAULT_RTSP_PORT)
    parser.add_argument("--left-stream", default=DEFAULT_LEFT_STREAM)
    parser.add_argument("--right-stream", default=DEFAULT_RIGHT_STREAM)
    parser.add_argument("--left-url", default=None, help="Full left stream URL override")
    parser.add_argument("--right-url", default=None, help="Full right stream URL override")
    parser.add_argument("--window-name", default="BlueOS Dual Stream (Tk)")
    parser.add_argument("--width", type=int, default=DEFAULT_WINDOW_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_WINDOW_HEIGHT)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument(
        "--horizontal-ratio",
        type=float,
        default=16.0 / 9.0,
        help="Canvas width/height ratio when videos are in landscape orientation",
    )
    parser.add_argument(
        "--vertical-ratio",
        type=float,
        default=9.0 / 16.0,
        help="Canvas width/height ratio when videos are in portrait orientation",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    require_dependencies()
    app = DualStreamApp(args)
    app.run()


if __name__ == "__main__":
    main()
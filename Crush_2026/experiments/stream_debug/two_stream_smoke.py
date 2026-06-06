import argparse
import os
import time

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
DEFAULT_PANEL_WIDTH = 640
DEFAULT_PANEL_HEIGHT = 360
DEFAULT_WINDOW_NAME = "BlueOS Dual Stream"
RECONNECT_SECONDS = 1.5
READ_FAILURE_LIMIT = 20


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


def require_opencv() -> None:
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


def wrap_text(text: str, width: int = 48) -> list[str]:
    words = text.split()
    if not words:
        return [""]

    lines = []
    current = words[0]
    for word in words[1:]:
        if len(current) + 1 + len(word) <= width:
            current = f"{current} {word}"
        else:
            lines.append(current)
            current = word
    lines.append(current)
    return lines


class StreamCapture:
    def __init__(self, label: str, source: str):
        self.label = label
        self.source = source
        self.capture = None
        self.latest_frame = None
        self.status = "Waiting to connect."
        self.next_retry_at = 0.0
        self.read_failures = 0

    def open(self) -> None:
        self.close()

        capture = cv2.VideoCapture(self.source)
        if hasattr(cv2, "CAP_PROP_BUFFERSIZE"):
            capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        if capture.isOpened():
            self.capture = capture
            self.status = "Streaming."
            self.read_failures = 0
            return

        capture.release()
        self.capture = None
        self.status = f"Unable to open stream. Retrying in {RECONNECT_SECONDS:.1f}s."
        self.next_retry_at = time.monotonic() + RECONNECT_SECONDS

    def update(self):
        now = time.monotonic()
        if self.capture is None:
            if now >= self.next_retry_at:
                self.open()
            return self.latest_frame

        ok, frame = self.capture.read()
        if ok and frame is not None:
            self.latest_frame = frame
            self.read_failures = 0
            self.status = "Streaming."
            return self.latest_frame

        self.read_failures += 1
        if self.read_failures >= READ_FAILURE_LIMIT:
            self.close()
            self.status = f"No frames received. Retrying in {RECONNECT_SECONDS:.1f}s."
            self.next_retry_at = now + RECONNECT_SECONDS
        else:
            self.status = f"Frame miss {self.read_failures}/{READ_FAILURE_LIMIT}."
        return self.latest_frame

    def close(self) -> None:
        if self.capture is not None:
            self.capture.release()
        self.capture = None


def render_panel(
    frame,
    title: str,
    source: str,
    status: str,
    width: int,
    height: int,
):
    if frame is None:
        pane = np.zeros((height, width, 3), dtype=np.uint8)
        pane[:] = (24, 24, 24)
    else:
        pane = cv2.resize(frame, (width, height))

    overlay_height = min(140, height - 20)
    cv2.rectangle(pane, (10, 10), (width - 10, overlay_height), (20, 20, 20), -1)
    cv2.rectangle(pane, (10, 10), (width - 10, overlay_height), (180, 180, 180), 1)
    cv2.putText(pane, title, (24, 38), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (80, 240, 80), 2, cv2.LINE_AA)
    cv2.putText(pane, "q or esc = quit", (24, 62), cv2.FONT_HERSHEY_SIMPLEX, 0.46, (220, 220, 220), 1, cv2.LINE_AA)

    line_y = 86
    for line in [f"Source: {source}", f"Status: {status}"]:
        for wrapped in wrap_text(line):
            if line_y > overlay_height - 12:
                break
            cv2.putText(
                pane,
                wrapped,
                (24, line_y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.45,
                (120, 180, 255),
                1,
                cv2.LINE_AA,
            )
            line_y += 20

    return pane


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Show two BlueOS video streams in one window")
    parser.add_argument("--blueos-ip", default=DEFAULT_BLUEOS_IP)
    parser.add_argument("--port", default=DEFAULT_RTSP_PORT)
    parser.add_argument("--left-stream", default=DEFAULT_LEFT_STREAM)
    parser.add_argument("--right-stream", default=DEFAULT_RIGHT_STREAM)
    parser.add_argument("--left-url", default=None, help="Full left stream URL override")
    parser.add_argument("--right-url", default=None, help="Full right stream URL override")
    parser.add_argument("--panel-width", type=int, default=DEFAULT_PANEL_WIDTH)
    parser.add_argument("--panel-height", type=int, default=DEFAULT_PANEL_HEIGHT)
    parser.add_argument("--window-name", default=DEFAULT_WINDOW_NAME)
    parser.add_argument("--fullscreen", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    require_opencv()

    left_url = build_stream_url(args.blueos_ip, args.port, args.left_stream, args.left_url)
    right_url = build_stream_url(args.blueos_ip, args.port, args.right_stream, args.right_url)

    left_stream = StreamCapture("Left", left_url)
    right_stream = StreamCapture("Right", right_url)
    left_stream.open()
    right_stream.open()

    cv2.namedWindow(args.window_name, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(args.window_name, args.panel_width * 2, args.panel_height)
    if args.fullscreen:
        cv2.setWindowProperty(args.window_name, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    print("Starting dual-stream viewer. Press q or esc to quit.")
    print(f"Left stream:  {left_url}")
    print(f"Right stream: {right_url}")

    try:
        while True:
            left_frame = left_stream.update()
            right_frame = right_stream.update()

            display = np.hstack(
                [
                    render_panel(
                        left_frame,
                        left_stream.label,
                        left_stream.source,
                        left_stream.status,
                        args.panel_width,
                        args.panel_height,
                    ),
                    render_panel(
                        right_frame,
                        right_stream.label,
                        right_stream.source,
                        right_stream.status,
                        args.panel_width,
                        args.panel_height,
                    ),
                ]
            )

            cv2.imshow(args.window_name, display)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
    finally:
        left_stream.close()
        right_stream.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
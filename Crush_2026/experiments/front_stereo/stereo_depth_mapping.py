import argparse
import queue
import threading
from urllib.parse import urlsplit

import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None


DEFAULT_BLUEOS_IP = "192.168.2.2"
DEFAULT_ANALYST_HOST = "192.168.2.112"
DEFAULT_RTSP_PORT = "8554"
DEFAULT_LEFT_STREAM = "video1"
DEFAULT_RIGHT_STREAM = "video2"
DEFAULT_LEFT_URL = "udp://192.168.2.112:5600"
DEFAULT_RIGHT_URL = "udp://192.168.2.1:5600"
DEFAULT_LEFT_MAP_X_PATH = "left_map_x.npy"
DEFAULT_LEFT_MAP_Y_PATH = "left_map_y.npy"
DEFAULT_RIGHT_MAP_X_PATH = "right_map_x.npy"
DEFAULT_RIGHT_MAP_Y_PATH = "right_map_y.npy"


def build_stream_url(blueos_ip: str, rtsp_port: str, stream_name: str, explicit_url: str | None) -> str:
    if explicit_url:
        return explicit_url
    return f"rtsp://{blueos_ip}:{rtsp_port}/{stream_name}"


def normalize_stream_source_url(stream_url: str, analyst_host: str) -> str:
    parsed = urlsplit(stream_url)
    if parsed.scheme != "udp":
        return stream_url

    port = parsed.port
    host = parsed.hostname or ""
    if port is None:
        raise SystemExit(f"UDP stream URL is missing a port: {stream_url}")

    if host in {"", "0.0.0.0", "127.0.0.1", "localhost", analyst_host}:
        return f"udp://@:{port}"

    raise SystemExit(
        "Configured UDP stream points to a different host than this analyst machine. "
        f"Stream: {stream_url} | analyst host: {analyst_host}. "
        f"Reconfigure BlueOS to send this stream to {analyst_host}:{port}, or run the receiver on {host}."
    )


def require_opencv() -> None:
    if cv2 is None:
        raise SystemExit("OpenCV is required to run this script. Install dependencies from requirements.txt.")

class VideoStreamThread:
    """Threaded video capture to eliminate RTSP buffer lag"""
    def __init__(self, src):
        self.cap = cv2.VideoCapture(src)
        self.q = queue.Queue(maxsize=2)
        self.stopped = False
        
    def start(self):
        t = threading.Thread(target=self.update, args=())
        t.daemon = True
        t.start()
        return self
        
    def update(self):
        while not self.stopped:
            if not self.q.full():
                ret, frame = self.cap.read()
                if not ret:
                    continue
                self.q.put(frame)
                
    def read(self):
        return self.q.get() if not self.q.empty() else None


def run_stereo_mapping(
    blueos_ip: str,
    analyst_host: str,
    rtsp_port: str,
    left_stream_name: str,
    right_stream_name: str,
    left_stream_url: str | None,
    right_stream_url: str | None,
    left_map_x_path: str,
    left_map_y_path: str,
    right_map_x_path: str,
    right_map_y_path: str,
) -> None:
    require_opencv()

    left_map_x = np.load(left_map_x_path)
    left_map_y = np.load(left_map_y_path)
    right_map_x = np.load(right_map_x_path)
    right_map_y = np.load(right_map_y_path)

    rtsp_left = normalize_stream_source_url(
        build_stream_url(blueos_ip, rtsp_port, left_stream_name, left_stream_url),
        analyst_host,
    )
    rtsp_right = normalize_stream_source_url(
        build_stream_url(blueos_ip, rtsp_port, right_stream_name, right_stream_url),
        analyst_host,
    )
    left_stream = VideoStreamThread(rtsp_left).start()
    right_stream = VideoStreamThread(rtsp_right).start()

    stereo = cv2.StereoSGBM_create(
        minDisparity=0,
        numDisparities=64,
        blockSize=5,
        P1=8 * 3 * 5 ** 2,
        P2=32 * 3 * 5 ** 2,
        disp12MaxDiff=1,
        uniquenessRatio=15,
        speckleWindowSize=100,
        speckleRange=2,
        mode=cv2.STEREO_SGBM_MODE_SGBM_3WAY,
    )

    left_window_name = f"Analyst Camera View (Left) - {analyst_host}"
    depth_window_name = f"Analyst Real-Time 3D Depth Map - {analyst_host}"

    cv2.namedWindow(left_window_name, cv2.WINDOW_NORMAL)
    cv2.namedWindow(depth_window_name, cv2.WINDOW_NORMAL)

    print(
        "Starting 3D Mapping & Detection Pipeline "
        f"for analyst host {analyst_host}. Press 'q' to quit."
    )

    while True:
        frame_l = left_stream.read()
        frame_l_right = right_stream.read()

        if frame_l is None or frame_l_right is None:
            continue

        rectified_l = cv2.remap(frame_l, left_map_x, left_map_y, cv2.INTER_LINEAR)
        rectified_r = cv2.remap(frame_l_right, right_map_x, right_map_y, cv2.INTER_LINEAR)

        gray_l = cv2.cvtColor(rectified_l, cv2.COLOR_BGR2GRAY)
        gray_r = cv2.cvtColor(rectified_r, cv2.COLOR_BGR2GRAY)

        disparity = stereo.compute(gray_l, gray_r).astype(np.float32) / 16.0

        depth_map_visual = cv2.normalize(
            disparity,
            None,
            alpha=0,
            beta=255,
            norm_type=cv2.NORM_MINMAX,
            dtype=cv2.CV_8U,
        )
        depth_map_color = cv2.applyColorMap(depth_map_visual, cv2.COLORMAP_JET)

        cv2.imshow(left_window_name, rectified_l)
        cv2.imshow(depth_window_name, depth_map_color)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cv2.destroyAllWindows()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Front stereo depth mapping smoke runner")
    parser.add_argument("--blueos-ip", default=DEFAULT_BLUEOS_IP)
    parser.add_argument("--analyst-host", default=DEFAULT_ANALYST_HOST)
    parser.add_argument("--port", default=DEFAULT_RTSP_PORT)
    parser.add_argument("--left-stream", default=DEFAULT_LEFT_STREAM)
    parser.add_argument("--right-stream", default=DEFAULT_RIGHT_STREAM)
    parser.add_argument(
        "--left-url",
        default=DEFAULT_LEFT_URL,
        help="Full left stream URL override, for example udp://@:5600",
    )
    parser.add_argument(
        "--right-url",
        default=DEFAULT_RIGHT_URL,
        help="Full right stream URL override, for example udp://@:5601",
    )
    parser.add_argument("--left-map-x", default=DEFAULT_LEFT_MAP_X_PATH)
    parser.add_argument("--left-map-y", default=DEFAULT_LEFT_MAP_Y_PATH)
    parser.add_argument("--right-map-x", default=DEFAULT_RIGHT_MAP_X_PATH)
    parser.add_argument("--right-map-y", default=DEFAULT_RIGHT_MAP_Y_PATH)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    run_stereo_mapping(
        blueos_ip=args.blueos_ip,
        analyst_host=args.analyst_host,
        rtsp_port=args.port,
        left_stream_name=args.left_stream,
        right_stream_name=args.right_stream,
        left_stream_url=args.left_url or "udp://192.168.2.112:5600",
        right_stream_url=args.right_url or "udp://192.168.2.1:5600",
        left_map_x_path=args.left_map_x,
        left_map_y_path=args.left_map_y,
        right_map_x_path=args.right_map_x,
        right_map_y_path=args.right_map_y,
    )


if __name__ == "__main__":
    main()
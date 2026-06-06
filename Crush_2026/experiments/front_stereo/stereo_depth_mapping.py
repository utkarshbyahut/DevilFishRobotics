import cv2
import numpy as np
import threading
import queue

# --- Configuration & Calibration Parameters ---
# NOTE: You MUST populate these arrays by running an OpenCV chessboard 
# stereo calibration script on your specific camera rig beforehand!
LEFT_MAP_X, LEFT_MAP_Y = np.load("left_map_x.npy"), np.load("left_map_y.npy")
RIGHT_MAP_X, RIGHT_MAP_Y = np.load("right_map_x.npy"), np.load("right_map_y.npy")

# RTSP Stream URLs from BlueOS
RTSP_LEFT = "rtsp://192.168.2.2:8554/front_left"
RTSP_RIGHT = "rtsp://192.168.2.2:8554/front_right"

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

# Initialize Streams
left_stream = VideoStreamThread(RTSP_LEFT).start()
right_stream = VideoStreamThread(RTSP_RIGHT).start()

# Initialize the Semi-Global Block Matching Stereo Matcher
# These parameters dictate the sharpness and accuracy of the depth map
stereo = cv2.StereoSGBM_create(
    minDisparity=0,
    numDisparities=64,       # Must be divisible by 16
    blockSize=5,             # Smaller = sharper edges, larger = smoother surfaces
    P1=8 * 3 * 5 ** 2,
    P2=32 * 3 * 5 ** 2,
    disp12MaxDiff=1,
    uniquenessRatio=15,
    speckleWindowSize=100,
    speckleRange=2,
    mode=cv2.STEREO_SGBM_MODE_SGBM_3WAY
)

print("Starting 3D Mapping & Detection Pipeline. Press 'q' to quit.")

while True:
    frame_l = left_stream.read()
    frame_l_right = right_stream.read()
    
    if frame_l is None or frame_l_right is None:
        continue

    # 1. Rectify the Frames (Straightens lens distortion so lines match perfectly)
    rectified_l = cv2.remap(frame_l, LEFT_MAP_X, LEFT_MAP_Y, cv2.INTER_LINEAR)
    rectified_r = cv2.remap(frame_l_right, RIGHT_MAP_X, RIGHT_MAP_Y, cv2.INTER_LINEAR)
    
    # 2. Convert to Grayscale for Processing
    gray_l = cv2.cvtColor(rectified_l, cv2.COLOR_BGR2GRAY)
    gray_r = cv2.cvtColor(rectified_r, cv2.COLOR_BGR2GRAY)
    
    # 3. Compute Stereo Disparity Map
    disparity = stereo.compute(gray_l, gray_r).astype(np.float32) / 16.0
    
    # Normalize disparity to 0-255 so we can visualize it easily
    depth_map_visual = cv2.normalize(disparity, None, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    depth_map_color = cv2.applyColorMap(depth_map_visual, cv2.COLORMAP_JET) # Closer objects turn red, far away turns blue
    
    # 4. Integrate Your Crab Detection Code Here!
    # Simply run your HSV masking and blob logic directly on the 'rectified_l' frame
    # processed_output = draw_hud(rectified_l, detections)
    
    # Display the results on the Analyst Monitor
    cv2.imshow("Analyst Camera View (Left)", rectified_l)
    cv2.imshow("Analyst Real-Time 3D Depth Map", depth_map_color)
    
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
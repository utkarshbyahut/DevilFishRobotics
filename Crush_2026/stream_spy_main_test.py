import cv2
from ultralytics import YOLO
from pymavlink import mavutil

# 1. Connect to BlueOS RTSP Stream (Replace with your ROV's Topside IP and Stream Port)
# BlueOS typically streams RTSP on port 8554
ROV_IP = "192.168.2.2" 
STREAM_PORT = "8554"
STREAM_NAME = "video1"  # Check your BlueOS Video Manager for the exact stream path
rtsp_url = f"rtsp://{ROV_IP}:{STREAM_PORT}/{STREAM_NAME}"

# 2. Load your model (Runs natively on surface GPU if available)
model = YOLO("yolov8n.pt") 

# 3. Optional: Connect to BlueOS MAVLink endpoint for sending commands back down
# mav_connection = mavutil.mavlink_connection('udp:192.168.2.2:14550')

cap = cv2.VideoCapture(rtsp_url)

if not cap.isOpened():
    print("Error: Could not connect to the BlueOS video stream.")
    exit()

while True:
    ret, frame = cap.read()
    if not ret:
        print("Dropped frame or stream interrupted.")
        break

    # Run high-speed inference on the surface PC
    results = model(frame, verbose=False)

    # Process detections
    for box in results[0].boxes:
        coords = box.xyxy[0].tolist()  # [xmin, ymin, xmax, ymax]
        class_id = int(box.cls[0])
        confidence = float(box.conf[0])
        
        # Draw bounding boxes on your topside monitor
        cv2.rectangle(frame, (int(coords[0]), int(coords[1])), (int(coords[2]), int(coords[3])), (0, 255, 0), 2)
        
        # Example Hook: If confidence is high, trigger autonomous tracking calculations here
        # target_x = (coords[0] + coords[2]) / 2
        # send_mavlink_tracking_correction(target_x)

    # Display the processed stream inside your topside cockpit layout
    cv2.imshow("Topside ROV Object Detection", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()

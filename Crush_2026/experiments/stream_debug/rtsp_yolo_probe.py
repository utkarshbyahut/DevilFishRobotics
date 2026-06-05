import cv2
from pymavlink import mavutil
from ultralytics import YOLO


ROV_IP = "192.168.2.2"
STREAM_PORT = "8554"
STREAM_NAME = "video1"
rtsp_url = f"rtsp://{ROV_IP}:{STREAM_PORT}/{STREAM_NAME}"

model = YOLO("yolov8n.pt")

# mav_connection = mavutil.mavlink_connection("udp:192.168.2.2:14550")

cap = cv2.VideoCapture(rtsp_url)

if not cap.isOpened():
    print("Error: Could not connect to the BlueOS video stream.")
    raise SystemExit(1)

while True:
    ret, frame = cap.read()
    if not ret:
        print("Dropped frame or stream interrupted.")
        break

    results = model(frame, verbose=False)

    for box in results[0].boxes:
        coords = box.xyxy[0].tolist()
        class_id = int(box.cls[0])
        confidence = float(box.conf[0])

        cv2.rectangle(
            frame,
            (int(coords[0]), int(coords[1])),
            (int(coords[2]), int(coords[3])),
            (0, 255, 0),
            2,
        )

        _ = (class_id, confidence)

    cv2.imshow("Topside ROV Object Detection", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
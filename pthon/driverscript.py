#!/usr/bin/env python3
"""
PC video server for XIAO ESP32-S3 + ST7789.
Supports streaming a video file OR live PC screen capture resized to 320x240.

Run with video file:
  python video_server.py --video "C:\Videos\video.mp4"

Run with live PC screen:
  python video_server.py --video screen
  or
  python video_server.py --screen
"""
import argparse
import ctypes
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2
import numpy as np

# Try importing mss for screen capture
try:
    import mss
    HAS_MSS = True
except ImportError:
    HAS_MSS = False


def ensure_desktop_attached():
    """Attach current thread to interactive desktop if running on Windows."""
    if sys.platform == "win32":
        try:
            user32 = ctypes.windll.user32
            hdesk = user32.OpenInputDesktop(0, False, 0x01FF)
            if hdesk:
                user32.SetThreadDesktop(hdesk)
        except Exception:
            pass


class VideoHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        if self.path not in ("/", "/stream.mjpg"):
            self.send_error(404, "Use /stream.mjpg")
            return

        video_path = self.server.video_path
        fps = self.server.stream_fps
        quality = self.server.jpeg_quality
        frame_delay = 1.0 / max(1.0, fps)
        is_screen = (video_path.lower() in ("screen", "live"))

        cap = None
        sct = None
        monitor = None

        if is_screen:
            if not HAS_MSS:
                self.send_error(500, "Screen capture requires 'mss'. Install: pip install mss")
                return
            ensure_desktop_attached()
            sct = mss.mss()
            monitor = sct.monitors[1] if len(sct.monitors) > 1 else sct.monitors[0]
            print(f"[PC server] Streaming live screen: {monitor['width']}x{monitor['height']} -> 320x240")
        else:
            if not os.path.exists(video_path):
                self.send_error(404, f"Video file not found: {video_path}")
                return
            cap = cv2.VideoCapture(video_path)
            if not cap.isOpened():
                self.send_error(500, f"Could not open video: {video_path}")
                return
            print(f"[PC server] Streaming video file: {video_path}")

        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()

        encode_params = [int(cv2.IMWRITE_JPEG_QUALITY), quality]
        try:
            while True:
                started = time.monotonic()

                if is_screen:
                    # Live PC screen capture
                    sct_img = sct.grab(monitor)
                    frame = np.array(sct_img)
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
                else:
                    # Video file playback
                    ok, frame = cap.read()
                    if not ok:
                        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        ok, frame = cap.read()
                        if not ok:
                            break

                # Ensure resolution is exactly 320x240
                if frame.shape[1] != 320 or frame.shape[0] != 240:
                    frame = cv2.resize(frame, (320, 240), interpolation=cv2.INTER_AREA)

                encoded_ok, jpg = cv2.imencode(".jpg", frame, encode_params)
                if not encoded_ok:
                    continue

                jpg_bytes = jpg.tobytes()
                part_header = (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n"
                    + f"Content-Length: {len(jpg_bytes)}\r\n".encode("ascii")
                    + b"\r\n"
                )
                self.wfile.write(part_header)
                self.wfile.write(jpg_bytes)
                self.wfile.write(b"\r\n")
                self.wfile.flush()

                elapsed = time.monotonic() - started
                time.sleep(max(0.0, frame_delay - elapsed))

        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except OSError:
            # Usually means the ESP32 disconnected or restarted.
            pass
        finally:
            if cap is not None:
                cap.release()
            if sct is not None:
                sct.close()

    def log_message(self, fmt, *args):
        print("[PC server] " + (fmt % args))


def main():
    parser = argparse.ArgumentParser(description="PC Video & Screen Server for ESP32 (320x240)")
    parser.add_argument("--video", default="C:\\Videos\\video.mp4", help="Path to video file or 'screen' for live screen")
    parser.add_argument("--screen", action="store_true", help="Stream live PC screen directly")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--fps", type=float, default=15.0)
    parser.add_argument("--quality", type=int, default=60)
    args = parser.parse_args()

    video_source = "screen" if args.screen else args.video

    server = ThreadingHTTPServer((args.host, args.port), VideoHandler)
    server.daemon_threads = True
    server.video_path = video_source
    server.stream_fps = max(1.0, min(args.fps, 30.0))
    server.jpeg_quality = max(20, min(args.quality, 90))

    print("=" * 60)
    print(" Video server is running.")
    print(f" Source:      {video_source}")
    print(f" Resolution:  320x240 | FPS: {server.stream_fps} | Quality: {server.jpeg_quality}")
    print(f" Local URL:   http://127.0.0.1:{args.port}/stream.mjpg")
    print(f" ESP32 URL:   http://192.168.4.2:{args.port}/stream.mjpg")
    print(" Press Ctrl+C to stop.")
    print("=" * 60)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

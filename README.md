# ESP32-S3 Touchscreen PC Remote

A DIY PC remote-control project built using a **Seeed XIAO ESP32-S3, ST7789 320×240 display, and XPT2046 touchscreen**.

The ESP32 receives a live preview of the PC screen over Wi-Fi and sends touchscreen interactions back to the PC to control the mouse. A lightweight Python server handles screen capture, MJPEG streaming, and mouse control.

## Features

-  Live PC screen streaming over Wi-Fi
-  320×240 ST7789 TFT display
-  XPT2046 touchscreen input
-  Touch-to-mouse coordinate mapping
-  Touch-and-hold mouse button support
-  Python-based PC server
-  HTTP MJPEG streaming
-  No additional display controller or USB connection required for screen streaming and touch control

##  Hardware Required

| Component | Purpose |
|---|---|
| Seeed XIAO ESP32-S3 | Wi-Fi connectivity and processing |
| ST7789 TFT display (320×240) | PC screen preview |
| XPT2046 touchscreen | Mouse input |
| USB cable | Programming and powering the ESP32 |
| PC or laptop | Screen source and mouse-control server |
| Wi-Fi router | Network communication |

##  Wiring

The display and touchscreen share the SPI clock and data lines.

### ST7789 Display

| Display Pin | XIAO ESP32-S3 GPIO |
|---|---:|
| SCK / CLK | GPIO 7 |
| MOSI / SDA | GPIO 9 |
| CS | GPIO 4 |
| DC | GPIO 3 |
| RST | GPIO 2 |
| MISO | Not connected |
| VCC | According to your display module's specifications |
| GND | GND |

### XPT2046 Touchscreen

| Touch Pin | XIAO ESP32-S3 GPIO |
|---|---:|
| CLK | GPIO 7 |
| MOSI | GPIO 9 |
| MISO | GPIO 8 |
| CS | GPIO 5 |
| IRQ | GPIO 1 |
| VCC | According to your touch module's specifications |
| GND | GND |

**Important:** Verify your particular display and touchscreen module's voltage requirements before connecting power. The SPI clock and MOSI lines are shared, while the display and touchscreen use separate chip-select pins.

##  Project Structure

```text
esp32-s3-pc-remote/
├── README.md
├── pc_remote_control_server.py
└── esp32_screen_touch_control.ino
```

- `pc_remote_control_server.py` — Runs on the PC. Captures the desktop, streams MJPEG frames, and translates touch coordinates into mouse actions.
- `esp32_screen_touch_control.ino` — Runs on the ESP32. Receives and displays the stream and sends touchscreen events to the PC.

##  How It Works

```text
              PC / Laptop
        ┌─────────────────────┐
        │ Desktop Screen      │
        │        │            │
        │        ▼            │
        │ Python Screen       │
        │ Capture + MJPEG     │
        │ Server              │
        │        ▲            │
        │        │ HTTP POST  │
        │ Mouse Control       │
        └────────┬────────────┘
                 │ Wi-Fi
                 ▼
        ┌─────────────────────┐
        │ XIAO ESP32-S3        │
        │                     │
        │ MJPEG Receiver      │
        │        │            │
        │        ▼            │
        │ ST7789 Display      │
        │                     │
        │ XPT2046 Touch Input │
        └─────────────────────┘
```

1. The Python server captures the PC desktop.
2. The desktop is resized to 320×240 and encoded as JPEG frames.
3. The ESP32 receives the frames over HTTP and renders them on the ST7789.
4. The ESP32 reads touchscreen coordinates from the XPT2046.
5. Touch coordinates are scaled to the PC's screen dimensions and sent to the server.
6. The server moves the mouse and handles mouse-button press and release events.

##  Software Requirements

### PC

- Python 3.10 or later recommended
- OpenCV
- MSS
- PyAutoGUI
- NumPy

### ESP32

Install the following libraries through the Arduino IDE Library Manager:

- **Arduino_GFX_Library**
- **TJpg_Decoder** by Bodmer
- **XPT2046_Touchscreen**

Also install the ESP32 board support package and select the appropriate **XIAO ESP32-S3** board.

##  Setup and Installation

### 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/esp32-s3-pc-remote.git
cd esp32-s3-pc-remote
```

Replace `YOUR_USERNAME/esp32-s3-pc-remote` with your GitHub repository path.

### 2. Install the PC dependencies

Open a terminal in the project directory and run:

```bash
python -m pip install opencv-python mss pyautogui numpy
```

If your system uses `python3` instead of `python`, use that command instead.

### 3. Configure the PC server

Open `pc_remote_control_server.py` and check the configuration values, including the HTTP port and touch-control token.

The supplied configuration uses:

```text
Server port: 8080
Touch endpoint: /touch
Video stream: /stream.mjpg
Control token: esp32-touch-7f3a
```

The server binds to `0.0.0.0` so other devices on the local network can connect.

### 4. Find your PC's local IP address

On Windows, open Command Prompt and run:

```bat
ipconfig
```

Find the IPv4 address of the network adapter connected to your Wi-Fi. For example:

```text
192.168.1.85
```

Your PC's IP address may be different. Use the actual address assigned to your computer.

### 5. Configure the ESP32

Open `esp32_screen_touch_control.ino` in Arduino IDE.

Update the Wi-Fi credentials and server address:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* SERVER_IP = "192.168.1.85";
const uint16_t SERVER_PORT = 8080;

const char* CONTROL_TOKEN = "esp32-touch-7f3a";
```

These are example values. Use your own Wi-Fi credentials and the PC's actual local IP address. The control token in the ESP32 sketch must match the token in the Python server.

### 6. Upload the ESP32 firmware

1. Connect the XIAO ESP32-S3 to your computer.
2. Select the XIAO ESP32-S3 board in Arduino IDE.
3. Select the correct COM port.
4. Confirm that the required libraries are installed.
5. Compile and upload the sketch.
6. Open Serial Monitor to check Wi-Fi connection and stream status.

### 7. Start the PC server

Run the following command from the project directory:

```bash
python pc_remote_control_server.py --screen
```

The server should start capturing the desktop and serving the MJPEG stream.

On the ESP32, the configured stream URL should be:

```text
http://192.168.1.85:8080/stream.mjpg
```

Replace the IP address with your PC's actual address.

### 8. Test the connection

On a device connected to the same Wi-Fi network, open:

```text
http://192.168.1.85:8080/
```

The browser preview should display the captured PC screen.

Once the ESP32 connects successfully, the ST7789 should display the live desktop preview. Touching the display should move the PC mouse to the corresponding position.

##  Touchscreen Coordinate Mapping

The display resolution is 320×240, while the PC screen may have a much higher resolution.

The server scales the received coordinates to the PC's screen dimensions:

```text
PC_X = Touch_X × (PC_Width  - 1) / 319
PC_Y = Touch_Y × (PC_Height - 1) / 239
```

The implementation clamps coordinates to valid screen bounds.

The ESP32 firmware also includes raw touch calibration limits and optional axis swapping and inversion. If touch positions are inaccurate or mirrored, adjust the calibration settings in the sketch.

##  Network Configuration

Both the PC and ESP32 must be connected to the same reachable network.

If the ESP32 cannot connect:

- Verify the PC's IPv4 address.
- Ensure the server is listening on `0.0.0.0`, not just `127.0.0.1`.
- Allow inbound TCP traffic on port `8080` in Windows Firewall.
- Ensure the Wi-Fi network does not isolate wireless clients.
- Confirm that the ESP32's server IP, port, and Wi-Fi credentials are correct.

Use a **Private** Windows network profile where appropriate and create a firewall rule limited to your trusted local network.

##  Troubleshooting

| Problem | What to check |
|---|---|
| Display remains blank | Check display wiring, power, reset pin, initialization, and display rotation |
| ESP32 cannot connect to Wi-Fi | Check SSID, password, and router compatibility |
| Stream does not load | Confirm the Python server is running and TCP port 8080 is reachable |
| Browser preview works, but ESP32 does not | Check the ESP32 server IP, stream parser, libraries, and Serial Monitor |
| Touchscreen does not respond | Check XPT2046 wiring, CS/IRQ pins, and SPI initialization |
| Touch is inverted or swapped | Adjust raw calibration limits, `TOUCH_SWAP_XY`, and inversion settings |
| Mouse does not move | Check the control token, `/touch` endpoint, and server logs |
| Mouse remains pressed | Verify that touch-release events reach the server |
| Display colors look incorrect | Check RGB/BGR handling and display color configuration |
| Preview looks stretched | The current stream is resized to 320×240, which may not match the PC's aspect ratio |

##  Security Notes

**This project is intended for trusted local networks.**

- The server exposes desktop imagery and mouse-control functionality to devices that can reach it.
- The control token is a basic shared secret, not a replacement for encrypted authentication.
- Do not expose port `8080` directly to the public internet or configure public port forwarding for this server.
- Keep the token private and change it in both the Python server and ESP32 firmware before sharing or deploying the project.
- Use the project only on computers and networks you are authorized to control.

##  Current Limitations

- The display shows a low-resolution preview rather than a full-resolution desktop.
- The 320×240 stream may distort the aspect ratio of a widescreen monitor.
- Streaming smoothness depends on Wi-Fi quality, JPEG quality, frame rate, and ESP32 decoding performance.
- Mouse control depends on the PC server running and accepting requests.
- The implementation is designed for a local network, not remote internet access.

##  Future Improvements

- Add click, drag, and right-click gestures.
- Add configurable frame rate and JPEG quality.
- Preserve the PC's aspect ratio with letterboxing.
- Improve touch responsiveness while video frames are being received.
- Add keyboard input and scroll gestures.
- Add a pairing interface and stronger authentication.
- Optimize JPEG decoding and network traffic.

Suggestions, bug reports, and pull requests are welcome. If you improve the streaming performance, touch calibration, or mouse interaction, consider contributing the changes back to the project.

/*
  XIAO ESP32-S3 + ST7789 320x240 + XPT2046 touch
  - Receives PC screen MJPEG at http://192.168.1.85:8080/stream.mjpg
  - Sends 320x240 touch coordinates to POST /touch
  - PC server scales the touch coordinates to the PC desktop resolution.

  Required Arduino libraries:
    - Arduino GFX Library
    - TJpg_Decoder by Bodmer
    - XPT2046_Touchscreen by Paul Stoffregen

  Set your Wi-Fi credentials below. The PC server must be running
  pc_remote_control_server.py --screen and listening on port 8080.
*/

#include <WiFi.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>
#include <XPT2046_Touchscreen.h>

// ---------- Wi-Fi ----------
const char *WIFI_SSID = "DIR-825-1291";
const char *WIFI_PASSWORD = "21600746";

// ---------- PC server ----------
IPAddress SERVER_IP(192, 168, 1, 85);
const uint16_t SERVER_PORT = 8080;
const char *STREAM_PATH = "/stream.mjpg";
const char *TOUCH_PATH = "/touch";
const char *CONTROL_TOKEN = "esp32-touch-7f3a";

// ---------- ST7789 pins ----------
#define TFT_SCK  7
#define TFT_MOSI 9
#define TFT_MISO -1
#define TFT_CS   4
#define TFT_DC   3
#define TFT_RST  2

// ---------- XPT2046 pins from your reference code ----------
#define TOUCH_IRQ  1
#define TOUCH_MISO 8
#define TOUCH_MOSI 9
#define TOUCH_CS   5
#define TOUCH_CLK  7

#define SCREEN_W 320
#define SCREEN_H 240

// Touch calibration copied from your reference code.
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3900
#define TOUCH_RAW_Y_MIN 200
#define TOUCH_RAW_Y_MAX 3900
#define TOUCH_SWAP_XY 0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, FSPI
);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, TFT_RST, 3, true, 240, 320
);

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
WiFiClient streamClient;
WiFiClient touchClient;

static const size_t JPEG_BUFFER_SIZE = 150 * 1024;
uint8_t *jpegBuffer = nullptr;

bool touchWasDown = false;
uint32_t lastTouchSend = 0;
int lastTouchX = 0;
int lastTouchY = 0;
bool lastSentDown = false;
const uint32_t TOUCH_SEND_INTERVAL_MS = 45;

bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return true;
}

bool readLine(WiFiClient &client, String &line, uint32_t timeoutMs = 5000) {
  line = "";
  uint32_t started = millis();

  while ((uint32_t)(millis() - started) < timeoutMs) {
    while (client.available() > 0) {
      char c = (char)client.read();
      if (c == '\n') {
        line.trim();
        return true;
      }
      if (line.length() < 240) line += c;
    }
    if (!client.connected() && client.available() == 0) return false;
    delay(1);
  }
  return false;
}

bool connectToStream() {
  streamClient.stop();
  Serial.printf("Connecting to stream at %s:%u...\n",
                SERVER_IP.toString().c_str(), SERVER_PORT);

  if (!streamClient.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("Stream TCP connection failed.");
    return false;
  }

  streamClient.setTimeout(5000);
  streamClient.printf(
    "GET %s HTTP/1.1\r\n"
    "Host: %s:%u\r\n"
    "User-Agent: XIAO-ESP32-S3\r\n"
    "Accept: multipart/x-mixed-replace\r\n"
    "Connection: keep-alive\r\n\r\n",
    STREAM_PATH, SERVER_IP.toString().c_str(), SERVER_PORT
  );

  String line;
  if (!readLine(streamClient, line)) {
    Serial.println("No HTTP response from stream server.");
    streamClient.stop();
    return false;
  }

  Serial.print("HTTP status: ");
  Serial.println(line);
  if (line.indexOf("200") < 0) {
    streamClient.stop();
    return false;
  }

  bool headersComplete = false;
  uint32_t started = millis();
  while ((uint32_t)(millis() - started) < 8000) {
    if (!readLine(streamClient, line)) {
      if (!streamClient.connected()) break;
      continue;
    }
    if (line.length() == 0) {
      headersComplete = true;
      break;
    }
  }

  if (!headersComplete) {
    Serial.println("Stream headers timed out.");
    streamClient.stop();
    return false;
  }

  Serial.println("MJPEG stream connected.");
  return true;
}

bool receiveAndDrawFrame() {
  String line;
  bool boundaryFound = false;
  int contentLength = -1;
  uint32_t started = millis();

  while ((uint32_t)(millis() - started) < 10000) {
    if (!readLine(streamClient, line)) {
      if (!streamClient.connected()) return false;
      continue;
    }
    if (line.startsWith("--frame")) {
      boundaryFound = true;
      break;
    }
  }
  if (!boundaryFound) return false;

  bool partHeadersComplete = false;
  started = millis();
  while ((uint32_t)(millis() - started) < 5000) {
    if (!readLine(streamClient, line)) {
      if (!streamClient.connected()) return false;
      continue;
    }
    if (line.length() == 0) {
      partHeadersComplete = true;
      break;
    }
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("content-length:")) {
      contentLength = line.substring(line.indexOf(':') + 1).toInt();
    }
  }

  if (!partHeadersComplete || contentLength <= 0 ||
      (size_t)contentLength > JPEG_BUFFER_SIZE) {
    Serial.printf("Invalid JPEG Content-Length: %d\n", contentLength);
    return false;
  }

  size_t received = 0;
  started = millis();
  while (received < (size_t)contentLength &&
         (uint32_t)(millis() - started) < 10000) {
    int availableBytes = streamClient.available();
    if (availableBytes > 0) {
      size_t remaining = (size_t)contentLength - received;
      size_t toRead = (size_t)availableBytes;
      if (toRead > remaining) toRead = remaining;
      int n = streamClient.read(jpegBuffer + received, toRead);
      if (n > 0) {
        received += (size_t)n;
        started = millis();
      }
    } else {
      if (!streamClient.connected()) return false;
      delay(1);
    }
  }

  if (received != (size_t)contentLength) return false;

  TJpgDec.drawJpg(0, 0, jpegBuffer, received);
  return true;
}

void showMessage(const char *line1, const char *line2 = "") {
  gfx->fillScreen(0x0000);
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(1);
  gfx->setCursor(8, 8);
  gfx->println(line1);
  if (line2[0]) {
    gfx->setCursor(8, 28);
    gfx->println(line2);
  }
}

bool sendTouchEvent(int x, int y, bool down) {
  if (WiFi.status() != WL_CONNECTED) return false;

  touchClient.stop();
  if (!touchClient.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("Touch POST connection failed.");
    return false;
  }
  touchClient.setTimeout(1200);

  char body[96];
  int bodyLength = snprintf(body, sizeof(body),
                            "{\"x\":%d,\"y\":%d,\"down\":%s}",
                            x, y, down ? "true" : "false");

  touchClient.printf(
    "POST %s HTTP/1.1\r\n"
    "Host: %s:%u\r\n"
    "Content-Type: application/json\r\n"
    "X-Remote-Control-Token: %s\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n\r\n",
    TOUCH_PATH, SERVER_IP.toString().c_str(), SERVER_PORT,
    CONTROL_TOKEN, bodyLength
  );
  touchClient.write((const uint8_t *)body, bodyLength);

  String response;
  bool gotResponse = readLine(touchClient, response, 1200);
  touchClient.stop();

  if (gotResponse && response.indexOf("200") >= 0) return true;
  if (gotResponse) {
    Serial.print("Touch server response: ");
    Serial.println(response);
  }
  return false;
}

bool getTouchPoint(int &x, int &y, bool &down) {
  down = touch.touched();
  if (!down) return false;

  TS_Point p = touch.getPoint();

  int32_t tx = map(p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_W - 1);
  int32_t ty = map(p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_H - 1);

  tx = constrain(tx, 0, SCREEN_W - 1);
  ty = constrain(ty, 0, SCREEN_H - 1);

#if TOUCH_SWAP_XY
  int32_t temp = tx; tx = ty; ty = temp;
#endif
#if TOUCH_INVERT_X
  tx = SCREEN_W - 1 - tx;
#endif
#if TOUCH_INVERT_Y
  ty = SCREEN_H - 1 - ty;
#endif

  x = (int)tx;
  y = (int)ty;
  return true;
}

void serviceTouch() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastTouchSend) < TOUCH_SEND_INTERVAL_MS) return;

  int x = lastTouchX;
  int y = lastTouchY;
  bool down = false;
  bool hasPoint = getTouchPoint(x, y, down);

  // Send updates while held, plus one release event after lifting the finger.
  if (hasPoint) {
    lastTouchX = x;
    lastTouchY = y;
    touchWasDown = true;
    if (sendTouchEvent(x, y, true)) {
      lastSentDown = true;
    }
    lastTouchSend = millis();
  } else if (touchWasDown) {
    touchWasDown = false;
    if (lastSentDown) {
      sendTouchEvent(lastTouchX, lastTouchY, false);
      lastSentDown = false;
    }
    lastTouchSend = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  gfx->begin();
  gfx->invertDisplay(true);
  showMessage("Starting remote screen...");

  jpegBuffer = (uint8_t *)malloc(JPEG_BUFFER_SIZE);
  if (!jpegBuffer) {
    Serial.println("Could not allocate JPEG buffer.");
    showMessage("JPEG buffer allocation", "failed");
    while (true) delay(1000);
  }

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftOutput);

  // Touch uses the same SCK/MOSI wires as the display, plus its own MISO/CS/IRQ.
  SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(SPI);
  touch.setRotation(3);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showMessage("Connecting to Wi-Fi...", WIFI_SSID);
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (uint32_t)(millis() - started) < 25000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showMessage("Wi-Fi failed", "Check credentials / 2.4GHz");
    Serial.println("Wi-Fi connection failed.");
    return;
  }

  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("PC stream: http://%s:%u%s\n",
                SERVER_IP.toString().c_str(), SERVER_PORT, STREAM_PATH);
  showMessage("Wi-Fi connected", "Opening PC screen...");
}

void loop() {
  // Check touch before and after each frame. Input is sent to /touch on the PC.
  serviceTouch();

  if (WiFi.status() != WL_CONNECTED) {
    streamClient.stop();
    touchClient.stop();
    WiFi.reconnect();
    delay(300);
    return;
  }

  if (!streamClient.connected()) {
    if (!connectToStream()) {
      serviceTouch();
      delay(300);
      return;
    }
  }

  if (!receiveAndDrawFrame()) {
    Serial.println("Stream interrupted; reconnecting...");
    streamClient.stop();
    delay(150);
  }

  serviceTouch();
}

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
// WIFI
// ============================================================
const char* WIFI_NAME = "TargetedJustice";
const char* WIFI_PASSWORD = "pushyourenergy";

// ============================================================
// GOOUUU ESP32-S3-CAM V1.5 N16R8
// OV3660 CAMERA
// ============================================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

WebServer server(80);

// ============================================================
// CAMERA
// ============================================================

void setupCamera() {

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {

    Serial.println("PSRAM FOUND");

    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;

  } else {

    Serial.println("WARNING: PSRAM NOT FOUND");

    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {

    Serial.printf(
      "CAMERA INIT FAILED: 0x%x\n",
      err
    );

    while (true) {
      delay(1000);
    }
  }

  Serial.println("CAMERA INITIALIZED!");

  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor) {

    // OV3660 image adjustments
    sensor->set_vflip(sensor, 0);
    sensor->set_hmirror(sensor, 0);
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
  }
}

// ============================================================
// HTML PAGE
// ============================================================

void handleRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>

<html>
<head>
<meta name="viewport"
      content="width=device-width, initial-scale=1">
<title>GOOUUU ESP32-S3-CAM</title>
<style>

body {
  background: #111;
  color: white;
  font-family: Arial;
  text-align: center;
  margin: 0;
  padding: 20px;
}

h1 {
  font-size: 22px;
}

img {
  width: 100%;
  max-width: 800px;
  height: auto;
  border-radius: 8px;
}

a {
  color: #4da6ff;
}

</style>

</head>

<body>

<h1>GOOUUU ESP32-S3-CAM</h1>

<img src="/stream">

<p>
<a href="/capture" target="_blank">
Take Snapshot
</a>
</p>

<p>
Stream URL:
<br>
<code>/stream</code>
</p>

</body>
</html>

)rawliteral";

  server.send(200, "text/html", html);
}

// ============================================================
// SINGLE JPEG
// ============================================================
void handleCapture() {

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {

    server.send(
      500,
      "text/plain",
      "Camera capture failed"
    );
    return;
  }

  WiFiClient client = server.client();

  server.sendHeader(
    "Content-Type",
    "image/jpeg"
  );

  server.send_P(
    200,
    "image/jpeg",
    (const char*)fb->buf,
    fb->len
  );
  esp_camera_fb_return(fb);
}

// ============================================================
// MJPEG STREAM
// ============================================================

void handleStream() {

  WiFiClient client = server.client();

  client.println(
    "HTTP/1.1 200 OK"
  );
  client.println(
    "Content-Type: multipart/x-mixed-replace; boundary=frame"
  );
  client.println(
    "Cache-Control: no-cache"
  );
  client.println(
    "Access-Control-Allow-Origin: *"
  );

  client.println();
  while (client.connected()) {

    camera_fb_t *fb =
      esp_camera_fb_get();

    if (!fb) {
      Serial.println(
        "Camera capture failed"
      );
      break;
    }
    client.println(
      "--frame"
    );

    client.println(
      "Content-Type: image/jpeg"
    );
    client.print(
      "Content-Length: "
    );
    client.println(
      fb->len
    );
    client.println();

    client.write(
      fb->buf,
      fb->len
    );
    client.println();
    esp_camera_fb_return(fb);

    // Approximately 10 FPS
    delay(100);
  }
  client.stop();

  Serial.println(
    "Stream client disconnected"
  );
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "GOOUUU ESP32-S3-CAM V1.5"
  );

  Serial.println(
    "OV3660 LIVE STREAM"
  );

  Serial.println(
    "================================"
  );

  setupCamera();

  Serial.println();
  Serial.print(
    "Connecting to Wi-Fi"
  );

  WiFi.begin(
    WIFI_NAME,
    WIFI_PASSWORD
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println(
    "WIFI CONNECTED!"
  );

  Serial.print(
    "IP ADDRESS: http://"
  );
  Serial.println(
    WiFi.localIP()
  );
  
  Serial.println();
  
  Serial.print(
    "LIVE STREAM: http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/stream"
  );

  Serial.println();
  
  server.on(
    "/",
    handleRoot
  );
  server.on(
    "/capture",
    handleCapture
  );
  server.on(
    "/stream",
    handleStream
  );

  server.begin();

  Serial.println(
    "WEB SERVER STARTED!"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();
}

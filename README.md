# esp32s3-cam-quicklet
Reference, code, and Arduino ".ino" files for ESP32-S3-CAM N16R8 V1.5 GOOUUU. Configuration guide for this exact model esp32 device. At time of purchase from amazon they were $9

# Board Setup Configuration Arduino

### Arduino IDE > File > Preferences > Additional Boards Manager URLs > Add:
https://espressif.github.io/arduino-esp32/package_esp32_index.json

#### Flash on the "right USBC port" & Power with pins or "left USBC port"

### Arduino IDE → Tools

| Setting                                  | Value                                          |
| ---------------------------------------- | ---------------------------------------------- |
| **Board**                                | `ESP32S3 Dev Module`                           |
| **USB CDC On Boot**                      | `Enabled`                                      |
| **CPU Frequency**                        | `240MHz (WiFi)`                                |
| **Core Debug Level**                     | `None`                                         |
| **USB DFU On Boot**                      | `Disabled`                                     |
| **Erase All Flash Before Sketch Upload** | `Disabled` normally                            |
| **Flash Mode**                           | `QIO`                                          |
| **Flash Size**                           | `8MB`*                                         |
| **Partition Scheme**                     | `Huge APP (3MB No OTA/1MB SPIFFS)`             |
| **PSRAM**                                | `OPI PSRAM`*                                   |
| **Upload Mode**                          | `UART0 / Hardware CDC` depending on your board |
| **Upload Speed**                         | `921600`                                       |
| **Port**                                 | Your ESP32-S3 COM port                         |
|                                          |                                                |

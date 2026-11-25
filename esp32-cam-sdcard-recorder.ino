/*

  ============================================================

  GOOUUU ESP32-S3-CAM V1.5 N16R8

  MJPEG AVI SD-CARD RECORDER

  ============================================================

  

  Camera:

    OV2640 / compatible JPEG camera

  

  Recording:

    640x480 VGA

    JPEG quality 12

    15 FPS target

    MJPEG AVI

    5-minute files

  

  Board:

    ESP32-S3-WROOM-1 N16R8

    16 MB Flash

    8 MB PSRAM

  

  SD:

    SDMMC 1-bit

  

    CLK  = GPIO39

    CMD  = GPIO38

    D0   = GPIO40

  

  IMPORTANT:

    First test with SEGMENT_SECONDS = 30.

    Once a 30-second AVI is confirmed playable,

    change it back to 300.

*/

  

#include <Arduino.h>

#include "esp_camera.h"

#include "FS.h"

#include "SD_MMC.h"

#include "esp_heap_caps.h"

  

// ============================================================

// CAMERA PINS

// GOOUUU ESP32-S3-CAM V1.5

// ============================================================

  

#define PWDN_GPIO_NUM     -1

#define RESET_GPIO_NUM    -1

  

#define XCLK_GPIO_NUM      15

#define SIOD_GPIO_NUM       4

#define SIOC_GPIO_NUM       5

  

#define Y9_GPIO_NUM        16

#define Y8_GPIO_NUM        17

#define Y7_GPIO_NUM        18

#define Y6_GPIO_NUM        12

#define Y5_GPIO_NUM        10

#define Y4_GPIO_NUM         8

#define Y3_GPIO_NUM         9

#define Y2_GPIO_NUM        11

  

#define VSYNC_GPIO_NUM      6

#define HREF_GPIO_NUM       7

#define PCLK_GPIO_NUM      13

  

// ============================================================

// SD CARD

// ============================================================

  

#define SD_CLK   39

#define SD_CMD   38

#define SD_D0    40

  

// ============================================================

// RECORDING SETTINGS

// ============================================================

  

#define FRAME_WIDTH   640

#define FRAME_HEIGHT  480

  

#define FPS            15

  

// Lower number = higher JPEG quality / larger files

#define JPEG_QUALITY   12

  

// ------------------------------------------------------------

// TEST WITH 30 FIRST.

// Change to 300 after the test works.

// ------------------------------------------------------------

  

#define SEGMENT_SECONDS 300

  

// Maximum expected frames in one segment.

// A few extra frames are allowed.

#define MAX_FRAMES_PER_SEGMENT \

    ((FPS * SEGMENT_SECONDS) + 20)

  

// Flush SD every N frames.

// 15 FPS / 75 = approximately every 5 seconds.

#define FLUSH_EVERY_FRAMES 75

  

// ============================================================

// FILE

// ============================================================

  

File aviFile;

  

char filename[64];

  

uint32_t frameCount = 0;

uint32_t droppedFrames = 0;

  

uint32_t recordingStart = 0;

uint32_t lastFrameTime = 0;

  

// We track this ourselves.

// Do NOT rely on File.size() while the file is being written.

uint32_t aviFilePosition = 0;

  

uint32_t mjpegBytes = 0;

  

// Position of the beginning of the "movi" DATA.

// The first 00dc chunk begins here.

uint32_t moviDataStart = 0;

  

// ============================================================

// AVI INDEX ENTRY

// ============================================================

//

// Each idx1 entry is 16 bytes:

//

// 4 bytes  chunk ID

// 4 bytes  flags

// 4 bytes  offset

// 4 bytes  size

//

// 4500 frames = about 72 KB.

// This is stored in PSRAM.

// ============================================================

  

struct AVIIndexEntry

{

    uint32_t offset;

    uint32_t size;

};

  

AVIIndexEntry *aviIndex = nullptr;

  

// ============================================================

// WRITE HELPERS

// ============================================================

  

bool writeBytes(

    const uint8_t *data,

    size_t length

)

{

    if (!aviFile)

        return false;

  

    size_t written =

        aviFile.write(data, length);

  

    if (written != length)

    {

        Serial.print("WRITE ERROR: wrote ");

        Serial.print(written);

        Serial.print(" of ");

        Serial.println(length);

  

        return false;

    }

  

    aviFilePosition += written;

  

    return true;

}

  

// ------------------------------------------------------------

// Little endian 16-bit

// ------------------------------------------------------------

  

bool writeLE16(

    uint16_t value

)

{

    uint8_t b[2];

  

    b[0] = value & 0xFF;

    b[1] = (value >> 8) & 0xFF;

  

    return writeBytes(b, 2);

}

  

// ------------------------------------------------------------

// Little endian 32-bit

// ------------------------------------------------------------

  

bool writeLE32(

    uint32_t value

)

{

    uint8_t b[4];

  

    b[0] = value & 0xFF;

    b[1] = (value >> 8) & 0xFF;

    b[2] = (value >> 16) & 0xFF;

    b[3] = (value >> 24) & 0xFF;

  

    return writeBytes(b, 4);

}

  

// ------------------------------------------------------------

// FOURCC

// ------------------------------------------------------------

  

bool writeFourCC(

    const char *s

)

{

    return writeBytes(

        (const uint8_t *)s,

        4

    );

}

  

// ============================================================

// PATCH EXISTING 32-BIT VALUE

// ============================================================

//

// Seeking changes the file position, so we restore it.

// ============================================================

  

bool patchLE32(

    uint32_t position,

    uint32_t value

)

{

    if (!aviFile)

        return false;

  

    uint32_t oldPosition =

        aviFilePosition;

  

    if (!aviFile.seek(position))

    {

        Serial.print("SEEK ERROR at ");

        Serial.println(position);

  

        return false;

    }

  

    uint8_t b[4];

  

    b[0] = value & 0xFF;

    b[1] = (value >> 8) & 0xFF;

    b[2] = (value >> 16) & 0xFF;

    b[3] = (value >> 24) & 0xFF;

  

    size_t written =

        aviFile.write(b, 4);

  

    if (written != 4)

    {

        Serial.println(

            "PATCH WRITE ERROR"

        );

  

        return false;

    }

  

    if (!aviFile.seek(oldPosition))

    {

        Serial.println(

            "SEEK RESTORE ERROR"

        );

  

        return false;

    }

  

    return true;

}

  

// ============================================================

// ALLOCATE AVI INDEX

// ============================================================

  

bool allocateAVIIndex()

{

    size_t bytes =

        sizeof(AVIIndexEntry) *

        MAX_FRAMES_PER_SEGMENT;

  

    Serial.print(

        "Allocating AVI index: "

    );

  

    Serial.print(bytes / 1024);

  

    Serial.println(

        " KB in PSRAM..."

    );

  

    aviIndex =

        (AVIIndexEntry *)heap_caps_malloc(

            bytes,

            MALLOC_CAP_SPIRAM

        );

  

    if (!aviIndex)

    {

        Serial.println(

            "ERROR: Could not allocate AVI index."

        );

  

        return false;

    }

  

    memset(

        aviIndex,

        0,

        bytes

    );

  

    Serial.println(

        "AVI index allocated."

    );

  

    return true;

}

  

// ============================================================

// WRITE AVI HEADER

// ============================================================

//

// Standard AVI structure:

//

// RIFF

//   LIST hdrl

//     avih

//     LIST strl

//       strh

//       strf

//   LIST movi

//     00dc JPEG

//     00dc JPEG

//     ...

//   idx1

//

// ============================================================

  

bool writeAVIHeader()

{

    // --------------------------------------------------------

    // RIFF

    // --------------------------------------------------------

  

    if (!writeFourCC("RIFF"))

        return false;

  

    // RIFF size -- patched at finalize

    if (!writeLE32(0))

        return false;

  

    if (!writeFourCC("AVI "))

        return false;

  

    // --------------------------------------------------------

    // LIST hdrl

    // --------------------------------------------------------

  

    if (!writeFourCC("LIST"))

        return false;

  

    // hdrl list size = 192

    if (!writeLE32(192))

        return false;

  

    if (!writeFourCC("hdrl"))

        return false;

  

    // --------------------------------------------------------

    // avih

    // --------------------------------------------------------

  

    if (!writeFourCC("avih"))

        return false;

  

    if (!writeLE32(56))

        return false;

  

    // Microseconds per frame

    if (!writeLE32(1000000 / FPS))

        return false;

  

    // Max bytes/sec

    if (!writeLE32(0))

        return false;

  

    // Padding

    if (!writeLE32(0))

        return false;

  

    // Flags = AVIF_HASINDEX

    if (!writeLE32(0x10))

        return false;

  

    // Total frames

    // PATCH OFFSET = 48

    if (!writeLE32(0))

        return false;

  

    // Initial frames

    if (!writeLE32(0))

        return false;

  

    // Number of streams

    if (!writeLE32(1))

        return false;

  

    // Suggested buffer size

    if (!writeLE32(0))

        return false;

  

    // Width

    if (!writeLE32(FRAME_WIDTH))

        return false;

  

    // Height

    if (!writeLE32(FRAME_HEIGHT))

        return false;

  

    // Reserved

    if (!writeLE32(0))

        return false;

  

    if (!writeLE32(0))

        return false;

  

    if (!writeLE32(0))

        return false;

  

    if (!writeLE32(0))

        return false;

  

    // --------------------------------------------------------

    // LIST strl

    // --------------------------------------------------------

  

    if (!writeFourCC("LIST"))

        return false;

  

    // strl size

    if (!writeLE32(116))

        return false;

  

    if (!writeFourCC("strl"))

        return false;

  

    // --------------------------------------------------------

    // strh

    // --------------------------------------------------------

  

    if (!writeFourCC("strh"))

        return false;

  

    if (!writeLE32(56))

        return false;

  

    if (!writeFourCC("vids"))

        return false;

  

    if (!writeFourCC("MJPG"))

        return false;

  

    // Flags

    if (!writeLE32(0))

        return false;

  

    // Priority / language

    if (!writeLE16(0))

        return false;

  

    if (!writeLE16(0))

        return false;

  

    // Initial frames

    if (!writeLE32(0))

        return false;

  

    // Scale

    if (!writeLE32(1))

        return false;

  

    // Rate

    if (!writeLE32(FPS))

        return false;

  

    // Start

    if (!writeLE32(0))

        return false;

  

    // Length / frame count

    // PATCH OFFSET = 140

    if (!writeLE32(0))

        return false;

  

    // Suggested buffer size

    if (!writeLE32(0))

        return false;

  

    // Quality

    if (!writeLE32(0xFFFFFFFF))

        return false;

  

    // Sample size

    if (!writeLE32(0))

        return false;

  

    // rcFrame

    if (!writeLE16(0))

        return false;

  

    if (!writeLE16(0))

        return false;

  

    if (!writeLE16(FRAME_WIDTH))

        return false;

  

    if (!writeLE16(FRAME_HEIGHT))

        return false;

  

    // --------------------------------------------------------

    // strf

    // --------------------------------------------------------

  

    if (!writeFourCC("strf"))

        return false;

  

    if (!writeLE32(40))

        return false;

  

    // BITMAPINFOHEADER

    if (!writeLE32(40))

        return false;

  

    if (!writeLE32(FRAME_WIDTH))

        return false;

  

    if (!writeLE32(FRAME_HEIGHT))

        return false;

  

    // Planes

    if (!writeLE16(1))

        return false;

  

    // Bits

    if (!writeLE16(24))

        return false;

  

    // Compression

    if (!writeFourCC("MJPG"))

        return false;

  

    // Image size

    if (!writeLE32(0))

        return false;

  

    // X pixels/meter

    if (!writeLE32(0))

        return false;

  

    // Y pixels/meter

    if (!writeLE32(0))

        return false;

  

    // Colors used

    if (!writeLE32(0))

        return false;

  

    // Important colors

    if (!writeLE32(0))

        return false;

  

    // --------------------------------------------------------

    // LIST movi

    // --------------------------------------------------------

  

    if (!writeFourCC("LIST"))

        return false;

  

    // movi size patched later

    // PATCH OFFSET = 216

    if (!writeLE32(0))

        return false;

  

    if (!writeFourCC("movi"))

        return false;

  

    // First JPEG begins here.

    moviDataStart =

        aviFilePosition;

  

    mjpegBytes = 0;

  

    return true;

}

  

// ============================================================

// WRITE FRAME

// ============================================================

  

bool writeFrame(

    camera_fb_t *fb

)

{

    if (!fb)

        return false;

  

    if (fb->format != PIXFORMAT_JPEG)

        return false;

  

    if (frameCount >= MAX_FRAMES_PER_SEGMENT)

    {

        Serial.println(

            "ERROR: AVI index is full."

        );

  

        return false;

    }

  

    // --------------------------------------------------------

    // Offset relative to start of movi data

    // --------------------------------------------------------

  

    uint32_t frameOffset =

        aviFilePosition -

        moviDataStart;

  

    // --------------------------------------------------------

    // 00dc

    // --------------------------------------------------------

  

    if (!writeFourCC("00dc"))

        return false;

  

    // JPEG size

    if (!writeLE32(fb->len))

        return false;

  

    // JPEG data

    if (!writeBytes(

            fb->buf,

            fb->len))

    {

        return false;

    }

  

    // --------------------------------------------------------

    // WORD alignment

    // --------------------------------------------------------

  

    if (fb->len & 1)

    {

        uint8_t pad = 0;

  

        if (!writeBytes(&pad, 1))

            return false;

    }

  

    // --------------------------------------------------------

    // Save index entry

    // --------------------------------------------------------

  

    aviIndex[frameCount].offset =

        frameOffset;

  

    aviIndex[frameCount].size =

        fb->len;

  

    mjpegBytes += fb->len;

  

    frameCount++;

  

    // --------------------------------------------------------

    // Periodic flush

    // --------------------------------------------------------

  

    if (

        (frameCount %

         FLUSH_EVERY_FRAMES) == 0

    )

    {

        aviFile.flush();

    }

  

    return true;

}

  

// ============================================================

// WRITE AVI INDEX

// ============================================================

  

bool writeAVIIndex()

{

    Serial.println(

        "Writing AVI index..."

    );

  

    if (!writeFourCC("idx1"))

        return false;

  

    uint32_t indexSize =

        frameCount * 16;

  

    if (!writeLE32(indexSize))

        return false;

  

    for (

        uint32_t i = 0;

        i < frameCount;

        i++

    )

    {

        // Chunk ID

        if (!writeFourCC("00dc"))

            return false;

  

        // Flags:

        // AVIIF_KEYFRAME

        if (!writeLE32(0x10))

            return false;

  

        // Offset relative to movi data

        if (!writeLE32(

                aviIndex[i].offset))

        {

            return false;

        }

  

        // JPEG size

        if (!writeLE32(

                aviIndex[i].size))

        {

            return false;

        }

    }

  

    return true;

}

  

// ============================================================

// FINALIZE AVI

// ============================================================

  

bool finalizeAVI()

{

    if (!aviFile)

        return false;

  

    Serial.println();

    Serial.println(

        "Finalizing AVI..."

    );

  

    // --------------------------------------------------------

    // movi LIST size

    //

    // Size includes:

    //   "movi" 4 bytes

    //   frame data

    // --------------------------------------------------------

  

    uint32_t moviSize =

        aviFilePosition -

        (moviDataStart - 4);

  

    // --------------------------------------------------------

    // Write idx1 at current end

    // --------------------------------------------------------

  

    if (!writeAVIIndex())

    {

        Serial.println(

            "ERROR: AVI index failed."

        );

  

        aviFile.close();

  

        return false;

    }

  

    // --------------------------------------------------------

    // File size BEFORE closing

    // --------------------------------------------------------

  

    uint32_t finalFileSize =

        aviFilePosition;

  

    // --------------------------------------------------------

    // RIFF size

    // --------------------------------------------------------

  

    if (!patchLE32(

            4,

            finalFileSize - 8))

    {

        Serial.println(

            "ERROR: RIFF patch failed."

        );

    }

  

    // --------------------------------------------------------

    // Total frame count in avih

    // --------------------------------------------------------

  

    if (!patchLE32(

            48,

            frameCount))

    {

        Serial.println(

            "ERROR: avih frame patch failed."

        );

    }

  

    // --------------------------------------------------------

    // Frame count in strh

    // --------------------------------------------------------

  

    if (!patchLE32(

            140,

            frameCount))

    {

        Serial.println(

            "ERROR: strh frame patch failed."

        );

    }

  

    // --------------------------------------------------------

    // movi LIST size

    // --------------------------------------------------------

  

    if (!patchLE32(

            216,

            moviSize))

    {

        Serial.println(

            "ERROR: movi size patch failed."

        );

    }

  

    // --------------------------------------------------------

    // Flush everything

    // --------------------------------------------------------

  

    aviFile.flush();

  

    // --------------------------------------------------------

    // Close

    // --------------------------------------------------------

  

    aviFile.close();

  

    // --------------------------------------------------------

    // Report

    // --------------------------------------------------------

  

    uint32_t elapsed =

        millis() -

        recordingStart;

  

    float actualFPS = 0;

  

    if (elapsed > 0)

    {

        actualFPS =

            (frameCount * 1000.0f) /

            elapsed;

    }

  

    Serial.println();

    Serial.println(

        "================================"

    );

  

    Serial.println(

        " AVI FINALIZED"

    );

  

    Serial.println(

        "================================"

    );

  

    Serial.print(

        "File: "

    );

  

    Serial.println(filename);

  

    Serial.print(

        "Frames: "

    );

  

    Serial.println(frameCount);

  

    Serial.print(

        "FPS: "

    );

  

    Serial.println(

        actualFPS,

        2

    );

  

    Serial.print(

        "JPEG data: "

    );

  

    Serial.print(

        mjpegBytes / 1024

    );

  

    Serial.println(

        " KB"

    );

  

    Serial.print(

        "AVI size: "

    );

  

    Serial.print(

        finalFileSize / 1024

    );

  

    Serial.println(

        " KB"

    );

  

    Serial.println();

  

    // --------------------------------------------------------

    // Re-open file to verify it actually exists

    // --------------------------------------------------------

  

    File verify =

        SD_MMC.open(

            filename,

            FILE_READ

        );

  

    if (verify)

    {

        uint32_t verifiedSize =

            verify.size();

  

        Serial.print(

            "Verified SD file size: "

        );

  

        Serial.print(

            verifiedSize / 1024

        );

  

        Serial.println(

            " KB"

        );

  

        verify.close();

    }

    else

    {

        Serial.println(

            "WARNING: Could not reopen AVI."

        );

    }

  

    Serial.println();

  

    return true;

}

  

// ============================================================

// START NEW AVI

// ============================================================

  

bool startAVI()

{

    frameCount = 0;

    droppedFrames = 0;

  

    mjpegBytes = 0;

  

    aviFilePosition = 0;

    moviDataStart = 0;

  

    recordingStart =

        millis();

  

    snprintf(

        filename,

        sizeof(filename),

        "/VIDEO_%lu.avi",

        (unsigned long)millis()

    );

  

    Serial.println();

    Serial.print(

        "Opening: "

    );

  

    Serial.println(filename);

  

    aviFile =

        SD_MMC.open(

            filename,

            FILE_WRITE

        );

  

    if (!aviFile)

    {

        Serial.println(

            "ERROR: Cannot create AVI."

        );

  

        return false;

    }

  

    if (!writeAVIHeader())

    {

        Serial.println(

            "ERROR: Cannot write AVI header."

        );

  

        aviFile.close();

  

        return false;

    }

  

    aviFile.flush();

  

    Serial.println(

        "Recording started."

    );

  

    return true;

}

  

// ============================================================

// CAMERA INITIALIZATION

// ============================================================

  

bool initCamera()

{

    camera_config_t config = {};

  

    config.ledc_channel =

        LEDC_CHANNEL_0;

  

    config.ledc_timer =

        LEDC_TIMER_0;

  

    config.pin_d0 =

        Y2_GPIO_NUM;

  

    config.pin_d1 =

        Y3_GPIO_NUM;

  

    config.pin_d2 =

        Y4_GPIO_NUM;

  

    config.pin_d3 =

        Y5_GPIO_NUM;

  

    config.pin_d4 =

        Y6_GPIO_NUM;

  

    config.pin_d5 =

        Y7_GPIO_NUM;

  

    config.pin_d6 =

        Y8_GPIO_NUM;

  

    config.pin_d7 =

        Y9_GPIO_NUM;

  

    config.pin_xclk =

        XCLK_GPIO_NUM;

  

    config.pin_pclk =

        PCLK_GPIO_NUM;

  

    config.pin_vsync =

        VSYNC_GPIO_NUM;

  

    config.pin_href =

        HREF_GPIO_NUM;

  

    config.pin_sccb_sda =

        SIOD_GPIO_NUM;

  

    config.pin_sccb_scl =

        SIOC_GPIO_NUM;

  

    config.pin_pwdn =

        PWDN_GPIO_NUM;

  

    config.pin_reset =

        RESET_GPIO_NUM;

  

    config.xclk_freq_hz =

        20000000;

  

    config.pixel_format =

        PIXFORMAT_JPEG;

  

    // --------------------------------------------------------

    // PSRAM

    // --------------------------------------------------------

  

    if (psramFound())

    {

        Serial.println(

            "PSRAM detected."

        );

  

        config.frame_size =

            FRAMESIZE_VGA;

  

        config.jpeg_quality =

            JPEG_QUALITY;

  

        config.fb_count = 2;

  

        config.fb_location =

            CAMERA_FB_IN_PSRAM;

    }

    else

    {

        Serial.println(

            "WARNING: PSRAM not detected."

        );

  

        config.frame_size =

            FRAMESIZE_VGA;

  

        config.jpeg_quality = 15;

  

        config.fb_count = 1;

  

        // We know this board should have PSRAM.

        // If it doesn't, fail instead of pretending.

        config.fb_location =

            CAMERA_FB_IN_DRAM;

    }

  

    config.grab_mode =

        CAMERA_GRAB_WHEN_EMPTY;

  

    esp_err_t err =

        esp_camera_init(

            &config

        );

  

    Serial.print(

        "PSRAM: "

    );

  

    Serial.println(

        psramFound()

            ? "YES"

            : "NO"

    );

  

    Serial.print(

        "PSRAM size: "

    );

  

    Serial.println(

        ESP.getPsramSize()

    );

  

    if (err != ESP_OK)

    {

        Serial.print(

            "Camera init failed: 0x"

        );

  

        Serial.println(

            err,

            HEX

        );

  

        return false;

    }

  

    sensor_t *sensor =

        esp_camera_sensor_get();

  

    if (!sensor)

    {

        Serial.println(

            "ERROR: No camera sensor."

        );

  

        return false;

    }

  

    sensor->set_framesize(

        sensor,

        FRAMESIZE_VGA

    );

  

    sensor->set_quality(

        sensor,

        JPEG_QUALITY

    );

  

    Serial.println(

        "Camera initialized."

    );

  

    return true;

}

  

// ============================================================

// SD INITIALIZATION

// ============================================================

  

bool initSD()

{

    Serial.println();

    Serial.println(

        "Initializing SD card..."

    );

  

    // --------------------------------------------------------

    // GOOUUU ESP32-S3-CAM V1.5

    //

    // CLK = GPIO39

    // CMD = GPIO38

    // D0  = GPIO40

    //

    // 1-bit SDMMC

    // --------------------------------------------------------

  

    if (!SD_MMC.setPins(

            SD_CLK,

            SD_CMD,

            SD_D0))

    {

        Serial.println(

            "SD_MMC.setPins() failed."

        );

  

        return false;

    }

  

    if (!SD_MMC.begin(

            "/sdcard",

            true,       // 1-bit mode

            false))     // don't auto-format

    {

        Serial.println(

            "SD card mount failed."

        );

  

        return false;

    }

  

    uint8_t cardType =

        SD_MMC.cardType();

  

    if (cardType == CARD_NONE)

    {

        Serial.println(

            "No SD card detected."

        );

  

        return false;

    }

  

    uint64_t cardSize =

        SD_MMC.cardSize();

  

    uint64_t usedBytes =

        SD_MMC.usedBytes();

  

    Serial.print(

        "SD size: "

    );

  

    Serial.print(

        cardSize /

        (1024ULL * 1024ULL)

    );

  

    Serial.println(

        " MB"

    );

  

    Serial.print(

        "Used: "

    );

  

    Serial.print(

        usedBytes /

        (1024ULL * 1024ULL)

    );

  

    Serial.println(

        " MB"

    );

  

    Serial.print(

        "Free: "

    );

  

    if (cardSize > usedBytes)

    {

        Serial.print(

            (cardSize - usedBytes) /

            (1024ULL * 1024ULL)

        );

    }

    else

    {

        Serial.print(0);

    }

  

    Serial.println(

        " MB"

    );

  

    Serial.println(

        "SD card ready."

    );

  

    return true;

}

  

// ============================================================

// SETUP

// ============================================================

  

void setup()

{

    Serial.begin(115200);

  

    delay(1500);

  

    Serial.println();

    Serial.println(

        "================================"

    );

  

    Serial.println(

        " GOOUUU ESP32-S3-CAM RECORDER"

    );

  

    Serial.println(

        "================================"

    );

  

    Serial.print(

        "Free heap: "

    );

  

    Serial.println(

        ESP.getFreeHeap()

    );

  

    Serial.print(

        "Free PSRAM: "

    );

  

    Serial.println(

        ESP.getFreePsram()

    );

  

    // --------------------------------------------------------

    // Allocate AVI index

    // --------------------------------------------------------

  

    if (!allocateAVIIndex())

    {

        Serial.println(

            "FATAL: AVI index allocation failed."

        );

  

        while (true)

        {

            delay(1000);

        }

    }

  

    // --------------------------------------------------------

    // Camera

    // --------------------------------------------------------

  

    if (!initCamera())

    {

        Serial.println(

            "FATAL: Camera failed."

        );

  

        while (true)

        {

            delay(1000);

        }

    }

  

    // --------------------------------------------------------

    // SD

    // --------------------------------------------------------

  

    if (!initSD())

    {

        Serial.println(

            "FATAL: SD failed."

        );

  

        while (true)

        {

            delay(1000);

        }

    }

  

    // --------------------------------------------------------

    // Start recording

    // --------------------------------------------------------

  

    if (!startAVI())

    {

        Serial.println(

            "FATAL: AVI failed."

        );

  

        while (true)

        {

            delay(1000);

        }

    }

  

    lastFrameTime =

        millis();

}

  

// ============================================================

// LOOP

// ============================================================

  

void loop()

{

    // --------------------------------------------------------

    // Segment timeout

    // --------------------------------------------------------

  

    if (

        millis() -

        recordingStart >=

        SEGMENT_SECONDS * 1000UL

    )

    {

        Serial.println();

        Serial.println(

            "Segment complete."

        );

  

        finalizeAVI();

  

        delay(100);

  

        if (!startAVI())

        {

            Serial.println(

                "FATAL: Could not start next segment."

            );

  

            while (true)

            {

                delay(1000);

            }

        }

  

        lastFrameTime =

            millis();

  

        return;

    }

  

    // --------------------------------------------------------

    // Capture JPEG

    // --------------------------------------------------------

  

    camera_fb_t *fb =

        esp_camera_fb_get();

  

    if (!fb)

    {

        droppedFrames++;

  

        Serial.println(

            "Camera frame failed."

        );

  

        return;

    }

  

    // --------------------------------------------------------

    // Write JPEG frame

    // --------------------------------------------------------

  

    bool success =

        writeFrame(fb);

  

    esp_camera_fb_return(fb);

  

    if (!success)

    {

        Serial.println();

        Serial.println(

            "FATAL: SD write failed."

        );

  

        finalizeAVI();

  

        while (true)

        {

            delay(1000);

        }

    }

  

    // --------------------------------------------------------

    // FPS control

    // --------------------------------------------------------

  

    const uint32_t frameInterval =

        1000 / FPS;

  

    uint32_t now =

        millis();

  

    uint32_t elapsed =

        now - lastFrameTime;

  

    if (elapsed < frameInterval)

    {

        delay(

            frameInterval -

            elapsed

        );

    }

  

    lastFrameTime =

        millis();

  

    // --------------------------------------------------------

    // Status every 5 seconds

    // --------------------------------------------------------

  

    static uint32_t lastStatus =

        0;

  

    if (

        millis() -

        lastStatus >=

        5000

    )

    {

        lastStatus =

            millis();

  

        uint32_t elapsedTime =

            millis() -

            recordingStart;

  

        float actualFPS =

            0;

  

        if (elapsedTime > 0)

        {

            actualFPS =

                (frameCount * 1000.0f) /

                elapsedTime;

        }

  

        Serial.print(

            "Frames: "

        );

  

        Serial.print(

            frameCount

        );

  

        Serial.print(

            " | FPS: "

        );

  

        Serial.print(

            actualFPS,

            2

        );

  

        Serial.print(

            " | Dropped: "

        );

  

        Serial.print(

            droppedFrames

        );

  

        Serial.print(

            " | Data: "

        );

  

        Serial.print(

            mjpegBytes / 1024

        );

  

        Serial.print(

            " KB"

        );

  

        Serial.print(

            " | File: "

        );

  

        Serial.print(

            aviFilePosition / 1024

        );

  

        Serial.println(

            " KB"

        );

    }

}

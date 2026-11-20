#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pngle.h"
#include "RainRadar.h"
#include "RainRadarDisplay.h"
#include "WiFiManager.h"

// Rain radar detailed debug
static const bool DEBUG_RAIN_RADAR = true;

// Settings
static constexpr unsigned long RADAR_INTERVAL = 5UL * 60UL * 1000UL;

static constexpr double MAP_CENTER_LAT = 35.45;
static constexpr double MAP_CENTER_LON = 139.60;

static constexpr int MAP_ZOOM = 8;
static constexpr int TILE_SIZE = 256;

static constexpr int DISPLAY_WIDTH = 320;
static constexpr int DISPLAY_HEIGHT = 187;
static constexpr int DISPLAY_Y      = 31;

static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

// Mask opacity: 60%
static constexpr uint8_t MASK_OPACITY = 255 * 0.6;

// RGB332 framebuffer: 1 byte per pixel
static constexpr size_t FRAMEBUFFER_SIZE = DISPLAY_WIDTH * DISPLAY_HEIGHT;

static const char* TARGET_TIMES_URL = "https://www.jma.go.jp/bosai/jmatile/data/nowc/targetTimes_N1.json";
static const char* RADAR_BASE_URL   = "https://www.jma.go.jp/bosai/jmatile/data/nowc/";
static const char* PALE2_BASE_URL   = "https://www.jma.go.jp/tile/gsi/pale2/";
static const char* MASK_BASE_URL    = "https://www.jma.go.jp/bosai/jmatile/data/map/none/none/none/surf/mask/";

// State
static unsigned long radarStartTime = 0;
static bool radarFirstRequest = true;

static uint8_t* compositeBuffer = nullptr;

// Structures

// 地図全体でのpixel座標
struct GlobalPixel {
    double x;
    double y;
};

// 必要なタイル番号の範囲
struct TileRange {
    int minTileIndexX;
    int maxTileIndexX;
    int minTileIndexY;
    int maxTileIndexY;
};

enum class TileLayer : uint8_t {
    BaseMap,
    Radar,
    Mask
};

// PNGタイルの情報
struct PngTileContext {
    TileLayer layer;

    int tileIndexX;
    int tileIndexY;

    int destinationPixelX;
    int destinationPixelY;
};

static PngTileContext pngTileContext;

static pngle_t* radarPngle = nullptr;

static uint32_t pngleImageWidth = 0;
static uint32_t pngleImageHeight = 0;
static uint32_t pnglePixelsDrawn = 0;

static bool pngleDecodeError = false;

// RGB conversion
static uint8_t rgb888To332(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>( ((r & 0xE0)     ) |
                                 ((g & 0xE0) >> 3) |
                                 ((b & 0xC0) >> 6) );
}

// Alpha blending
static uint8_t blendChannel(uint8_t destination, uint8_t source, uint8_t alpha) {
    /*
     *  (source x alpha) + (destination x ( 255 - alpha)) + 127
     * ---------------------------------------------------------
     *                         255
     *  (注) +127は四捨五入のため
     */
    return static_cast<uint8_t>( (static_cast<uint16_t>(source) * alpha +
                                  static_cast<uint16_t>(destination) * (255 - alpha) + 127)
                                  / 255 );
}

static uint8_t blendRGB332(uint8_t destination, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha)
{
    const uint8_t destR = static_cast<uint8_t>(((destination >> 5) & 0x07) * 255 / 7);
    const uint8_t destG = static_cast<uint8_t>(((destination >> 2) & 0x07) * 255 / 7);
    const uint8_t destB = static_cast<uint8_t>(((destination     ) & 0x03) * 255 / 3);

    const uint8_t outR = blendChannel(destR, r, alpha);
    const uint8_t outG = blendChannel(destG, g, alpha);
    const uint8_t outB = blendChannel(destB, b, alpha);

    return rgb888To332(outR, outG, outB);
}

// Geographic conversion
static GlobalPixel latLonToGlobalPixel(double latitude, double longitude, int zoom)
{
    const double latRad = latitude * M_PI / 180.0;
    const double mapSize = static_cast<double>(TILE_SIZE) * (1UL << zoom);

    GlobalPixel pixel;

    pixel.x = (longitude + 180.0) / 360.0 * mapSize;
    pixel.y = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * mapSize;

    return pixel;
}

static TileRange calculateTileRange()
{
    const GlobalPixel center = latLonToGlobalPixel(MAP_CENTER_LAT, MAP_CENTER_LON, MAP_ZOOM);

    const double left   = center.x - DISPLAY_WIDTH  / 2.0;
    const double right  = center.x + DISPLAY_WIDTH  / 2.0 - 1.0;
    const double top    = center.y - DISPLAY_HEIGHT / 2.0;
    const double bottom = center.y + DISPLAY_HEIGHT / 2.0 - 1.0;

    TileRange range;

    range.minTileIndexX = static_cast<int>(floor(left   / TILE_SIZE));
    range.maxTileIndexX = static_cast<int>(floor(right  / TILE_SIZE));
    range.minTileIndexY = static_cast<int>(floor(top    / TILE_SIZE));
    range.maxTileIndexY = static_cast<int>(floor(bottom / TILE_SIZE));

    return range;
}

// PNG callbacks
static void radarPngleInit(pngle_t* pngle, uint32_t width, uint32_t height)
{
    (void)pngle;

    pngleImageWidth  = width;
    pngleImageHeight = height;

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("PNG image dimensions: %lu x %lu\n",
                      static_cast<unsigned long>(width), static_cast<unsigned long>(height));
    }

    if (width != TILE_SIZE || height != TILE_SIZE) {
        pngleDecodeError = true;
        Serial.println("PNG error: unexpected tile dimensions.");
    }
}

static void radarPngleDraw(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4])
{
    (void)pngle;

    if (compositeBuffer == nullptr || pngleDecodeError || rgba == nullptr) {
        return;
    }

    // タイルの左上を画面座標系に変換する。
    const int tileOriginX = pngTileContext.tileIndexX * TILE_SIZE - pngTileContext.destinationPixelX;
    const int tileOriginY = pngTileContext.tileIndexY * TILE_SIZE - pngTileContext.destinationPixelY;

    // 描画対象の画像行を走査する。
    for (uint32_t dy = 0; dy < h; ++dy) {

        const int globalY = tileOriginY + static_cast<int>(y + dy);

        // 画面外の行はスキップする。
        if (globalY < 0 || globalY >= DISPLAY_HEIGHT) {
            continue;
        }

        // この行の合成バッファ先頭位置。
        const size_t rowStart = static_cast<size_t>(globalY) * DISPLAY_WIDTH;

        // この行の画像データ先頭位置。
        const size_t rgbaRowStart = static_cast<size_t>(dy) * w * 4;

        // 描画対象のピクセルを走査する。
        for (uint32_t dx = 0; dx < w; ++dx) {

            const size_t rgbaIndex = rgbaRowStart + static_cast<size_t>(dx) * 4;

            const uint8_t r = rgba[rgbaIndex];
            const uint8_t g = rgba[rgbaIndex + 1];
            const uint8_t b = rgba[rgbaIndex + 2];
            const uint8_t a = rgba[rgbaIndex + 3];

            // 完全透明なピクセルは変更しない。
            if (a == 0) {
                continue;
            }

            const int globalX = tileOriginX + static_cast<int>(x + dx);

            // 画面外のピクセルはスキップする。
            if (globalX < 0 || globalX >= DISPLAY_WIDTH) {
                continue;
            }

            const size_t bufferIndex = rowStart + static_cast<size_t>(globalX);

            uint8_t& destination = compositeBuffer[bufferIndex];

            switch (pngTileContext.layer) {

                case TileLayer::BaseMap: {
                    // 透明度を白背景に反映する。
                    const uint8_t flatR = blendChannel(255, r, a);
                    const uint8_t flatG = blendChannel(255, g, a);
                    const uint8_t flatB = blendChannel(255, b, a);

                    // グレースケールに変換する。
                    const uint8_t gray = static_cast<uint8_t>(
                        (299UL * flatR +
                         587UL * flatG +
                         114UL * flatB + 500) / 1000
                    );

                    // コントラストを下げる。
                    const uint8_t lowContrast = static_cast<uint8_t>((128 + gray + 1) / 2);

                    // 地図を暗くする。
                    const uint8_t adjusted = static_cast<uint8_t>((lowContrast + 1) / 2);

                    destination = rgb888To332(adjusted, adjusted, adjusted);

                    break;
                }

                case TileLayer::Radar: {
                    destination = blendRGB332(destination, r, g, b, a);
                    break;
                }

                case TileLayer::Mask: {
                    const uint8_t maskAlpha = static_cast<uint8_t>(
                        (static_cast<uint16_t>(a) * MASK_OPACITY + 127) / 255
                    );

                    destination = blendRGB332(destination, r, g, b, maskAlpha);

                    break;
                }
            }
            ++pnglePixelsDrawn;
        }
    }
}

// Fetch latest radar times
static bool fetchRainRadarTimes(RainRadarTimes& times)
{
    HTTPClient http;
    WiFiClientSecure client;

    client.setInsecure();

    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, TARGET_TIMES_URL)) {
        Serial.println("Radar times: HTTP begin failed.");
        return false;
    }

    const int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Radar times: HTTP error %d\n", httpCode);

        http.end();
        return false;
    }

    DynamicJsonDocument doc(16384);
    const DeserializationError error = deserializeJson(doc, http.getStream());

    http.end();

    if (error) {
        Serial.printf("Radar times: JSON error: %s\n", error.c_str());
        return false;
    }

    JsonArray items = doc.as<JsonArray>();

    for (JsonObject item : items) {
        const char* base  = item["basetime"]  | "";
        const char* valid = item["validtime"] | "";

        JsonArray elements = item["elements"].as<JsonArray>();

        bool hasHrpns = false;

        for (JsonVariant element : elements) {
            const char* name = element | "";

            if (strcmp(name, "hrpns") == 0) {
                hasHrpns = true;
                break;
            }
        }

        if (hasHrpns && strlen(base) > 0 && strlen(valid) > 0) {
            times.basetime  = base;
            times.validtime = valid;
            times.valid = true;

            if (DEBUG_RAIN_RADAR) {
                Serial.printf("Radar basetime:  %s\n", times.basetime.c_str());
                Serial.printf("Radar validtime: %s\n", times.validtime.c_str());
            }

            return true;
        }
    }

    Serial.println("Radar times: no hrpns entry found.");

    return false;
}

// Decode and composite one PNG tile
static bool decodeAndCompositePngTile(const String& url, const char* tileType, TileLayer layer,
                                      int tileIndexX, int tileIndexY, int destinationPixelX, int destinationPixelY)
{
    HTTPClient http;
    WiFiClientSecure client;

    client.setInsecure();

    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, url)) {
        Serial.printf("%s tile: HTTP begin failed.\n", tileType);
        return false;
    }

    const int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("%s tile: HTTP error %d\n", tileType, httpCode);
        http.end();
        return false;
    }

    const int contentLength = http.getSize();

    if (contentLength <= 0) {
        Serial.printf("%s tile: invalid content length: %d\n", tileType, contentLength);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    if (stream == nullptr) {
        Serial.printf("%s tile: no response stream.\n", tileType);
        http.end();
        return false;
    }

    radarPngle = pngle_new();

    if (radarPngle == nullptr) {
        Serial.println("PNG error: failed to allocate decoder.");
        http.end();
        return false;
    }

    pngTileContext.layer             = layer;
    pngTileContext.tileIndexX        = tileIndexX;
    pngTileContext.tileIndexY        = tileIndexY;
    pngTileContext.destinationPixelX = destinationPixelX;
    pngTileContext.destinationPixelY = destinationPixelY;

    pngleImageWidth  = 0;
    pngleImageHeight = 0;
    pnglePixelsDrawn = 0;
    pngleDecodeError = false;

    pngle_set_init_callback(radarPngle, radarPngleInit);
    pngle_set_draw_callback(radarPngle, radarPngleDraw);

    uint8_t inputBuffer[1024];

    size_t totalReceived = 0;
    size_t remain = 0;

    bool decodeOK = true;

    const unsigned long startTime = millis();
    unsigned long lastDataTime = startTime;

    while (totalReceived < static_cast<size_t>(contentLength) || remain > 0) {
        if (millis() - startTime > HTTP_TIMEOUT_MS) {
            Serial.printf("%s tile: total timeout.\n", tileType);
            decodeOK = false;
            break;
        }

        // Read new data after any unconsumed bytes.
        if (stream->available() > 0 && remain < sizeof(inputBuffer)) {
            size_t requestBytes = sizeof(inputBuffer) - remain;

            const int availableBytes = stream->available();

            if (requestBytes > static_cast<size_t>(availableBytes)) {
                requestBytes = static_cast<size_t>(availableBytes);
            }
            const size_t bytesLeft = static_cast<size_t>(contentLength) - totalReceived;

            if (requestBytes > bytesLeft) {
                requestBytes = bytesLeft;
            }

            if (requestBytes > 0) {
                const int bytesRead = stream->read(inputBuffer + remain, requestBytes);

                if (bytesRead > 0) {
                    remain += static_cast<size_t>(bytesRead);
                    totalReceived += static_cast<size_t>(bytesRead);

                    lastDataTime = millis();
                }
            }
        }

        // No buffered data yet: wait for the next chunk.
        if (remain == 0) {
            if (millis() - lastDataTime > HTTP_TIMEOUT_MS) {
                Serial.printf("%s tile: stream timeout.\n", tileType);
                decodeOK = false;
                break;
            }
            delay(1);
            continue;
        }

        // Feed all currently buffered bytes to pngle.
        const int consumed = pngle_feed(radarPngle, inputBuffer, remain);

        if (consumed < 0) {
            const char* error = pngle_error(radarPngle);

            Serial.printf("%s tile: PNG decode error: %s\n", tileType, error ? error : "unknown");
            decodeOK = false;
            break;
        }

        if (static_cast<size_t>(consumed) > remain) {
            Serial.printf("%s tile: invalid consumed byte count.\n", tileType);
            decodeOK = false;
            break;
        }

        // Preserve any bytes not consumed by pngle.
        remain -= static_cast<size_t>(consumed);

        if (remain > 0 && consumed > 0) {
            memmove(inputBuffer, inputBuffer + consumed, remain);
        }

        // If pngle cannot consume the data, read more
        // before trying again. Never discard the remainder.
        if (consumed == 0) {
            if (remain == sizeof(inputBuffer)) {
                Serial.printf("%s tile: PNG input buffer is full.\n", tileType);
                decodeOK = false;
                break;
            }

            if (totalReceived >= static_cast<size_t>(contentLength)) {
                Serial.printf("%s tile: decoder stalled at end of file.\n", tileType);
                decodeOK = false;
                break;
            }
        }

        if (pngleDecodeError) {
            decodeOK = false;
            break;
        }
    }

    // All HTTP bytes must have arrived and been consumed.
    if (totalReceived != static_cast<size_t>(contentLength)) {
        Serial.printf("%s tile: incomplete download (%u / %d bytes).\n",
                      tileType, static_cast<unsigned>(totalReceived), contentLength);
        decodeOK = false;
    }

    if (remain != 0) {
        Serial.printf("%s tile: %u unconsumed bytes remain.\n", tileType, static_cast<unsigned>(remain));
        decodeOK = false;
    }

    if (pngleImageWidth != TILE_SIZE ||
        pngleImageHeight != TILE_SIZE) {
        Serial.printf("%s tile: unexpected dimensions.\n", tileType);
        decodeOK = false;
    }

    if (pngleDecodeError) {
        decodeOK = false;
    }

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("%s tile (%d, %d): %u bytes, %lu pixels\n",
                      tileType,
                      tileIndexX, tileIndexY,
                      static_cast<unsigned>(totalReceived), static_cast<unsigned long>(pnglePixelsDrawn) );
    }

    pngle_destroy(radarPngle);
    radarPngle = nullptr;

    http.end();

    return decodeOK;
}

// Tile URL builders
static String makeRadarTileUrl(const RainRadarTimes& times, int tileIndexX, int tileIndexY)
{
    char url[256];

    snprintf(url, sizeof(url),
             "%s%s/none/%s/surf/hrpns/%d/%d/%d.png",
             RADAR_BASE_URL, times.basetime.c_str(), times.validtime.c_str(), MAP_ZOOM, tileIndexX, tileIndexY);

    return String(url);
}

static String makePale2TileUrl(int tileIndexX, int tileIndexY)
{
    char url[192];

    snprintf(url, sizeof(url),
             "%s%d/%d/%d.png",
             PALE2_BASE_URL, MAP_ZOOM, tileIndexX, tileIndexY);

    return String(url);
}

static String makeMaskTileUrl(int tileIndexX, int tileIndexY)
{
    char url[256];

    snprintf(url, sizeof(url),
             "%s%d/%d/%d.png",
             MASK_BASE_URL, MAP_ZOOM, tileIndexX, tileIndexY);

    return String(url);
}

// Fetch tile layers
static bool fetchPale2Tile(int tileIndexX, int tileIndexY, int destinationPixelX, int destinationPixelY)
{
    return decodeAndCompositePngTile(
        makePale2TileUrl(tileIndexX, tileIndexY),
        "Pale2",
        TileLayer::BaseMap,
        tileIndexX,
        tileIndexY,
        destinationPixelX,
        destinationPixelY
    );
}

static bool fetchRainRadarTile(const RainRadarTimes& times, int tileIndexX, int tileIndexY, int destinationPixelX, int destinationPixelY)
{
    return decodeAndCompositePngTile(
        makeRadarTileUrl(times, tileIndexX, tileIndexY),
        "Radar",
        TileLayer::Radar,
        tileIndexX,
        tileIndexY,
        destinationPixelX,
        destinationPixelY
    );
}

static bool fetchMaskTile(int tileIndexX, int tileIndexY, int destinationPixelX, int destinationPixelY)
{
    return decodeAndCompositePngTile(
        makeMaskTileUrl(tileIndexX, tileIndexY),
        "Mask",
        TileLayer::Mask,
        tileIndexX,
        tileIndexY,
        destinationPixelX,
        destinationPixelY
    );
}

static bool fetchPale2Tiles(const TileRange& range, int destinationPixelX, int destinationPixelY)
{
    int total = 0;
    int success = 0;

    for (int tileIndexY = range.minTileIndexY; tileIndexY <= range.maxTileIndexY; ++tileIndexY) {
        for (int tileIndexX = range.minTileIndexX; tileIndexX <= range.maxTileIndexX; ++tileIndexX) {
            ++total;

            if (fetchPale2Tile(tileIndexX, tileIndexY, destinationPixelX, destinationPixelY)) {
                ++success;
            } else {
                Serial.printf("Pale2 tile failed: z=%d x=%d y=%d\n", MAP_ZOOM, tileIndexX, tileIndexY);
                return false;
            }
        }
    }

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Pale2 tiles: %d/%d fetched successfully\n", success, total);
    }

    return success == total;
}

static bool fetchRainRadarTiles(const RainRadarTimes& times, const TileRange& range,
                                int destinationPixelX, int destinationPixelY)
{
    int total = 0;
    int success = 0;

    for (int tileIndexY = range.minTileIndexY; tileIndexY <= range.maxTileIndexY; ++tileIndexY) {
        for (int tileIndexX = range.minTileIndexX; tileIndexX <= range.maxTileIndexX; ++tileIndexX) {
            ++total;

            if (fetchRainRadarTile(times, tileIndexX, tileIndexY, destinationPixelX, destinationPixelY)) {
                ++success;
            } else {
                Serial.printf("Radar tile failed: z=%d x=%d y=%d\n", MAP_ZOOM, tileIndexX, tileIndexY);
                return false;
            }
        }
    }

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Radar tiles: %d/%d fetched successfully\n", success, total);
    }

    return success == total;
}

static bool fetchMaskTiles(const TileRange& range, int destinationPixelX, int destinationPixelY)
{
    int total = 0;
    int success = 0;

    for (int tileIndexY = range.minTileIndexY; tileIndexY <= range.maxTileIndexY; ++tileIndexY) {
        for (int tileIndexX = range.minTileIndexX; tileIndexX <= range.maxTileIndexX; ++tileIndexX) {
            ++total;

            if (fetchMaskTile(tileIndexX, tileIndexY, destinationPixelX, destinationPixelY)) {
                ++success;
            } else {
                Serial.printf("Mask tile failed: z=%d x=%d y=%d\n", MAP_ZOOM, tileIndexX, tileIndexY);
                return false;
            }
        }
    }

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Mask tiles: %d/%d fetched successfully\n", success, total);
    }

    return success == total;
}

// Fetch, composite, and display rain radar
static bool fetchRainRadar(RainRadarTimes& times)
{
    Serial.println("========== Rain radar update ==========");

    times.valid = false;

    if (!fetchRainRadarTimes(times)) {
        Serial.println("Rain radar update failed: target times.");
        return false;
    }

    const GlobalPixel center = latLonToGlobalPixel(
        MAP_CENTER_LAT,
        MAP_CENTER_LON,
        MAP_ZOOM
    );

    const int destinationPixelX = static_cast<int>(lround(center.x - DISPLAY_WIDTH / 2.0));
    const int destinationPixelY = static_cast<int>(lround(center.y - DISPLAY_HEIGHT / 2.0));

    const TileRange range = calculateTileRange();

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Tile range: z=%d, x=%d-%d, y=%d-%d\n",
                      MAP_ZOOM, range.minTileIndexX, range.maxTileIndexX, range.minTileIndexY, range.maxTileIndexY);
    }

    if (compositeBuffer == nullptr) {
        compositeBuffer = static_cast<uint8_t*>(malloc(FRAMEBUFFER_SIZE));
    }

    if (compositeBuffer == nullptr) {
        Serial.println("Rain radar update failed: framebuffer allocation.");
        Serial.printf("Free heap: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
        return false;
    }

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Free heap before tile decoding: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
    }

    memset(compositeBuffer, 0, FRAMEBUFFER_SIZE);

    // Layer 1: base map
    if (!fetchPale2Tiles(range, destinationPixelX, destinationPixelY)) {
        Serial.println("Rain radar update failed: Pale2 layer.");
        return false;
    }

    // Layer 2: radar precipitation
    if (!fetchRainRadarTiles(times, range, destinationPixelX, destinationPixelY)) {
        Serial.println("Rain radar update failed: radar layer.");
        return false;
    }

    // Layer 3: mask
    if (!fetchMaskTiles(range, destinationPixelX, destinationPixelY)) {
        Serial.println("Rain radar update failed: mask layer.");
        return false;
    }

    rainRadarDisplayShow(compositeBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_Y);

    if (DEBUG_RAIN_RADAR) {
        Serial.printf("Free heap after composition: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
    }

    Serial.println("Rain radar update: all tiles fetched successfully.");
    Serial.println("=======================================");

    return true;
}

// Public update function
bool rainRadarUpdate(RainRadarTimes& times)
{
    if (!isTimeSynchronized()) {
        return false;
    }

    if (!radarFirstRequest && millis() - radarStartTime < RADAR_INTERVAL) {
        return false;
    }

    radarStartTime = millis();

    Serial.println();
    Serial.println("Starting rain radar request...");

    if (fetchRainRadar(times)) {
        radarFirstRequest = false;

        Serial.println("Rain radar request succeeded.");

        return true;
    } else {
        Serial.println("Rain radar request failed.");

        networkReconnect();

        return false;
    }
}

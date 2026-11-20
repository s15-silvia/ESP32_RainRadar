#include <WiFi.h>
#include "WiFiManager.h"

// WiFi settings
const char SSID[] = "xxxxx";
const char PASSWD[] = "xxxxx";

const unsigned long RECONNECT_DELAY = 15000;

// NTP settings
const char NTP_SERVER[] = "ntp.nict.jp";

const long GMT_OFFSET_SEC = 9 * 60 * 60;
const int DAYLIGHT_OFFSET_SEC = 0;

const unsigned long NTP_REQUEST_DELAY = 5000;

// Network state
volatile bool reconnectPending = false;
volatile unsigned long reconnectStartTime = 0;

volatile bool timeSyncRequest = false;
unsigned long timeSyncRequestTime = 0;

bool timeSynchronized = false;


// WiFi event handlers
static void WiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("Connected to AP successfully!");
}

static void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("=== WiFi connected ===");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  reconnectPending = false;

  // NTP同期要求
  timeSyncRequestTime = millis() + NTP_REQUEST_DELAY;
  timeSyncRequest = true;
}

static void WiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println();
  Serial.println("=== WiFi disconnected ===");

  Serial.print("Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);

  Serial.print("WiFi.status(): ");
  Serial.println(WiFi.status());

  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());

  // NTP同期状態をリセット
  timeSynchronized = false;

  // 最後のDISCONNECTEDから再接続タイマーを開始
  // 再度DISCONNECTEDが発生した場合はタイマーをリセット
  reconnectPending = true;
  reconnectStartTime = millis();

  Serial.println("Reconnect timer started/reset.");
}

// NTP
static bool syncTime()
{
  Serial.println();
  Serial.println("=== NTP time sync ===");

  Serial.print("NTP server: ");
  Serial.println(NTP_SERVER);

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  struct tm timeinfo;

  // 最大10秒待つ
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&timeinfo, 1000)) {
      Serial.println("Time synchronized.");
      Serial.printf("Current time: %04d/%02d/%02d %02d:%02d:%02d\n",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      );
      return true;
    }
    Serial.println("Waiting for NTP...");
  }

  Serial.println("NTP synchronization failed.");

  return false;
}

void networkBegin()
{
  WiFi.mode(WIFI_STA);

  // ESP32の自動再接続は使用しない
  // DISCONNECTED後、一定時間待ってからloop()内から呼ばれるnetworkUpdate()で再接続する
  WiFi.setAutoReconnect(false);

  WiFi.onEvent( WiFiConnected,    WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent( WiFiGotIP,        WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent( WiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  Serial.println("=== WiFi connect Start ===");

  Serial.print("SSID: ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWD);
}

void networkUpdate()
{
  // WiFi reconnect
  if (reconnectPending && millis() - reconnectStartTime >= RECONNECT_DELAY) {
    reconnectPending = false;
    Serial.println("=== WiFi reconnect ===");

    WiFi.begin(SSID, PASSWD);
  }

  // NTP Sync
  if (timeSyncRequest && millis() >= timeSyncRequestTime) {
    timeSyncRequest = false;

    if (syncTime()) {
      timeSynchronized = true;
    } else {
      Serial.println("NTP sync failed.");
      Serial.println("WiFi reconnect requested.");

      WiFi.disconnect();
    }
  }
}

bool isTimeSynchronized()
{
  return timeSynchronized;
}

void networkReconnect()
{
  Serial.println("WiFi reconnect requested.");

  WiFi.disconnect();
}

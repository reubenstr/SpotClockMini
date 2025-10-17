/*
  APL Door Sign
  Reuben Strangelove
  Feb 2025

  Displays office information with a LED backlight.

  MCU:
    ESP32-C3 Super Mini: https://forum.arduino.cc/t/esp32-c3-supermini-pinout/1189850



  export SPOTCLOCK_WIFI_SSID="REPLACE_WITH_YOUR_SSID"
  export SPOTCLOCK_WIFI_PASS="REPLACE_WITH_YOUR_PASSWORD"

  */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_ST7789.h>
#include "main.h"

#ifndef SPOTCLOCK_WIFI_SSID
#error "WIFI_SSID is not defined. Set env var or pass -D WIFI_SSID=... to PlatformIO."
#endif
#ifndef SPOTCLOCK_WIFI_PASS
#error "WIFI_PASS is not defined. Set env var or pass -D WIFI_PASS=... to PlatformIO."
#endif

#define TFT_CS 0
#define TFT_RST 1
#define TFT_DC 2

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

const unsigned long heartbeatDelayMs{500};

Status status{};

int selectedApiIndex = 1;
ApiInfo *selectedApi = selectApiByIndex(selectedApiIndex);

///////////////////////////////////////////////////////////////////////////////
// General
///////////////////////////////////////////////////////////////////////////////

void heartbeat()
{
  static unsigned long start = 0;
  static bool state{false};

  if (millis() - start > heartbeatDelayMs)
  {
    start = millis();
    state = !state;
    digitalWrite(BUILTIN_LED, state);
  }
}

const char *formatEpochToHourMinute(unsigned long epoch)
{
  static char timeString[6];
  time_t rawtime = (time_t)epoch;
  struct tm *timeinfo = localtime(&rawtime);

  if (timeinfo)
  {
    snprintf(timeString, sizeof(timeString), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    return timeString;
  }
  else
  {
    return "00:00";
  }
}

///////////////////////////////////////////////////////////////////////////////
// Display Methods
///////////////////////////////////////////////////////////////////////////////

void initDisplay()
{
  tft.init(170, 320);
  delay(250);
  tft.setRotation(3);
  delay(250);
  tft.fillScreen(ST77XX_BLACK);
}

void updateValues()
{
  float val1 = 4200.45;
  float val2 = 150.45;
  float val3 = 102.57;
  float val4 = 1.45;

  int screenWidth = tft.width();

  struct ValueDisplay
  {
    float value;
    int y;          // Vertical position
    uint16_t color; // Text color
    int textSize;
  };

  ValueDisplay values[] = {
      {val1, 0, ST77XX_GREEN, 4},
      {val2, 40, ST77XX_GREEN, 3},
      {val3, 80, ST77XX_RED, 4},
      {val4, 120, ST77XX_GREEN, 3}};

  for (int i = 0; i < 4; i++)
  {
    char buf[10];
    dtostrf(values[i].value, 7, 2, buf);
    int charWidth = 6 * values[i].textSize;
    int textPixelWidth = strlen(buf) * charWidth;

    int x = (screenWidth - textPixelWidth) / 2;

    tft.setCursor(x, values[i].y);
    tft.setTextColor(values[i].color);
    tft.setTextSize(values[i].textSize);
    tft.print(buf);
  }
}

void updateIndicators()
{

  static bool firstEntry{true};
  static Status prevStatus{};

  int y = tft.height() - 16;
  int padding = 2;
  int textSize = 2;
  int textHeight = 8 * textSize;
  int radius = 0;
  int h = textHeight + 2 * padding;

  if (prevStatus.wifi != status.wifi || firstEntry)
  {
    prevStatus.wifi = status.wifi;

    const char *label1 = "WiFi";
    int x1 = 0;
    int w1 = strlen(label1) * 6 * textSize + 2 * padding;
    tft.fillRoundRect(x1, y - padding, w1, h, radius, status.wifi ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(x1 + padding, y);
    tft.setTextColor(ST77XX_BLACK);
    tft.setTextSize(textSize);
    tft.print(label1);
  }

  if (prevStatus.www != status.www || firstEntry)
  {
    prevStatus.www = status.www;

    const char *label2 = "WWW";
    int x2 = 60;
    int w2 = strlen(label2) * 6 * textSize + 2 * padding;
    tft.fillRoundRect(x2, y - padding, w2, h, radius, status.www ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(x2 + padding, y);
    tft.setTextColor(ST77XX_BLACK);
    tft.print(label2);
  }

  if (prevStatus.api != status.api || firstEntry)
  {
    prevStatus.api = status.api;

    const char *label3 = "API";
    int x3 = 110;
    int w3 = strlen(label3) * 6 * textSize + 2 * padding;
    tft.fillRoundRect(x3, y - padding, w3, h, radius, status.api ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(x3 + padding, y);
    tft.setTextColor(ST77XX_BLACK);
    tft.print(label3);
  }

  if (prevStatus.fetch != status.fetch || firstEntry)
  {
    prevStatus.fetch = status.fetch;

    const char *label4 = "FETCH";
    int x4 = 160;
    int w4 = strlen(label4) * 6 * textSize + 2 * padding;
    tft.fillRoundRect(x4, y - padding, w4, h, radius, status.fetch ? ST77XX_BLUE: ST77XX_WHITE);
    tft.setCursor(x4 + padding, y);
    tft.setTextColor(ST77XX_BLACK);
    tft.print(label4);
  }

  if (prevStatus.timestamp != status.timestamp || firstEntry)
  {
    prevStatus.timestamp = status.timestamp;

    const char *label5 = formatEpochToHourMinute(status.timestamp);
    int x5 = 230;
    int w5 = strlen(label5) * 6 * textSize + 2 * padding;
    tft.fillRoundRect(x5, y - padding, w5, h, radius, ST77XX_WHITE);
    tft.setCursor(x5 + padding, y);
    tft.setTextColor(ST77XX_BLACK);
    tft.print(label5);
  }

  firstEntry = false;
}

///////////////////////////////////////////////////////////////////////////////
// WiFi
///////////////////////////////////////////////////////////////////////////////

void connectWifi()
{
  const unsigned long connectionTimeoutMs{15000};

  Serial.print("[WiFi] Connecting to SSID: ");
  Serial.println(SPOTCLOCK_WIFI_SSID);

  status.wifi = false;
  status.www = false;
  status.api = false;
  updateIndicators();

  WiFi.begin(SPOTCLOCK_WIFI_SSID, SPOTCLOCK_WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - start > connectionTimeoutMs)
    {
      Serial.println("[WiFi] Failed to connect (timeout).");
      break;
    }
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    status.wifi = true;
    Serial.println("[WiFi] Connected");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  }
}

void checkWifi()
{
  const unsigned long connectionTimeoutMs{5000};
  static unsigned long last = millis();

  if (WiFi.status() == WL_CONNECTED)
  {
    last = millis();
  }
  else
  {
    if (millis() - last > connectionTimeoutMs)
    {
      connectWifi();
      last = millis();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////

void setup()
{
  pinMode(BUILTIN_LED, OUTPUT);

  Serial.begin(115200);

  while (!Serial)
  {
    ; // wait for serial port to connect. Only during development
  }

  Serial.println("SpotClock Mini Startup");

  initDisplay();
  updateIndicators();

  connectWifi();

  if (selectedApi)
  {
    Serial.println("Selected API:");
    Serial.print("Name: ");
    Serial.println(selectedApi->name);
    Serial.print("URL: ");
    Serial.println(selectedApi->url);
    Serial.print("Rate: ");
    Serial.println(selectedApi->rateLimit);
  }
  else
  {
    Serial.println("Invalid API index!");
  }
}

///////////////////////////////////////////////////////////////////////////////
// Main Loop
///////////////////////////////////////////////////////////////////////////////

void loop()
{
  heartbeat();

  checkWifi();

  updateValues();

  updateIndicators();

  delay(10);
}

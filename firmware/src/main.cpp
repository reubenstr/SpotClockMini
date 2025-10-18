/*
  SpotClockMini
  Reuben Strangelove
  Oct 2025

  Displays Gold and Silver spot prices.

  MCU:
    ESP32-C3 Super Mini: https://forum.arduino.cc/t/esp32-c3-supermini-pinout/1189850

  API:
    Data provided by swissquote.com while not data rich, API access is free.



  To setup WiFi credentiuals, modify and add the following lines to your .bashrc:
    export SPOTCLOCK_WIFI_SSID="REPLACE_WITH_YOUR_SSID"
    export SPOTCLOCK_WIFI_PASS="REPLACE_WITH_YOUR_PASSWORD"

  */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Ping.h>
#include <unordered_map>
#include <map>
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

const unsigned long connectionTimeoutMs{30000};
const unsigned long heartbeatDelayMs{500};
const unsigned long qouteCycleTimeMs{1000};
const unsigned long apiFetchRateMs{10000};
const unsigned long pingRateMs{5000};

Status status{};

std::map<Element, Quote> quotes = {
    {Element::AU, {0.0f, 0.0f}},
    {Element::AG, {0.0f, 0.0f}},
    {Element::PT, {0.0f, 0.0f}},
};

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

void printIndicator(int x, int y, int size, const char *text, int fgColor, int bgColor)
{
  int padding = 2;
  int textSize = 2;
  int textHeight = 8 * textSize;
  int radius = 0;
  int h = textHeight + 2 * padding;

  tft.setTextSize(textSize);

  const char *label1 = "WiFi";
  int w1 = strlen(text) * 6 * textSize + 2 * padding;
  tft.fillRoundRect(x, y - padding, w1, h, radius, bgColor);
  tft.setCursor(x + padding, y);
  tft.setTextColor(fgColor);
  tft.print(text);
}

void updateDisplayQuotes()
{
  static unsigned long start{0};
  static Element element{Element::AG};

  if (millis() - start > qouteCycleTimeMs)
  {
    start = millis();

    element = nextElement(element);

    int textSize = 2;
    int w = strlen(elementTextMap.at(element)) * 6 * textSize;
    tft.setTextSize(4);
    tft.setCursor(tft.width() / 2 - w, 10);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(elementTextMap.at(element));

    char buf[10];
    int charWidth, textPixelWidth;

    float price = quotes[element].currentPrice;
    float delta = quotes[element].yesterdayClose;

    int qouteColor = ST77XX_GREEN;
    int deltaColor = ST77XX_RED;

    int largeTextSize = 6;
    int smallTextSize = 3;

    dtostrf(price, 7, 2, buf);
    charWidth = 6 * largeTextSize;
    textPixelWidth = strlen(buf) * charWidth;
    tft.setCursor((tft.width() - textPixelWidth) / 2, 60);
    tft.setTextColor(qouteColor);
    tft.setTextSize(largeTextSize);
    tft.print(buf);

    dtostrf(delta, 7, 2, buf);
    charWidth = 6 * smallTextSize;
    textPixelWidth = strlen(buf) * charWidth;
    tft.setCursor((tft.width() - textPixelWidth) / 2, 110);
    tft.setTextColor(deltaColor);
    tft.setTextSize(smallTextSize);
    tft.print(buf);
  }
}

void updateDisplayIndicators()
{
  static bool firstEntry{true};
  static Status prevStatus;

  int textSize = 2;
  int y = tft.height() - 21;

  if (prevStatus.wifi != status.wifi || firstEntry)
  {
    prevStatus.wifi = status.wifi;
    printIndicator(5, y, textSize, "WiFi", ST77XX_BLACK, status.wifi ? ST77XX_GREEN : ST77XX_RED);
  }

  if (prevStatus.www != status.www || firstEntry)
  {
    prevStatus.www = status.www;
    printIndicator(65, y, textSize, "WWW", ST77XX_BLACK, status.www ? ST77XX_GREEN : ST77XX_RED);
  }

  if (prevStatus.api != status.api || firstEntry)
  {
    prevStatus.api = status.api;
    printIndicator(120, y, textSize, "API", ST77XX_BLACK, status.api ? ST77XX_GREEN : ST77XX_RED);
  }

  if (prevStatus.fetch != status.fetch || firstEntry)
  {
    prevStatus.fetch = status.fetch;
    printIndicator(175, y, textSize, "Fetch", ST77XX_BLACK, status.fetch ? ST77XX_BLUE : ST77XX_WHITE);
  }

  if (prevStatus.timestamp != status.timestamp || firstEntry)
  {
    prevStatus.timestamp = status.timestamp;
    const char *text = formatEpochToHourMinute(status.timestamp);
    printIndicator(250, y, textSize, text, ST77XX_BLACK, ST77XX_WHITE);
  }

  firstEntry = false;
}

void displayWifiConnectionMessage()
{
  tft.fillScreen(ST77XX_BLACK);

  int padding = 5;
  int textSize = 3;
  int textHeight = 8 * textSize;
  int radius = 3;
  int y = tft.height() / 2 - textHeight / 2;
  int h = textHeight + 2 * padding;

  tft.setTextSize(textSize);

  const char *label = "Connecting...";
  int w = strlen(label) * 6 * textSize + 2 * padding;
  int x = (tft.width() - w) / 2;
  tft.fillRoundRect(x, y - padding, w, h, radius, ST77XX_YELLOW);
  tft.setCursor(x + padding, y);
  tft.setTextColor(ST77XX_BLACK);
  tft.print(label);
}

void displayWifiConnectionTick()
{
  static bool toggle{false};
  toggle = !toggle;

  tft.setTextSize(3);
  tft.setCursor(290, 135);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  if (toggle)
    tft.print("-");
  else
    tft.print("+");
}

void displayNormal()
{
  tft.fillScreen(ST77XX_BLACK);
  
  // Frame.
  tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_BLUE);
  tft.drawFastHLine(0, 45, tft.width(), ST77XX_BLUE);
  tft.drawFastHLine(0, tft.height() - 26, tft.width(), ST77XX_BLUE);
}

///////////////////////////////////////////////////////////////////////////////
// WiFi
///////////////////////////////////////////////////////////////////////////////

void connectWifi()
{
  Serial.print("[WiFi] Connecting to SSID: ");
  Serial.println(SPOTCLOCK_WIFI_SSID);

  status.wifi = false;
  status.www = false;
  status.api = false;

  displayWifiConnectionMessage();

  WiFi.begin(SPOTCLOCK_WIFI_SSID, SPOTCLOCK_WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - start > connectionTimeoutMs)
    {
      Serial.println("[WiFi] Failed to connect (timeout).");
      Serial.println("[WiFi] Rebooting ESP32...");
      delay(3000);
      ESP.restart();
    }
    Serial.print(".");
    displayWifiConnectionTick();
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    displayNormal();
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
// API Methods
///////////////////////////////////////////////////////////////////////////////

void fetchData(Element element)
{
  WiFiClientSecure client;
  // client.setInsecure(); // disable cert verification
  client.setCACert(rootCACert);
  HTTPClient https;
  https.setTimeout(8000);
  String url = apiEndpoints.at(element);

  Serial.printf("[API] Fetching element %s from %s\n", elementTextMap.at(element), url.c_str());

  if (https.begin(client, url))
  {
    int httpCode = https.GET();
    if (httpCode > 0)
    {
      if (httpCode == HTTP_CODE_OK)
      {
        status.api = true;

        String payload = https.getString();
        Serial.println("Response:");
        Serial.println(payload);
      }
      else
      {
        Serial.printf("[API] Error, HTTP code: %d\n", httpCode);
        status.api = false;
      }
    }
    else
    {
      Serial.printf("[API] GET failed, error: %s\n", https.errorToString(httpCode).c_str());
      status.api = false;
    }

    https.end();
  }
  else
  {
    status.api = false;
    Serial.println("[API] Unable to connect");
  }
}

void processApi()
{
  static unsigned long start{0};
  static Element element{Element::AU};

  if (millis() - start > apiFetchRateMs)
  {
    start = millis();

    if (WiFi.status() == WL_CONNECTED)
    {
      element = nextElement(element);

      status.fetch = true;
      updateDisplayIndicators();

      fetchData(element);

      status.fetch = false;
      updateDisplayIndicators();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Ping
///////////////////////////////////////////////////////////////////////////////

void checkWebConnection()
{

  static unsigned long start{0};

  if (millis() - start > pingRateMs)
  {
    start = millis();

    if (Ping.ping("www.google.com"))
    {
      status.www = true;
    }
    else
    {
      status.www = false;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////

void setup()
{
  pinMode(BUILTIN_LED, OUTPUT);

  delay(2000);
  Serial.begin(115200);

  Serial.println("SpotClock Mini Startup");

  initDisplay();

  connectWifi();
}

///////////////////////////////////////////////////////////////////////////////
// Main Loop
///////////////////////////////////////////////////////////////////////////////

void loop()
{
  heartbeat();

  checkWifi();

  checkWebConnection();  

  updateDisplayQuotes();

  updateDisplayIndicators();

  processApi();

  delay(25);
}

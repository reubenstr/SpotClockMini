/*
  SpotClockMini
  Reuben Strangelove
  Oct 2025

  Displays Gold and Silver spot prices.

  MCU:
    ESP32-C3 Super Mini: https://forum.arduino.cc/t/esp32-c3-supermini-pinout/1189850
    Note: The antenna on this module is poorly designed and needs modification - or just use another ESP32

  API:
    Data provided by swissquote.com while not data rich, API access is free and does not require a key.

  WiFi:
    Add the following lines to your .bashrc:
      export SPOTCLOCK_WIFI_SSID="<REPLACE_WITH_YOUR_SSID>"
      export SPOTCLOCK_WIFI_PASS="<REPLACE_WITH_YOUR_PASSWORD>"
  */

#include <Arduino.h>
#include <ArduinoJson.h>
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
#error "SPOTCLOCK_WIFI_SSID is not defined."
#endif
#ifndef SPOTCLOCK_WIFI_PASS
#error "SPOTCLOCK_WIFI_PASS is not defined."
#endif

#define TFT_CS 0
#define TFT_RST 1
#define TFT_DC 2
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

const unsigned long connectionTimeoutMs{30000};
const unsigned long heartbeatRateMs{250};
const unsigned long qouteCycleTimeMs{2000};
const unsigned long apiFetchRateMs{15000};
const unsigned long pingRateMs{15000};

Status status{};

std::map<Element, Quote> quotes = {
    {Element::AU, {0.0f, 0.0f, 0, 0}},
    {Element::AG, {0.0f, 0.0f, 0, 0}},
    {Element::PT, {0.0f, 0.0f, 0, 0}},
};

// NTP for RTC updates; set EST
const char *tz = "EST5EDT,M3.2.0/2,M11.1.0/2";
const char *ntpServer = "pool.ntp.org";

// Backlight brightness control:
const int pwmPin = 3;
const int pwmChannel = 0;
const int pwmFrequency = 5000;
const int pwmResolution = 8;
const int pwmFullBrightnessDuty = 255;
const int pwmDimBrightnessDuty = 20;
const int dimStartHour = 21;
const int dimEndHour = 7;

///////////////////////////////////////////////////////////////////////////////
// General
///////////////////////////////////////////////////////////////////////////////

void heartbeat()
{
  static unsigned long start = 0;
  static bool state{false};

  if (millis() - start > heartbeatRateMs)
  {
    start = millis();
    state = !state;
    digitalWrite(PIN_NEOPIXEL, state);
  }
}

bool checkForDailyOpen(Quote &quote, unsigned long long timestamp)
{
   time_t ts;
    if (timestamp > 2000000000UL)
      // timestamp in milliseconds
        ts = static_cast<time_t>(timestamp / 1000UL);
    else
        ts = static_cast<time_t>(timestamp);

  struct tm t;
  gmtime_r(&ts, &t);

  int utcHour = t.tm_hour;
  int utcYday = t.tm_yday;

  // 6:00 AM EST = 11:00 UTC (no DST)
  const int triggerUTC_HOUR = 0;//11; //TEMP

  // TEMP
  Serial.println(timestamp);
  Serial.println(triggerUTC_HOUR);
  Serial.println(utcHour);
  Serial.println(utcYday);
  Serial.println(quote.lastTriggerDay);
  Serial.println("------------");

  if (utcYday == quote.lastTriggerDay)
    return false;

  if (utcHour >= triggerUTC_HOUR)
  {
    quote.lastTriggerDay = utcYday;
    quote.yesterdayClose = quote.currentPrice;
    return true;
  }

  quote.timestamp = timestamp;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Display Methods
///////////////////////////////////////////////////////////////////////////////

void initDisplay()
{
  ledcSetup(pwmChannel, pwmFrequency, pwmResolution);
  ledcAttachPin(pwmPin, pwmChannel);
  ledcWrite(pwmChannel, pwmFullBrightnessDuty);

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
  char buf[24];
  int charWidth, textPixelWidth;

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

    float price = quotes[element].currentPrice;
    float delta = quotes[element].currentPrice - quotes[element].yesterdayClose;
    int color = quotes[element].yesterdayClose == 0 ? ST77XX_WHITE : delta < 0 ? ST77XX_RED
                                                                               : ST77XX_GREEN;

    int largeTextSize = 6;
    int smallTextSize = 3;

    dtostrf(price, 7, 2, buf);
    charWidth = 6 * largeTextSize;
    textPixelWidth = strlen(buf) * charWidth;
    tft.setCursor((tft.width() - textPixelWidth) / 2, 57);
    tft.setTextColor(color, ST77XX_BLACK);
    tft.setTextSize(largeTextSize);
    tft.print(buf);

    if (quotes[element].yesterdayClose != 0)
    {
      dtostrf(delta, 6, 2, buf);
      charWidth = 6 * smallTextSize;
      textPixelWidth = strlen(buf) * charWidth;
      tft.setCursor((tft.width() - textPixelWidth) / 2, 108);
      tft.setTextColor(color, ST77XX_BLACK);
      tft.setTextSize(smallTextSize);
      tft.print(buf);
    }
  }
}

void updateDisplayIndicators()
{
  static bool firstEntry{true};
  static Status prevStatus;
  static unsigned long lastDisplayedMinute = 999;

  int textSize = 2;
  int y = tft.height() - 22;

  if (prevStatus.wifi != status.wifi || firstEntry)
  {
    prevStatus.wifi = status.wifi;
    printIndicator(5, y, textSize, "WiFi", ST77XX_BLACK, status.wifi ? ST77XX_GREEN : ST77XX_RED);
  }

  if (prevStatus.www != status.www || firstEntry)
  {
    prevStatus.www = status.www;
    printIndicator(68, y, textSize, "WWW", ST77XX_BLACK, status.www ? ST77XX_GREEN : ST77XX_RED);
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

  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);
  if (timeinfo)
  {
    if (timeinfo->tm_min != lastDisplayedMinute)
    {
      lastDisplayedMinute = timeinfo->tm_min;
      static char timeString[6];
      snprintf(timeString, sizeof(timeString), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
      printIndicator(250, y, textSize, timeString, ST77XX_BLACK, ST77XX_WHITE);
    }
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
  tft.drawFastHLine(0, tft.height() - 28, tft.width(), ST77XX_BLUE);
}

void updateDisplayBrightness()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return;

  int hour = timeinfo.tm_hour;

  if (hour >= dimStartHour || hour < dimEndHour)
  {
    ledcWrite(pwmChannel, pwmDimBrightnessDuty);
  }
  else
  {
    ledcWrite(pwmChannel, pwmFullBrightnessDuty);    
  }
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
        Serial.println("[API] Response:");
        Serial.println(payload);

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
          status.api = false;
          Serial.print("[API] Error, deserializeJson() failed: ");
          Serial.println(error.f_str());
          return;
        }

        JsonVariant ts = doc[0]["ts"];
        if (ts.isNull())
        {
          Serial.println("[API] Error, 'ts' is null or missing.");
        }
        else
        {
          unsigned long long timestamp = doc[0]["ts"].as<unsigned long long>();
          checkForDailyOpen(quotes.at(element), timestamp);
        }

        JsonVariant bidValue = doc[0]["spreadProfilePrices"][0]["bid"];
        if (bidValue.isNull())
        {
          Serial.println("[API] Error, 'Bid' is null or missing.");
        }
        else
        {
          float bid = bidValue.as<float>();
          quotes.at(element).currentPrice = bid;
        }
      }
      else
      {
        Serial.printf("[API] Error, HTTP code: %d\n", httpCode);
        status.api = false;
      }
    }
    else
    {
      Serial.printf("[API] Error, GET failed: %s\n", https.errorToString(httpCode).c_str());
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

void apiFetchTask(void *pvParameters)
{
  static Element element = Element::AU;

  const TickType_t delayTicks = pdMS_TO_TICKS(apiFetchRateMs);

  while (true)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      element = nextElement(element);

      status.fetch = true;
      fetchData(element);
      status.fetch = false;
    }
    else
    {
      status.fetch = false;
    }

    vTaskDelay(delayTicks);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Ping
///////////////////////////////////////////////////////////////////////////////

void webConnectionTask(void *pvParameters)
{
  const TickType_t delayTicks = pdMS_TO_TICKS(pingRateMs);

  while (true)
  {
    if (Ping.ping("www.google.com"))
    {
      status.www = true;
    }
    else
    {
      status.www = false;
    }

    vTaskDelay(delayTicks);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////

void setup()
{
  pinMode(PIN_NEOPIXEL, OUTPUT);

  delay(2000);
  Serial.begin(115200);

  Serial.println("SpotClock Mini Startup");

  initDisplay();

  connectWifi();

  configTzTime(tz, ntpServer);

  xTaskCreate(webConnectionTask, "Ping", 2048, NULL, 1, NULL);
  xTaskCreate(apiFetchTask, "Api", 8192, NULL, 1, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// Main Loop
///////////////////////////////////////////////////////////////////////////////

void loop()
{
  heartbeat();

  checkWifi();

  updateDisplayQuotes();

  updateDisplayIndicators();

  updateDisplayBrightness();

  delay(25);
}
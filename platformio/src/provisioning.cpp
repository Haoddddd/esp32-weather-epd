/* Setup portal for esp32-weather-epd.
 * Copyright (C) 2026  Haodong Zhao
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <vector>

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "cjk_text.h"
#include "config.h"
#include "icons/icons_196x196.h"
#include "portal_html.h"
#include "provisioning.h"
#include "renderer.h"
#include "settings.h"
#if USE_CJK_TEXT
  #include "cities_zh_CN.h"
#endif

// Text drawn on the display. Text shown on the phone lives in portal_html.h.
#if USE_CJK_TEXT
const char *TXT_SETUP_HINT = "连按两次复位键重新设置";
static const char *TXT_SETUP_TITLE        = "设置天气屏";
static const char *TXT_SETUP_STEP_SCAN    = "1. 用手机相机扫描二维码";
static const char *TXT_SETUP_STEP_PAGE    = "2. 在弹出的页面中完成设置";
static const char *TXT_SETUP_NO_POPUP     = "没有弹出页面？";
static const char *TXT_SETUP_OPEN_URL     = "用浏览器打开 ";
static const char *TXT_SETUP_NETWORK      = "WiFi：";
static const char *TXT_SETUP_PASSWORD     = "密码：";
static const char *TXT_SETUP_TIMEOUT      = "设置已超时";
static const char *TXT_SETUP_TIMEOUT_HINT = "按复位键重新开始";
#else
const char *TXT_SETUP_HINT = "Press reset twice to set up";
static const char *TXT_SETUP_TITLE        = "Setup";
static const char *TXT_SETUP_STEP_SCAN    = "1. Scan the QR code with your phone";
static const char *TXT_SETUP_STEP_PAGE    = "2. Follow the page that opens";
static const char *TXT_SETUP_NO_POPUP     = "No page opened?";
static const char *TXT_SETUP_OPEN_URL     = "Open in a browser: ";
static const char *TXT_SETUP_NETWORK      = "WiFi: ";
static const char *TXT_SETUP_PASSWORD     = "Password: ";
static const char *TXT_SETUP_TIMEOUT      = "Setup Timed Out";
static const char *TXT_SETUP_TIMEOUT_HINT = "Press reset to try again";
#endif

// Two resets within this window enter setup mode.
static const uint64_t DOUBLE_RESET_WINDOW_US = 3000000;
static const char *DRD_NAMESPACE = "drd";
static const char *DRD_KEY_ARMED = "armed";

// Leave setup mode after this long without any request from the phone.
static const unsigned long PORTAL_TIMEOUT_MS = 10 * 60 * 1000;
// How long to wait for the router when testing the entered credentials.
static const unsigned long CONNECT_TIMEOUT_MS = 20000;
// Time for the phone to fetch the result before the device restarts.
static const unsigned long RESTART_DELAY_MS = 5000;

static const byte DNS_PORT = 53;

typedef enum connect_state
{
  CONNECT_IDLE,
  CONNECT_PENDING,
  CONNECT_CONNECTED,
  CONNECT_FAILED
} connect_state_t;

static WebServer server(80);
static DNSServer dnsServer;
static unsigned long lastActivity;
static connect_state_t connectState = CONNECT_IDLE;
static const char *connectFailReason = "";
static unsigned long connectStarted;
static settings_t pendingSettings;
static String networksJson;

static void setDoubleResetArmed(bool armed)
{
  Preferences prefs;
  prefs.begin(DRD_NAMESPACE, false);
  prefs.putBool(DRD_KEY_ARMED, armed);
  prefs.end();
  return;
} // end setDoubleResetArmed

static void disarmDoubleReset(void *)
{
  setDoubleResetArmed(false);
  return;
} // end disarmDoubleReset

/* Returns true if the user pressed reset twice within
 * DOUBLE_RESET_WINDOW_US. Call once, early during boot.
 *
 * Each boot (other than a wake from deep sleep) arms a flag in NVS that a
 * timer clears again after the window. Finding the flag still armed means
 * the previous boot was reset within the window.
 */
bool checkDoubleReset()
{
  Preferences prefs;
  prefs.begin(DRD_NAMESPACE, true);
  const bool armed = prefs.isKey(DRD_KEY_ARMED)
                  && prefs.getBool(DRD_KEY_ARMED, false);
  prefs.end();

  if (esp_reset_reason() == ESP_RST_DEEPSLEEP)
  { // timer wake, not a button press; clear a flag left by a short boot
    if (armed)
    {
      setDoubleResetArmed(false);
    }
    return false;
  }
  if (armed)
  {
    setDoubleResetArmed(false);
    return true;
  }

  setDoubleResetArmed(true);
  const esp_timer_create_args_t args = {
    .callback = disarmDoubleReset,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "drd",
    .skip_unhandled_events = false,
  };
  esp_timer_handle_t timer;
  if (esp_timer_create(&args, &timer) == ESP_OK)
  {
    esp_timer_start_once(timer, DOUBLE_RESET_WINDOW_US);
  }
  return false;
} // end checkDoubleReset

/* Escapes s for use inside a JSON string literal.
 */
static String jsonEscape(const String &s)
{
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); ++i)
  {
    const char c = s[i];
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (static_cast<uint8_t>(c) < 0x20)
    {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    }
    else
    {
      out += c;
    }
  }
  return out;
} // end jsonEscape

static void sendJson(int code, const String &json)
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", json);
  return;
} // end sendJson

/* Replies with an error code; portal_html.h turns it into a message.
 */
static void sendError(const char *code)
{
  sendJson(400, String("{\"error\":\"") + code + "\"}");
  return;
} // end sendError

/* Scans for WiFi networks and caches them as JSON, strongest first, one
 * entry per SSID.
 */
static void scanNetworks()
{
  const int count = WiFi.scanNetworks();
  std::vector<int> order;
  for (int i = 0; i < count; ++i)
  {
    if (WiFi.SSID(i).isEmpty())
    {
      continue;
    }
    bool duplicate = false;
    for (int j : order)
    {
      if (WiFi.SSID(j) == WiFi.SSID(i))
      {
        duplicate = true;
        break;
      }
    }
    if (!duplicate)
    {
      order.push_back(i);
    }
  }
  std::sort(order.begin(), order.end(),
            [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

  networksJson = "[";
  for (size_t k = 0; k < order.size(); ++k)
  {
    const int i = order[k];
    if (k > 0)
    {
      networksJson += ',';
    }
    networksJson += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i))
                  + "\",\"rssi\":" + String(WiFi.RSSI(i))
                  + ",\"secure\":"
                  + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN
                     ? "false" : "true")
                  + "}";
  }
  networksJson += "]";
  WiFi.scanDelete();
  return;
} // end scanNetworks

#if USE_CJK_TEXT
/* Returns the index of the city named shortName, or -1.
 */
static int findCity(const String &shortName)
{
  for (uint16_t i = 0; i < CITY_COUNT; ++i)
  {
    if (shortName == CITIES[i].shortName)
    {
      return i;
    }
  }
  return -1;
} // end findCity
#endif

static void handleRoot()
{
  lastActivity = millis();
  server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
  return;
} // end handleRoot

static void handleState()
{
  lastActivity = millis();
#if USE_CJK_TEXT
  const String cityMode = "list";
  const int cityIndex = findCity(settings.city);
#else
  const String cityMode = "manual";
  const int cityIndex = -1;
#endif
  sendJson(200, "{\"ssid\":\"" + jsonEscape(settings.wifiSsid)
              + "\",\"city\":\"" + jsonEscape(settings.city)
              + "\",\"lat\":\"" + jsonEscape(settings.lat)
              + "\",\"lon\":\"" + jsonEscape(settings.lon)
              + "\",\"cityMode\":\"" + cityMode
              + "\",\"cityIndex\":" + String(cityIndex)
              + ",\"customKey\":"
              + (settings.owmApiKey != OWM_APIKEY ? "true" : "false")
              + "}");
  return;
} // end handleState

static void handleNetworks()
{
  lastActivity = millis();
  if (server.hasArg("refresh") || networksJson.isEmpty())
  {
    scanNetworks();
  }
  sendJson(200, networksJson);
  return;
} // end handleNetworks

#if USE_CJK_TEXT
static void handleCities()
{
  lastActivity = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String chunk = "{\"provinces\":[";
  for (uint8_t i = 0; i < CITY_PROVINCE_COUNT; ++i)
  {
    chunk += (i ? ",\"" : "\"") + String(CITY_PROVINCES[i]) + "\"";
  }
  chunk += "],\"cities\":[";
  for (uint16_t i = 0; i < CITY_COUNT; ++i)
  {
    chunk += (i ? ",[" : "[") + String(CITIES[i].province) + ",\""
           + CITIES[i].name + "\"]";
    if (chunk.length() > 1024)
    {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]}";
  server.sendContent(chunk);
  server.sendContent("");
  return;
} // end handleCities
#endif

#if !USE_CJK_TEXT
/* Returns true if s is a decimal number within [min, max].
 */
static bool isCoordinate(const String &s, float min, float max)
{
  if (s.isEmpty() || s.length() > 12)
  {
    return false;
  }
  char *end;
  const float value = strtof(s.c_str(), &end);
  return *end == '\0' && value >= min && value <= max;
} // end isCoordinate
#endif

/* Validates the submitted form into pendingSettings and starts connecting to
 * the selected network. The result is reported through /api/status.
 */
static void handleSave()
{
  lastActivity = millis();
  if (connectState == CONNECT_PENDING)
  {
    sendError("busy");
    return;
  }
  pendingSettings = settings;

  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  if (ssid.isEmpty() || ssid.length() > 32)
  {
    sendError("ssid");
    return;
  }
  if (!password.isEmpty() && (password.length() < 8 || password.length() > 63))
  {
    sendError("password");
    return;
  }
  pendingSettings.wifiSsid = ssid;
  // an empty password on the current network keeps the stored password, so
  // the city can be changed without retyping it
  if (!(password.isEmpty() && ssid == settings.wifiSsid))
  {
    pendingSettings.wifiPassword = password;
  }

#if USE_CJK_TEXT
  const long city = server.arg("city").toInt();
  if (!server.hasArg("city") || city < 0 || city >= CITY_COUNT)
  {
    sendError("city");
    return;
  }
  pendingSettings.city = CITIES[city].shortName;
  pendingSettings.lat = CITIES[city].lat;
  pendingSettings.lon = CITIES[city].lon;
#else
  const String cityName = server.arg("cityName");
  const String lat = server.arg("lat");
  const String lon = server.arg("lon");
  if (cityName.isEmpty() || cityName.length() > 32
   || !isCoordinate(lat, -90, 90) || !isCoordinate(lon, -180, 180))
  {
    sendError("location");
    return;
  }
  pendingSettings.city = cityName;
  pendingSettings.lat = lat;
  pendingSettings.lon = lon;
#endif

  if (server.hasArg("resetApikey"))
  {
    pendingSettings.owmApiKey = OWM_APIKEY;
  }
  else if (!server.arg("apikey").isEmpty())
  {
    const String key = server.arg("apikey");
    for (unsigned int i = 0; i < key.length(); ++i)
    {
      if (!isAlphaNumeric(key[i]) || key.length() > 64)
      {
        sendError("apikey");
        return;
      }
    }
    pendingSettings.owmApiKey = key;
  }

  Serial.printf("Setup: connecting to '%s'\n",
                pendingSettings.wifiSsid.c_str());
  WiFi.disconnect(false, true);
  WiFi.begin(pendingSettings.wifiSsid.c_str(),
             pendingSettings.wifiPassword.c_str());
  connectState = CONNECT_PENDING;
  connectStarted = millis();
  sendJson(200, "{\"ok\":true}");
  return;
} // end handleSave

static void handleStatus()
{
  lastActivity = millis();
  const char *state = "idle";
  switch (connectState)
  {
    case CONNECT_PENDING:   state = "connecting"; break;
    case CONNECT_CONNECTED: state = "connected";  break;
    case CONNECT_FAILED:    state = "failed";     break;
    default: break;
  }
  sendJson(200, String("{\"state\":\"") + state + "\",\"reason\":\""
              + connectFailReason + "\"}");
  return;
} // end handleStatus

/* Any unknown URL redirects to the setup page. Phones probe URLs like
 * /generate_204 or /hotspot-detect.html; the redirect makes them show the
 * captive portal.
 */
static void handleNotFound()
{
  lastActivity = millis();
  server.sendHeader("Location",
                    "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
  return;
} // end handleNotFound

/* Checks on a pending connection attempt.
 */
static void updateConnectState()
{
  if (connectState != CONNECT_PENDING)
  {
    return;
  }
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED)
  {
    Serial.println("Setup: connected, saving settings");
    settings = pendingSettings;
    saveSettings();
    connectState = CONNECT_CONNECTED;
    connectStarted = millis();
    return;
  }
  if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED
   || millis() - connectStarted > CONNECT_TIMEOUT_MS)
  {
    connectFailReason = status == WL_NO_SSID_AVAIL ? "no_ssid" : "auth";
    Serial.printf("Setup: connection failed (%s)\n", connectFailReason);
    WiFi.disconnect(false, true);
    connectState = CONNECT_FAILED;
  }
  return;
} // end updateConnectState

static void drawSetup(const String &apSsid, const String &apPassword,
                      const String &qrText)
{
  const String lines[] = {
    TXT_SETUP_STEP_SCAN,
    TXT_SETUP_STEP_PAGE,
    String(TXT_SETUP_NETWORK) + apSsid,
    String(TXT_SETUP_PASSWORD) + apPassword,
    TXT_SETUP_NO_POPUP,
    String(TXT_SETUP_OPEN_URL) + WiFi.softAPIP().toString(),
  };
  initDisplay();
  do
  {
    drawSetupScreen(qrText, TXT_SETUP_TITLE, lines,
                    sizeof(lines) / sizeof(lines[0]));
  } while (display.nextPage());
  powerOffDisplay();
  return;
} // end drawSetup

/* Runs the setup portal until the user saves working settings (then restarts)
 * or until PORTAL_TIMEOUT_MS passes without activity (then sleeps until the
 * reset button is pressed). Does not return.
 */
void runSetupPortal()
{
  Serial.println("Entering setup mode");
  WiFi.mode(WIFI_AP_STA);
  // scan before the access point starts, so the first page load is fast
  scanNetworks();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apSsid[20];
  snprintf(apSsid, sizeof(apSsid), "Weather-%02X%02X", mac[4], mac[5]);
  char apPassword[9];
  snprintf(apPassword, sizeof(apPassword), "%08lu",
           static_cast<unsigned long>(esp_random() % 100000000UL));

  WiFi.softAP(apSsid, apPassword);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/networks", HTTP_GET, handleNetworks);
#if USE_CJK_TEXT
  server.on("/api/cities", HTTP_GET, handleCities);
#endif
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("Setup: access point '%s', http://%s/\n", apSsid,
                WiFi.softAPIP().toString().c_str());

  const String qrText = String("WIFI:T:WPA;S:") + apSsid + ";P:" + apPassword
                      + ";;";
  drawSetup(apSsid, apPassword, qrText);

  lastActivity = millis();
  while (true)
  {
    dnsServer.processNextRequest();
    server.handleClient();
    updateConnectState();

    if (connectState == CONNECT_CONNECTED
     && millis() - connectStarted > RESTART_DELAY_MS)
    {
      Serial.println("Setup: done, restarting");
      ESP.restart();
    }
    if (connectState == CONNECT_PENDING)
    {
      lastActivity = millis();
    }
    if (millis() - lastActivity > PORTAL_TIMEOUT_MS)
    {
      Serial.println("Setup: timed out");
      server.stop();
      dnsServer.stop();
      WiFi.mode(WIFI_OFF);
      initDisplay();
      do
      {
        drawError(wifi_x_196x196, TXT_SETUP_TIMEOUT, TXT_SETUP_TIMEOUT_HINT);
      } while (display.nextPage());
      powerOffDisplay();
      // no wake source: only the reset button wakes the device
      esp_deep_sleep_start();
    }
    delay(2);
  }
} // end runSetupPortal

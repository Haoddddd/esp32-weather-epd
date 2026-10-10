/* Runtime settings for esp32-weather-epd.
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

#include <Preferences.h>

#include "config.h"
#include "settings.h"

// NVS keys (max 15 characters)
static const char *KEY_WIFI_SSID     = "wifi_ssid";
static const char *KEY_WIFI_PASSWORD = "wifi_pass";
static const char *KEY_OWM_APIKEY    = "owm_apikey";
static const char *KEY_LAT           = "lat";
static const char *KEY_LON           = "lon";
static const char *KEY_CITY          = "city";

settings_t settings;

/* Returns the stored string for key, or fallback if it has not been stored.
 * Checking first avoids error logs from Preferences for missing keys.
 */
static String getStoredString(Preferences &prefs, const char *key,
                              const String &fallback)
{
  return prefs.isKey(key) ? prefs.getString(key, fallback) : fallback;
} // end getStoredString

/* Loads settings from non-volatile storage into the global settings, using
 * the build-time defaults for anything that has not been stored.
 */
void loadSettings()
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  settings.wifiSsid     = getStoredString(prefs, KEY_WIFI_SSID, WIFI_SSID);
  settings.wifiPassword = getStoredString(prefs, KEY_WIFI_PASSWORD,
                                          WIFI_PASSWORD);
  settings.owmApiKey    = getStoredString(prefs, KEY_OWM_APIKEY, OWM_APIKEY);
  settings.lat          = getStoredString(prefs, KEY_LAT, LAT);
  settings.lon          = getStoredString(prefs, KEY_LON, LON);
  settings.city         = getStoredString(prefs, KEY_CITY, CITY_STRING);
  prefs.end();
  return;
} // end loadSettings

/* Writes the global settings to non-volatile storage.
 */
void saveSettings()
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(KEY_WIFI_SSID, settings.wifiSsid);
  prefs.putString(KEY_WIFI_PASSWORD, settings.wifiPassword);
  // only store an API key that differs from the build-time default, so
  // devices pick up a new default key with a firmware update
  if (settings.owmApiKey == OWM_APIKEY)
  {
    prefs.remove(KEY_OWM_APIKEY);
  }
  else
  {
    prefs.putString(KEY_OWM_APIKEY, settings.owmApiKey);
  }
  prefs.putString(KEY_LAT, settings.lat);
  prefs.putString(KEY_LON, settings.lon);
  prefs.putString(KEY_CITY, settings.city);
  prefs.end();
  return;
} // end saveSettings

/* Removes all stored settings, so the build-time defaults apply again.
 */
void clearSettings()
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove(KEY_WIFI_SSID);
  prefs.remove(KEY_WIFI_PASSWORD);
  prefs.remove(KEY_OWM_APIKEY);
  prefs.remove(KEY_LAT);
  prefs.remove(KEY_LON);
  prefs.remove(KEY_CITY);
  prefs.end();
  return;
} // end clearSettings

/* Returns true if a WiFi network has been configured.
 */
bool isWiFiConfigured()
{
  return !settings.wifiSsid.isEmpty();
} // end isWiFiConfigured

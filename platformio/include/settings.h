/* Runtime settings declarations for esp32-weather-epd.
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

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <Arduino.h>

// Settings that can change at runtime (e.g. through the setup portal). They
// are kept in non-volatile storage; any field that has not been stored falls
// back to the build-time default from config.cpp.
typedef struct settings
{
  String wifiSsid;
  String wifiPassword;
  String owmApiKey;
  String lat;
  String lon;
  String city;
} settings_t;

extern settings_t settings;

void loadSettings();
void saveSettings();
void clearSettings();
bool isWiFiConfigured();

#endif

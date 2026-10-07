/* CJK text rendering declarations for esp32-weather-epd.
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

#ifndef __CJK_TEXT_H__
#define __CJK_TEXT_H__

#include <cstdint>
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include "config.h"
#include "fonts/cjk/CjkFont.h"

// Locales whose strings contain CJK characters. USE_CJK_TEXT expands to 1 when
// the selected LOCALE is listed here. For any other locale it expands to an
// undefined identifier, which the preprocessor evaluates as 0, so no CJK code
// or font data is compiled in.
#define LOCALE_USES_CJK_zh_CN 1
#define CJK_XCAT(a, b) a ## b
#define CJK_CAT(a, b)  CJK_XCAT(a, b)
#define USE_CJK_TEXT   CJK_CAT(LOCALE_USES_CJK_, LOCALE)

uint8_t utf8CharLength(const String &text, unsigned int index);
bool containsCjk(const String &text);
uint16_t getCjkStringWidth(const String &text, const GFXfont *latinFont,
                           const CjkFont *cjkFont);
void drawCjkString(Adafruit_GFX &gfx, int16_t x, int16_t y,
                   const String &text, const GFXfont *latinFont,
                   const CjkFont *cjkFont, uint16_t color);

#endif

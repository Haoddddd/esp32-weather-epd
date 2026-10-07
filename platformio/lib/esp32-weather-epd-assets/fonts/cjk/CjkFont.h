/* CJK bitmap font data structures for esp32-weather-epd.
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

#ifndef __FONTS_CJK_CJKFONT_H__
#define __FONTS_CJK_CJKFONT_H__

#include <cstdint>
#include <Arduino.h>

// Same bitmap layout as Adafruit GFXglyph (rows packed MSB-first without
// per-row padding), but addressed by Unicode code point instead of by an
// 8-bit character index.
typedef struct
{
  uint32_t bitmapOffset; // offset into CjkFont.bitmap
  uint16_t codepoint;    // Unicode code point (BMP only)
  uint8_t  width;        // bitmap dimensions in pixels
  uint8_t  height;
  uint8_t  xAdvance;     // distance to advance cursor (x axis)
  int8_t   xOffset;      // x distance from cursor position to UL corner
  int8_t   yOffset;      // y distance from cursor position to UL corner
} CjkGlyph;

typedef struct
{
  const uint8_t  *bitmap;     // concatenated glyph bitmaps
  const CjkGlyph *glyph;      // glyph array, sorted by code point
  uint16_t        glyphCount;
  uint8_t         yAdvance;   // newline distance (y axis)
} CjkFont;

#endif

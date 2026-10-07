/* CJK text rendering for esp32-weather-epd.
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

#include "cjk_text.h"

#if USE_CJK_TEXT

/* Decodes the character that starts at text[index].
 *
 * Valid UTF-8 multi-byte sequences yield their code point. Any other byte is
 * treated as a single ISO-8859-1 character, so Latin-1 escapes already used by
 * the locale files (e.g. "\260C") keep working in mixed strings.
 *
 * Returns the number of bytes consumed.
 */
static uint8_t decodeChar(const String &text, unsigned int index,
                          uint32_t &codepoint)
{
  const uint8_t lead = text[index];
  uint8_t len = 0;
  if (lead >= 0xC2 && lead <= 0xDF)
  {
    len = 2;
    codepoint = lead & 0x1F;
  }
  else if (lead >= 0xE0 && lead <= 0xEF)
  {
    len = 3;
    codepoint = lead & 0x0F;
  }
  else if (lead >= 0xF0 && lead <= 0xF4)
  {
    len = 4;
    codepoint = lead & 0x07;
  }

  if (len == 0 || index + len > text.length())
  {
    codepoint = lead;
    return 1;
  }
  for (uint8_t i = 1; i < len; ++i)
  {
    const uint8_t cont = text[index + i];
    if ((cont & 0xC0) != 0x80)
    {
      codepoint = lead;
      return 1;
    }
    codepoint = (codepoint << 6) | (cont & 0x3F);
  }
  return len;
} // end decodeChar

/* Returns the glyph for codepoint, or nullptr if the font does not have it.
 */
static const CjkGlyph *findCjkGlyph(const CjkFont *font, uint32_t codepoint)
{
  int lo = 0;
  int hi = static_cast<int>(font->glyphCount) - 1;
  while (lo <= hi)
  {
    const int mid = (lo + hi) / 2;
    const uint16_t cp = font->glyph[mid].codepoint;
    if (cp == codepoint)
    {
      return &font->glyph[mid];
    }
    if (cp < codepoint)
    {
      lo = mid + 1;
    }
    else
    {
      hi = mid - 1;
    }
  }
  return nullptr;
} // end findCjkGlyph

/* Returns the horizontal advance of a single character in pixels.
 */
static uint8_t getCharAdvance(uint32_t codepoint, const GFXfont *latinFont,
                              const CjkFont *cjkFont)
{
  if (codepoint <= 0xFF)
  {
    if (latinFont == nullptr
     || codepoint < latinFont->first || codepoint > latinFont->last)
    {
      return 0;
    }
    return latinFont->glyph[codepoint - latinFont->first].xAdvance;
  }
  const CjkGlyph *glyph = findCjkGlyph(cjkFont, codepoint);
  // missing glyphs are left blank, but still take up space
  return glyph ? glyph->xAdvance : cjkFont->yAdvance / 2;
} // end getCharAdvance

/* Returns the number of bytes of the character that starts at text[index].
 */
uint8_t utf8CharLength(const String &text, unsigned int index)
{
  uint32_t codepoint;
  return decodeChar(text, index, codepoint);
} // end utf8CharLength

/* Returns true if text contains any character outside of ISO-8859-1, which
 * cannot be drawn with the Adafruit GFX fonts alone.
 */
bool containsCjk(const String &text)
{
  unsigned int i = 0;
  while (i < text.length())
  {
    uint32_t codepoint;
    i += decodeChar(text, i, codepoint);
    if (codepoint > 0xFF)
    {
      return true;
    }
  }
  return false;
} // end containsCjk

/* Returns the width of a mixed Latin/CJK string in pixels.
 */
uint16_t getCjkStringWidth(const String &text, const GFXfont *latinFont,
                           const CjkFont *cjkFont)
{
  uint16_t width = 0;
  unsigned int i = 0;
  while (i < text.length())
  {
    uint32_t codepoint;
    i += decodeChar(text, i, codepoint);
    width += getCharAdvance(codepoint, latinFont, cjkFont);
  }
  return width;
} // end getCjkStringWidth

/* Draws a mixed Latin/CJK string with its baseline at y. ISO-8859-1
 * characters are drawn with latinFont (which must be the font currently set on
 * gfx), all other characters with cjkFont. Leaves the cursor at the end of the
 * string, like print() does.
 */
void drawCjkString(Adafruit_GFX &gfx, int16_t x, int16_t y,
                   const String &text, const GFXfont *latinFont,
                   const CjkFont *cjkFont, uint16_t color)
{
  unsigned int i = 0;
  while (i < text.length())
  {
    uint32_t codepoint;
    i += decodeChar(text, i, codepoint);
    if (codepoint <= 0xFF)
    {
      gfx.drawChar(x, y, static_cast<unsigned char>(codepoint),
                   color, color, 1);
    }
    else
    {
      const CjkGlyph *glyph = findCjkGlyph(cjkFont, codepoint);
      if (glyph != nullptr)
      {
        const uint8_t *bitmap = cjkFont->bitmap + glyph->bitmapOffset;
        uint8_t bits = 0;
        uint16_t bit = 0;
        gfx.startWrite();
        for (uint8_t yy = 0; yy < glyph->height; ++yy)
        {
          for (uint8_t xx = 0; xx < glyph->width; ++xx)
          {
            if (!(bit++ & 7))
            {
              bits = *bitmap++;
            }
            if (bits & 0x80)
            {
              gfx.writePixel(x + glyph->xOffset + xx,
                             y + glyph->yOffset + yy, color);
            }
            bits <<= 1;
          }
        }
        gfx.endWrite();
      }
    }
    x += getCharAdvance(codepoint, latinFont, cjkFont);
  }
  gfx.setCursor(x, y);
  return;
} // end drawCjkString

#endif // USE_CJK_TEXT

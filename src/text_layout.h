#pragma once
// Portable text layout for the LED matrix: UTF-8 decoding, the proportional
// 5x7 font (large) and the 3x5 font (small). No Arduino dependencies, so the
// native tests in tests/firmware-tests.cpp can render and measure texts.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "font5x7.h"

namespace textlayout {

constexpr uint8_t FONT_LARGE = 0;  // 5x7, upper and lower case, umlauts
constexpr uint8_t FONT_SMALL = 1;  // 3x5, capitals only (lower case is mapped)
constexpr uint8_t LARGE_HEIGHT = 8;
constexpr uint8_t SMALL_HEIGHT = 5;

// Decodes the next UTF-8 code point and advances `pos`. Invalid bytes become '?'.
inline uint32_t nextCodepoint(const char *text, size_t length, size_t &pos) {
  const uint8_t c = static_cast<uint8_t>(text[pos++]);
  if (c < 0x80) return c;
  uint8_t extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
  else return '?';
  for (uint8_t i = 0; i < extra; i++) {
    if (pos >= length || (static_cast<uint8_t>(text[pos]) & 0xC0) != 0x80) return '?';
    cp = (cp << 6) | (static_cast<uint8_t>(text[pos++]) & 0x3F);
  }
  return cp;
}

// Returns the five font columns for a code point, or nullptr if unknown.
inline const uint8_t *largeColumns(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return FONT5X7_ASCII[cp - 32];
  for (const Font5x7Extra &extra : FONT5X7_EXTRA) {
    if (extra.codepoint == cp) return extra.cols;
  }
  return nullptr;
}

// Typographic replacements for characters the fonts do not contain.
inline uint32_t substitute(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x00B4: case 0x0060: return '\'';
    case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return '.';
    case 0x00A0: case 0x2009: case 0x202F: return ' ';
    case 0x00C0: case 0x00C1: case 0x00C2: return 'A';
    case 0x00C8: case 0x00CA: return 'E';
    case 0x00D3: case 0x00D4: return 'O';
    case 0x00F3: case 0x00F2: return 'o';
    case 0x00ED: case 0x00EC: case 0x00EE: return 'i';
    case 0x00FA: case 0x00F9: case 0x00FB: return 'u';
    default: return cp;
  }
}

// Width of one glyph in the proportional large font (empty side columns are
// trimmed; a space is 3 px wide).
inline uint8_t largeGlyph(uint32_t cp, const uint8_t *&cols, uint8_t &first) {
  cp = substitute(cp);
  if (cp == ' ') { cols = nullptr; first = 0; return 3; }
  cols = largeColumns(cp);
  if (!cols) cols = largeColumns('?');
  uint8_t start = 0, end = 4;
  while (start < 5 && cols[start] == 0) start++;
  if (start == 5) { first = 0; return 3; }
  while (end > start && cols[end] == 0) end--;
  first = start;
  return end - start + 1;
}

// 3x5 glyph rows ("111" per row, five rows), shared with the clock pages.
inline const char *smallGlyph(char c) {
  switch (c) {
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001001010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case ':': return "000010000010000";
    case '.': return "000000000000010";
    case ',': return "000000000010100";
    case '-': return "000000111000000";
    case '+': return "000010111010000";
    case '/': return "001001010100100";
    case '!': return "010010010000010";
    case '?': return "111001011000010";
    case '\'': return "010010000000000";
    case '%': return "101001010100101";
    case '(': return "010100100100010";
    case ')': return "010001001001010";
    case '=': return "000111000111000";
    case '*': return "000101010101000";
    case '#': return "101111101111101";
    case 'A': return "010101111101101";
    case 'B': return "110101110101110";
    case 'C': return "111100100100111";
    case 'D': return "110101101101110";
    case 'E': return "111100110100111";
    case 'F': return "111100110100100";
    case 'G': return "111100101101111";
    case 'H': return "101101111101101";
    case 'I': return "111010010010111";
    case 'J': return "001001001101111";
    case 'K': return "101101110101101";
    case 'L': return "100100100100111";
    case 'M': return "101111111101101";
    case 'N': return "101111111111101";
    case 'O': return "111101101101111";
    case 'P': return "110101110100100";
    case 'Q': return "111101101111011";
    case 'R': return "110101110101101";
    case 'S': return "111100111001111";
    case 'T': return "111010010010010";
    case 'U': return "101101101101111";
    case 'V': return "101101101101010";
    case 'W': return "101101111111101";
    case 'X': return "101101010101101";
    case 'Y': return "101101010010010";
    case 'Z': return "111001010100111";
    case '\x01': return "010101010000000";  // degree sign
    default: return nullptr;
  }
}

// Maps a code point to the 3x5 font: lower case to capitals, umlauts to the
// base letter (the 5 px font has no room for dots).
inline char smallChar(uint32_t cp) {
  cp = substitute(cp);
  if (cp >= 'a' && cp <= 'z') return static_cast<char>(cp - 32);
  switch (cp) {
    case 0x00C4: case 0x00E4: case 0x00E0: case 0x00E1: case 0x00E2: return 'A';
    case 0x00D6: case 0x00F6: case 0x00F4: return 'O';
    case 0x00DC: case 0x00FC: return 'U';
    case 0x00DF: return 'S';
    case 0x00E9: case 0x00E8: case 0x00EA: case 0x00C9: return 'E';
    case 0x00E7: return 'C';
    case 0x00F1: return 'N';
    case 0x00B0: return '\x01';
    case 0x00B7: case 0x2022: return '-';
    default: break;
  }
  if (cp < 128 && smallGlyph(static_cast<char>(cp))) return static_cast<char>(cp);
  if (cp == ' ') return ' ';
  return '?';
}

inline uint8_t smallAdvance(char c) {
  if (c == ' ') return 2;
  if (c == ':' || c == '.' || c == '!' || c == '\'') return 2;
  if (c == ',') return 3;
  return smallGlyph(c) ? 4 : 0;
}

// Column offset where narrow punctuation starts inside its 3 px cell.
inline uint8_t smallOffset(char c) {
  return (c == ':' || c == '.' || c == '!' || c == '\'') ? 1 : 0;
}

// Pixel width of a text (without trailing spacing).
inline int textWidth(const char *text, uint8_t font) {
  const size_t length = strlen(text);
  int width = 0;
  size_t pos = 0;
  bool any = false;
  while (pos < length) {
    const uint32_t cp = nextCodepoint(text, length, pos);
    if (font == FONT_SMALL) {
      const char c = smallChar(cp);
      width += smallAdvance(c);
    } else {
      const uint8_t *cols;
      uint8_t first;
      width += largeGlyph(cp, cols, first) + 1;
    }
    any = true;
  }
  return any ? width - 1 : 0;
}

// Draws text with its left edge at x. `plot(x, y)` is called for every lit
// pixel; clipping is the caller's job. Returns the x after the last glyph.
template <class Plot>
int drawText(const char *text, int x, int y, uint8_t font, Plot plot) {
  const size_t length = strlen(text);
  size_t pos = 0;
  while (pos < length) {
    const uint32_t cp = nextCodepoint(text, length, pos);
    if (font == FONT_SMALL) {
      const char c = smallChar(cp);
      const char *rows = smallGlyph(c);
      if (rows) {
        const int dx = -static_cast<int>(smallOffset(c));
        for (uint8_t row = 0; row < 5; row++)
          for (uint8_t col = 0; col < 3; col++)
            if (rows[row * 3 + col] == '1') plot(x + col + dx, y + row);
      }
      x += smallAdvance(c);
    } else {
      const uint8_t *cols;
      uint8_t first;
      const uint8_t width = largeGlyph(cp, cols, first);
      if (cols) {
        for (uint8_t col = 0; col < width; col++) {
          const uint8_t bits = cols[first + col];
          for (uint8_t row = 0; row < 8; row++)
            if (bits & (1 << row)) plot(x + col, y + row);
        }
      }
      x += width + 1;
    }
  }
  return x;
}

}  // namespace textlayout

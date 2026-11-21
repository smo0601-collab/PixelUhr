#pragma once
// Streaming extractor for news headlines from RSS 2.0 (<item><title>) and Atom
// (<entry><title>) feeds. Characters are fed one by one, so a feed never has to
// fit into RAM. No Arduino dependencies; covered by the native tests.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

class RssTitleParser {
 public:
  static constexpr size_t MAX_TITLE = 180;

  // Returns true when a complete headline is available via title().
  bool feed(char c) {
    if (cdata_) {
      append(c);
      if (titleLength_ >= 3 && memcmp(title_ + titleLength_ - 3, "]]>", 3) == 0) {
        titleLength_ -= 3;
        cdata_ = false;
      }
      return false;
    }
    if (inTag_) {
      if (inTitle_ && tagLength_ == 8 && memcmp(tag_, "![CDATA[", 8) == 0) {
        inTag_ = false;
        cdata_ = true;
        append(c);
        return false;
      }
      if (c == '>') {
        inTag_ = false;
        tag_[tagLength_] = 0;
        return closeTag();
      }
      if (tagLength_ < sizeof(tag_) - 1) tag_[tagLength_++] = c;
      return false;
    }
    if (c == '<') {
      inTag_ = true;
      tagLength_ = 0;
      return false;
    }
    if (inTitle_) append(c);
    return false;
  }

  const char *title() const { return output_; }

 private:
  static bool startsWith(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
  }

  static bool nameIs(const char *tag, const char *name) {
    const size_t n = strlen(name);
    return strncmp(tag, name, n) == 0 && (tag[n] == 0 || tag[n] == ' ' || tag[n] == '/' || tag[n] == '\t' ||
                                          tag[n] == '\r' || tag[n] == '\n');
  }

  bool closeTag() {
    if (nameIs(tag_, "item") || nameIs(tag_, "entry")) {
      inItem_ = true;
      return false;
    }
    if (nameIs(tag_, "/item") || nameIs(tag_, "/entry")) {
      inItem_ = false;
      inTitle_ = false;
      return false;
    }
    if (inItem_ && nameIs(tag_, "title") && tag_[tagLength_ - 1] != '/') {
      inTitle_ = true;
      titleLength_ = 0;
      return false;
    }
    if (inTitle_ && nameIs(tag_, "/title")) {
      inTitle_ = false;
      finish();
      return output_[0] != 0;
    }
    return false;
  }

  void append(char c) {
    if (titleLength_ < sizeof(title_) - 1) title_[titleLength_++] = c;
  }

  static void appendUtf8(char *out, size_t &length, size_t max, uint32_t cp) {
    char buffer[4];
    size_t n = 0;
    if (cp < 0x80) buffer[n++] = static_cast<char>(cp);
    else if (cp < 0x800) {
      buffer[n++] = static_cast<char>(0xC0 | (cp >> 6));
      buffer[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      buffer[n++] = static_cast<char>(0xE0 | (cp >> 12));
      buffer[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      buffer[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      buffer[n++] = static_cast<char>(0xF0 | (cp >> 18));
      buffer[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      buffer[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      buffer[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    if (length + n >= max) return;
    memcpy(out + length, buffer, n);
    length += n;
  }

  // Decodes entities, drops embedded markup and collapses white space.
  void finish() {
    size_t length = 0;
    bool space = true;
    bool markup = false;
    for (size_t i = 0; i < titleLength_; i++) {
      char c = title_[i];
      uint32_t cp = static_cast<uint8_t>(c);
      if (c == '&') {
        const char *end = static_cast<const char *>(memchr(title_ + i, ';', titleLength_ - i > 10 ? 10 : titleLength_ - i));
        if (end) {
          const char *entity = title_ + i + 1;
          const size_t n = static_cast<size_t>(end - entity);
          cp = 0;
          if (n >= 2 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            for (size_t k = hex ? 2 : 1; k < n; k++) {
              const char d = entity[k];
              int v = (d >= '0' && d <= '9') ? d - '0' : (hex && d >= 'a' && d <= 'f') ? d - 'a' + 10 :
                      (hex && d >= 'A' && d <= 'F') ? d - 'A' + 10 : -1;
              if (v < 0) { cp = '?'; break; }
              cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(v);
            }
          } else if (n == 3 && strncmp(entity, "amp", 3) == 0) cp = '&';
          else if (n == 2 && strncmp(entity, "lt", 2) == 0) cp = '<';
          else if (n == 2 && strncmp(entity, "gt", 2) == 0) cp = '>';
          else if (n == 4 && strncmp(entity, "quot", 4) == 0) cp = '"';
          else if (n == 4 && strncmp(entity, "apos", 4) == 0) cp = '\'';
          else if (n == 4 && strncmp(entity, "nbsp", 4) == 0) cp = ' ';
          else cp = '?';
          i += n + 1;
          if (cp == '<') { markup = true; continue; }
          if (cp == '>') { markup = false; continue; }
          if (markup) continue;
          if (cp == ' ') {
            if (!space && length < sizeof(output_) - 1) { output_[length++] = ' '; space = true; }
            continue;
          }
          appendUtf8(output_, length, sizeof(output_), cp);
          space = false;
          continue;
        }
      }
      if (c == '<') { markup = true; continue; }
      if (c == '>' && markup) { markup = false; continue; }
      if (markup) continue;
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        if (!space && length < sizeof(output_) - 1) { output_[length++] = ' '; space = true; }
        continue;
      }
      if (length < sizeof(output_) - 1) output_[length++] = c;
      space = false;
    }
    while (length && output_[length - 1] == ' ') length--;
    // Never cut a UTF-8 sequence in half.
    while (length && (static_cast<uint8_t>(output_[length - 1]) & 0xC0) == 0x80) {
      size_t start = length - 1;
      while (start && (static_cast<uint8_t>(output_[start]) & 0xC0) == 0x80) start--;
      const uint8_t lead = static_cast<uint8_t>(output_[start]);
      const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      if (length - start >= need) break;
      length = start;
    }
    output_[length] = 0;
  }

  char tag_[16] = {};
  size_t tagLength_ = 0;
  char title_[MAX_TITLE + 64] = {};
  size_t titleLength_ = 0;
  char output_[MAX_TITLE + 1] = {};
  bool inTag_ = false, inItem_ = false, inTitle_ = false, cdata_ = false;
};

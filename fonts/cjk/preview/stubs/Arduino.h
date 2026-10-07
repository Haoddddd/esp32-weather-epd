// Minimal host-side stand-in for the Arduino core, just enough to compile
// cjk_text.cpp and config.h for the CJK font preview.
#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>

#define PROGMEM

class String
{
public:
  String(const char *s = "") : s_(s) {}
  String(const std::string &s) : s_(s) {}
  unsigned int length() const { return s_.size(); }
  char operator[](unsigned int i) const { return s_[i]; }
  bool isEmpty() const { return s_.empty(); }
  String substring(unsigned int from, unsigned int to) const
  {
    return s_.substr(from, to - from);
  }
  void remove(unsigned int index) { s_.erase(index); }
  String operator+(const char *rhs) const { return s_ + rhs; }
  String &operator+=(const char *rhs)
  {
    s_ += rhs;
    return *this;
  }
  const char *c_str() const { return s_.c_str(); }

private:
  std::string s_;
};

// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Eigene Zeichenkettenklasse mit dem Funktionsumfang, den dieses Projekt nutzt (angelehnt an die gängige Arduino-"String"-Schnittstelle,
// aber reiner Eigencode auf std::string). Damit entfällt die Arduino-ESP32-Komponente. ArduinoJson erkennt die Klasse über
// ARDUINOJSON_ENABLE_ARDUINO_STRING (Writer: concat(), Reader: c_str()/length()).
#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <type_traits>

class String {
 public:
  String() {}
  String(const char* s) { if (s) s_ = s; }
  String(const String& o) : s_(o.s_) {}
  String(String&& o) noexcept : s_(std::move(o.s_)) {}
  String(const std::string& o) : s_(o) {}
  String(char c) { s_.assign(1, c); }
  String(int v, unsigned char base = 10) { zahl((long long)v, base); }
  String(unsigned int v, unsigned char base = 10) { zahl((unsigned long long)v, base); }
  String(long v, unsigned char base = 10) { zahl((long long)v, base); }
  String(unsigned long v, unsigned char base = 10) { zahl((unsigned long long)v, base); }
  String(long long v, unsigned char base = 10) { zahl(v, base); }
  String(unsigned long long v, unsigned char base = 10) { zahl(v, base); }
  String(float v, unsigned char dez = 2) { gleit((double)v, dez); }
  String(double v, unsigned char dez = 2) { gleit(v, dez); }

  String& operator=(const String& o) { s_ = o.s_; return *this; }
  String& operator=(String&& o) noexcept { s_ = std::move(o.s_); return *this; }
  String& operator=(const char* s) { s_ = s ? s : ""; return *this; }
  String& operator=(char c) { s_.assign(1, c); return *this; }

  // Anhängen: concat() liefert wie üblich bool (true = ok), += die Zeichenkette selbst
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(const char* s) { if (s) s_ += s; return true; }
  bool concat(const char* s, size_t n) { if (s) s_.append(s, n); return true; }
  bool concat(char c) { s_ += c; return true; }
  bool concat(int v) { s_ += String(v).s_; return true; }
  bool concat(unsigned int v) { s_ += String(v).s_; return true; }
  bool concat(long v) { s_ += String(v).s_; return true; }
  bool concat(unsigned long v) { s_ += String(v).s_; return true; }
  bool concat(float v) { s_ += String(v).s_; return true; }
  bool concat(double v) { s_ += String(v).s_; return true; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* s) { if (s) s_ += s; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(int v) { concat(v); return *this; }
  String& operator+=(unsigned int v) { concat(v); return *this; }
  String& operator+=(long v) { concat(v); return *this; }
  String& operator+=(unsigned long v) { concat(v); return *this; }
  String& operator+=(float v) { concat(v); return *this; }
  String& operator+=(double v) { concat(v); return *this; }

  size_t length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(size_t n) { s_.reserve(n); }
  char charAt(size_t i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](size_t i) const { return i < s_.size() ? s_[i] : 0; }
  char& operator[](size_t i) { return s_[i]; }
  void setCharAt(size_t i, char c) { if (i < s_.size()) s_[i] = c; }

  bool equals(const String& o) const { return s_ == o.s_; }
  bool equals(const char* s) const { return s && s_ == s; }
  bool equalsIgnoreCase(const String& o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++) if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  int compareTo(const String& o) const { return s_.compare(o.s_); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* s) const { return s && s_ == s; }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* s) const { return !s || s_ != s; }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool operator>(const String& o) const { return s_ > o.s_; }
  bool operator<=(const String& o) const { return s_ <= o.s_; }
  bool operator>=(const String& o) const { return s_ >= o.s_; }

  bool startsWith(const String& p, size_t von = 0) const { return s_.size() >= von + p.s_.size() && s_.compare(von, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String& p) const { return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0; }
  int indexOf(char c, size_t von = 0) const { size_t p = s_.find(c, von); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t, size_t von = 0) const { size_t p = s_.find(t.s_, von); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(const String& t) const { size_t p = s_.rfind(t.s_); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t von) const { return von >= s_.size() ? String() : String(s_.substr(von)); }
  String substring(size_t von, size_t bis) const {
    if (von > bis) { size_t t = von; von = bis; bis = t; }
    if (von >= s_.size()) return String();
    return String(s_.substr(von, bis - von));
  }
  void replace(char a, char b) { for (auto& c : s_) if (c == a) c = b; }
  void replace(const String& a, const String& b) {
    if (a.s_.empty()) return;
    size_t pos = 0;
    while ((pos = s_.find(a.s_, pos)) != std::string::npos) { s_.replace(pos, a.s_.size(), b.s_); pos += b.s_.size(); }
  }
  void remove(size_t idx, size_t n = (size_t)-1) { if (idx < s_.size()) s_.erase(idx, n); }
  void trim() {
    size_t a = 0, b = s_.size();
    while (a < b && isspace((unsigned char)s_[a])) a++;
    while (b > a && isspace((unsigned char)s_[b - 1])) b--;
    s_ = s_.substr(a, b - a);
  }
  void toLowerCase() { for (auto& c : s_) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (auto& c : s_) c = (char)toupper((unsigned char)c); }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  double toDouble() const { return strtod(s_.c_str(), nullptr); }
  const std::string& str() const { return s_; }

 private:
  void zahl(long long v, unsigned char base) { char b[40]; if (base == 16) snprintf(b, sizeof(b), "%llx", (unsigned long long)v); else snprintf(b, sizeof(b), "%lld", v); s_ = b; }
  void zahl(unsigned long long v, unsigned char base) { char b[40]; if (base == 16) snprintf(b, sizeof(b), "%llx", v); else snprintf(b, sizeof(b), "%llu", v); s_ = b; }
  void gleit(double v, unsigned char dez) { char b[64]; snprintf(b, sizeof(b), "%.*f", (int)dez, v); s_ = b; }
  std::string s_;
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, char b) { String r(a); r += b; return r; }
inline String operator+(char a, const String& b) { String r(a); r += b; return r; }
template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value>::type>
inline String operator+(const String& a, T b) { String r(a); r += String(b); return r; }
template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, char>::value>::type>
inline String operator+(T a, const String& b) { String r(a); r += b; return r; }
inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const char* a, const String& b) { return b != a; }

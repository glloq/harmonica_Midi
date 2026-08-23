#pragma once
#include "Arduino.h"
struct File {
  operator bool() const { return false; }
  String readString() { return String(); }
  void   close() {}
  void   print(const char*) {}
  bool   isDirectory() { return false; }
  File   openNextFile() { return File(); }
  String name() { return String(); }
};
struct FsStub {
  bool begin(bool = false) { return true; }
  bool exists(const char*) { return false; }
  File open(const char*, const char* = "r") { return File(); }
  bool remove(const char*) { return true; }
  bool rename(const char*, const char*) { return true; }
};
inline FsStub LittleFS;

#pragma once
// PC stand-in for the bits of Arduino the scene code uses (tools/preview.py).
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <type_traits>

#ifndef PI
#define PI 3.14159265358979f
#endif
#define DEG_TO_RAD 0.017453292519943295f

uint32_t millis();
uint32_t esp_random();

template <class A, class B>
inline typename std::common_type<A, B>::type min(A a, B b) { return a < b ? a : b; }
template <class A, class B>
inline typename std::common_type<A, B>::type max(A a, B b) { return a > b ? a : b; }
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

struct FakeSerial {
  void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
  }
  void println(const char *s = "") { fprintf(stderr, "%s\n", s); }
  size_t write(const uint8_t *, size_t n) { return n; }
  void flush() {}
};
extern FakeSerial Serial;

struct FakeESP {
  uint32_t getFreeHeap() { return 200000; }
};
extern FakeESP ESP;

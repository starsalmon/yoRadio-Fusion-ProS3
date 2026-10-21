#pragma once
// B3 diagnostics: cheap enter/exit markers with ms timestamps (YORADIO_STREAM_DIAG only).
#ifdef YORADIO_STREAM_DIAG
#include <Arduino.h>
struct B3Mark {
  const char* n; uint32_t t0;
  B3Mark(const char* name) : n(name), t0(millis()) { printf("[B3] %lu > %s\n", (unsigned long)t0, n); }
  ~B3Mark() { uint32_t t = millis(); printf("[B3] %lu < %s (%lu ms)\n", (unsigned long)t, n, (unsigned long)(t - t0)); }
};
#define B3MARK(name) B3Mark _b3m(name)
#else
#define B3MARK(name) do {} while (0)
#endif

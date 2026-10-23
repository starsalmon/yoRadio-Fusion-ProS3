// mbedTLS in this Arduino core is built with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, so TLS
// buffers (~16 KB each) must come from internal RAM. With audio + WiFi + BT running, internal
// heap is ~50 KB and fragmented, so every HTTPS podcast fetch failed with -32512
// ("SSL - Memory allocation failed"). Linked with -Wl,--wrap=esp_mbedtls_mem_calloc:
// try internal RAM first (unchanged behaviour), fall back to PSRAM only if that fails.
#include <Arduino.h>
#include <esp_heap_caps.h>
extern "C" void* __real_esp_mbedtls_mem_calloc(size_t n, size_t size);
extern "C" void* __wrap_esp_mbedtls_mem_calloc(size_t n, size_t size) {
  void* p = __real_esp_mbedtls_mem_calloc(n, size);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;  // freed by esp_mbedtls_mem_free -> heap_caps_free (handles both regions)
}

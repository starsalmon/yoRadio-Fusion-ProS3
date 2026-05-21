#pragma once
#include <stdint.h>
#include <stddef.h>

// Persistent resume for podcast episodes (keyed by enclosure URL).
//
// Stored on SPIFFS so it survives reboot without changing EEPROM layout.
// Implementation keeps a bounded "last N" cache (default 10).

// Return true and set outSec if a resume position exists.
bool podcast_resume_get_sec(const char* url, uint32_t* outSec);

// Update/checkpoint resume position for a URL.
// If curSec is near EOF, the entry is cleared (resume disabled).
void podcast_resume_update_sec(const char* url, uint32_t curSec, uint32_t durSec, bool forceFlush);


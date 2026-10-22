#pragma once
// Diagnostic only (YORADIO_HANG_DIAG): reset reason + RTC breadcrumbs + task WDT panic.
void hangdiag_boot();
void hangdiag_hb(const char* tag);   // loopTask breadcrumb
void hangdiag_dspTaskRegister();
void hangdiag_dspFeed();

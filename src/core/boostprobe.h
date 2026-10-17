#pragma once
// Diagnostic: after each GPIO39 boost ON edge, sample battery voltage (MAX17048 VCELL over I2C,
// as fast as the bus allows) and the LiPo SHIM LOW_BAT line (IO6) and log before/min/max.
// Call boostprobe_beforeOn() immediately BEFORE driving the boost on; sampling runs in a
// short-lived task, so the caller is not blocked.
void boostprobe_beforeOn();

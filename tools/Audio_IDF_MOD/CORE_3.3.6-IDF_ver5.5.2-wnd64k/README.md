# Audio_IDF_MOD variant: CORE_3.3.6-IDF_ver5.5.2-wnd64k

Same as `../CORE_3.3.6-IDF_ver5.5.2` (the tuned lwip from the 4pda/yoRadio Audio_IDF_MOD) with **only the
TCP receive window raised from 32768 to 65535** (plus the TCP receive mailbox sized to match).

| file | md5 | notes |
|---|---|---|
| liblwip.a | 65a4e6502d190691e53fe9c99bcc3368 | rebuilt, TCP_WND 65535 |
| libesp_netif.a | a4c11c7f5c9f3ed6ec9e7a123c2876e0 | **unchanged copy** of the 32k variant (rebuilt objects were identical apart from debug info) |

## Settings (applied on top of Arduino-ESP32 3.3.6's stock `esp32s3/sdkconfig`)

| option | stock 3.3.6 | 32k mod | this variant | why |
|---|---|---|---|---|
| CONFIG_LWIP_TCP_WND_DEFAULT | 5760 | 32768 | **65535** | the change: 64 KB in flight; RP FLAC ~100 KB/s at ~256 ms RTT needs >25 KB, 32 KB only just covers it. 65535 fits 16 bits, so no window scaling |
| CONFIG_LWIP_TCP_RECVMBOX_SIZE | 6 | 32 | **64** | coupled: one mbox entry per received segment; 65535/1436 (MSS) = 46 segments, 32 entries would cap the usable window at ~45 KB |
| CONFIG_LWIP_TCP_SND_BUF_DEFAULT | 5744 | 8192 | 8192 | unchanged |
| CONFIG_LWIP_TCP_QUEUE_OOSEQ | y | y | y | unchanged |
| CONFIG_LWIP_MAX_ACTIVE_TCP | 16 | **16** | 16 | the 4pda doc says 512, but the shipped 32k lib's memp limit is 16 (do_memp_malloc_pool compares against 15); 16 reproduces it exactly |
| CONFIG_LWIP_MAX_LISTENING_TCP | 16 | 512 (per doc) | 512 | kept as the doc; no effect on the compiled objects |
| CONFIG_LWIP_WND_SCALE | n | n | n | not needed for 65535 |

## Reproduction (verified)
- ESP-IDF `f56bea3d1f42f7ad30bb2fa6ff1f64341f0d938c` (release/v5.5, = Arduino 3.3.6 libs `versions.txt`), toolchain esp-14.2.0_20251107.
- Minimal IDF project `set(COMPONENTS main lwip esp_netif esp_psram esp_wifi)`, `sdkconfig` = Arduino 3.3.6 `esp32-arduino-libs/esp32s3/sdkconfig` with the overrides in `lwip_overrides.cfg`, `idf.py set-target esp32s3 && idf.py reconfigure`, then `ninja -C build esp-idf/lwip/liblwip.a esp-idf/esp_netif/libesp_netif.a`.
- Checks: with no overrides the build reproduces the stock 3.3.6 liblwip/libesp_netif (same symbols/sizes, identical disassembly incl. relocations). With the 32k settings (window 32768, recvmbox 32, snd buf 8192, OOSEQ, max active 16) **all 92 liblwip and 11 libesp_netif objects are byte-identical to the shipped 32k mod after `objcopy --strip-debug -R .comment`** (only DWARF/path differs).
- This variant vs the 32k mod: only api_msg, tcp, tcp_in, tcp_out objects differ (window/mbox constants). tcp_alloc sets rcv_wnd/rcv_ann_wnd with `movi.n a8,-1; s32i a8,a2,84` (0xFFFF/0xFFFF) instead of literal 0x80008000.

# Audio_IDF_MOD variant: CORE_3.3.6-IDF_ver5.5.2-wnd48k

Same as `../CORE_3.3.6-IDF_ver5.5.2` (the tuned 32 KB lwip) with **only the TCP receive window raised from
32768 to 49152**. TCP recv mbox stays 32 (wnd64k with mbox 64 dropped WiFi ~2 s into RP FLAC).

| file | md5 | notes |
|---|---|---|
| liblwip.a | 4f3d8bf471a390e41c9dc77f3b449ef9 | rebuilt, TCP_WND 49152 |
| libesp_netif.a | a4c11c7f5c9f3ed6ec9e7a123c2876e0 | **unchanged copy** of the 32k variant |

Settings: `lwip_overrides.cfg` (window 49152, snd buf 8192, recvmbox 32, OOSEQ, max active TCP 16, max listening 512).
Built with the same method as `../CORE_3.3.6-IDF_ver5.5.2-wnd64k/README.md` (IDF f56bea3d, esp-14.2.0_20251107).

Verified vs the shipped 32k lib after `objcopy --strip-debug -R .comment`: 89 of 92 liblwip objects byte-identical;
only tcp, tcp_in, tcp_out differ, and only in window constants (0x8000 -> 0xC000, literal 0x80008000 -> 0xc000c000,
1<<15 -> 3<<14, 1<<14 -> 3<<13, movi 128 -> 192). Function sizes unchanged. api_msg identical (mbox unchanged).

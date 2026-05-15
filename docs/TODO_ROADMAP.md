## TODO / Roadmap

- **Fix up invalid pin warnings at boot**: find what is generating `__pinMode(): Invalid IO 255 selected`
- **Fix SD → WEB → HLS AAC stall properly** (not just workarounds)
- **More controls via MQTT**: add other controls that only exist in the web UI (tone, smart start, auto dimming, screensaver), and include some fork-only features too
- **Test/refine low battery cutoff**: not properly tested
- **Theme switching**: add a way to select/switch themes (web UI/config + persist chosen theme)
- **Boot screen improvement**: simple animation + build info; unicode SSID handling
- **IR control UX**: set up receiver + on-screen “IR RX” indicator
- **Station logo workflow polish**: improve matching/coverage; automate maintaining the local image library
- **SD playback resume**: track resume is implemented; resume *position* still WIP
- **Album art**: revisit later (needs stable decoder/task model)
- **Output via bluetooth**: may not be possible without a dedicated BT module
- **Podcast mode**: dedicated mode to stream recent episodes from podcast feeds
- **Load station logos from SD**: might be easier long-term; likely needs stable image decode first
- **Configure new features from web**: allow setting options like `BATTERY_ENABLED`, `AUTO_DEEPSLEEP_IDLE_MINUTES`, `AUTO_DEEPSLEEP_BATT_PCT` from the web interface
- **Reduce blocking patterns and busy-waits** (queue allocation loops, MQTT playlist block)


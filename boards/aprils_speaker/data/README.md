SPIFFS overlay for `[env:aprils-speaker]` (see `tools/pio/per_env_data_dir.py`).

Files here replace/add to `data/` at the same relative path when building the April's Speaker
filesystem image (`pio run -e aprils-speaker -t buildfs/uploadfs`), e.g. `data/playlist.csv`.
Empty for now: April uses the shared `data/` (playlist, podcasts, web UI). This README is skipped.

"""Per-env SPIFFS data folder.

PlatformIO's data_dir is project-wide. An env can set
    custom_data_overlay = boards/<device>/data
to build its filesystem image from data/ with that folder copied over it (same relative paths
win). The merged tree is staged in $BUILD_DIR/data_fs and used as PROJECT_DATA_DIR for
buildfs/uploadfs only; envs without the option use data/ unchanged.
"""

Import("env")

import os
import shutil

overlay = env.GetProjectOption("custom_data_overlay", "").strip()
if overlay:
    project_dir = env.subst("$PROJECT_DIR")
    base = env.subst("$PROJECT_DATA_DIR")
    overlay_dir = os.path.join(project_dir, overlay)
    staged = os.path.join(env.subst("$BUILD_DIR"), "data_fs")

    def _stage(*_args, **_kwargs):
        if os.path.isdir(staged):
            shutil.rmtree(staged)
        shutil.copytree(base, staged)
        if os.path.isdir(overlay_dir):
            for root, _dirs, files in os.walk(overlay_dir):
                for name in files:
                    if name in ("README.md", ".gitkeep"):
                        continue
                    src = os.path.join(root, name)
                    dst = os.path.join(staged, os.path.relpath(src, overlay_dir))
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copy2(src, dst)
        print(f"[data-dir] {os.path.relpath(staged, project_dir)} = data/ + {overlay}")

    if any(t in COMMAND_LINE_TARGETS for t in ("buildfs", "uploadfs", "uploadfsota")):
        _stage()
        env.Replace(PROJECT_DATA_DIR=staged)

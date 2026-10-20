# Rendering work

- Rendering, quality policy, feature state, and debug-menu logic belong in shared C++. Browser JavaScript hosts the renderer and forwards input.
- This development machine has limited RAM and no swap. Run local builds and rendering checks sequentially inside `python3 scripts/browser_budget.py --report <path> -- <command>`. Defaults cap the entire process tree at 1100 MiB and 90% of one CPU, with a 768 MiB system reserve and a time limit. The wrapper fails closed if the envelope cannot be established.
- Browser checks must be headless and use software rendering. Unset `DISPLAY`, `WAYLAND_DISPLAY`, and `NIXOS_OZONE_WL`. Use an unwrapped Chromium executable so child processes remain in the test cgroup.
- Run full-scene/native GPU suites and full before/after memory matrices in CI. Never run them concurrently on this machine, raise limits to make a failing test pass, or launch a lock screen for validation.
- Memory claims must distinguish WASM allocator use/capacity, requested GPU bytes, driver overhead, and aggregate process-tree memory. Keep the original assets and renderer as the authoritative sources; do not create a second browser scene implementation.

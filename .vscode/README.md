# ESP-IDF 5.0.2 (when palette command doesn’t work)

This workspace is set up for **ESP-IDF 5.0.2**. The command **“ESP-IDF: Select Current ESP-IDF Version”** is known to fail in some extension versions.

**Use the tasks instead:**

- **Build:** `Terminal` → `Run Task...` → **ESP-IDF: Build (5.0.2)** (or `Cmd+Shift+B` for default build)
- **Flash:** `Terminal` → `Run Task...` → **ESP-IDF: Flash (5.0.2)**

Both tasks use the 5.0.2 paths in `settings.json` and `tasks.json`. The ESP-IDF status bar is disabled here to avoid showing a wrong cached version.

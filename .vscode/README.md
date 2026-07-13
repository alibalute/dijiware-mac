# ESP-IDF 5.0.2 in Cursor

This workspace is configured for **ESP-IDF 5.0.2** (`idf.espIdfPath`, CMake 3.24, Ninja 1.10.2).

## Build (preferred: ESP-IDF extension)

Use the **Build** icon in the bottom ESP-IDF status bar, or run **ESP-IDF: Build your project** from the Command Palette.

Keyboard shortcut: **Cmd+I B** (macOS).

## If you see `command 'espIdf.buildDevice' not found`

The ESP-IDF extension failed to activate. Common cause in Cursor:

> Unable to write into folder settings because the file has unsaved changes. Please save the **dijiware-mac** folder settings file first.

**Fix:**

1. Save `.vscode/settings.json` (and close it if you are not editing it).
2. **Developer: Reload Window** from the Command Palette.
3. Confirm the ESP-IDF status bar appears at the bottom.
4. In a multi-root workspace (firmware + Flutter), run **ESP-IDF: Pick a Workspace Folder** and choose **dijiware-mac**.

Check **Output → ESP-IDF** or **Help → Toggle Developer Tools → Console** if it still fails.

## Fallback: tasks

If the extension is unavailable:

- **Build:** `Cmd+Shift+B` → **ESP-IDF: Build (5.0.2)**
- **Flash:** **Terminal → Run Task… → ESP-IDF: Flash (5.0.2)**

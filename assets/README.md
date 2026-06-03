# SnipTune 10 Assets

This folder contains public assets used by SnipTune 10.

## Icons

`assets/icons` contains the SnipGeek/SnipTune icon files used by the host, installer, and release package:

* `snipgeek.svg`
* `snipgeek-512.png`
* `snipgeek.ico`

If the source artwork changes, regenerate the `.ico` with:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Generate-SnipGeekIcon.ps1
```

## Preview Media

Public screenshots and demo media live in the repository root `preview` folder:

* `preview/taskbar-sniptune.png` shows how to enable the widget from the Windows 10 taskbar Toolbars menu.
* `preview/ss1.png` shows compact mode.
* `preview/ss2.png` shows full mode with progress.
* `preview/demo-vidio-widget.mp4` shows the widget in use.

## Current Widget Highlights

SnipTune 10 1.0.5 focuses on:

* Faster first activation after login with per-user host prewarm.
* Manual taskbar toolbar activation to avoid repeated Windows confirmation dialogs.
* Compact and full view modes.
* Native-looking Windows 10 taskbar background rendering.
* Keyboard and screen reader accessibility.
* Hardened installer, update, and uninstall flow.

Links:

* GitHub: https://github.com/ieproject-app/widget-music
* Website: https://snipgeek.com

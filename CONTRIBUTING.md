# Contributing To SnipTune 10

Thanks for helping improve SnipTune 10. This project is a Windows 10 DeskBand, so changes should be tested carefully because the DLL runs inside `explorer.exe`.

## Development Setup

Install:

* Visual Studio Build Tools 2022 or later with Desktop development with C++.
* Windows 10 SDK.
* Inno Setup 6 if you need to build the installer.

Useful commands:

```bat
.\scripts\Build.cmd Release
.\scripts\Run-WidgetMusicTests.cmd Release
.\scripts\Package-WidgetMusic.cmd Release
.\scripts\Verify-WidgetMusicGoal.ps1 Release
```

Installer build:

```bat
.\scripts\Build-Installer.cmd Release
```

## Pull Request Flow

1. Create a branch from `master`.
2. Keep the change focused.
3. Update README/docs/tests/verifier when behavior changes.
4. Run the Release build, tests, package, and verifier before opening a PR.
5. Describe the user-facing impact and any manual test you performed.

Good first areas:

* Bug fixes.
* Documentation and install guidance.
* Windows 10 compatibility fixes.
* UX polish that keeps the widget lightweight.
* Accessibility improvements.

Please avoid broad rewrites unless an issue or maintainer discussion has agreed on the direction.

## Bug Reports

Use the GitHub bug report template and include:

* SnipTune 10 version.
* Windows version/build.
* Media player or browser used.
* Steps to reproduce.
* Screenshot or video when useful.
* `%TEMP%\WidgetMusicDeskband.log` and `%TEMP%\WidgetMusicHost.log` if debug logging was enabled.

## Security Reports

Do not open public issues for security-sensitive findings. Follow [SECURITY.md](SECURITY.md).

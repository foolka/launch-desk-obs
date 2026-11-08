# Changelog

## 0.3.0

- Add Command Desk with automatic installation detection, manual portable EXE selection and independent normal/tray launch.
- Preserve all settings from schemas 1, 2 and 3; the new app is always opted out until explicitly enabled.
- Test four-app narrow layouts, configuration migration and independent launch modes; refresh screenshots and three-language documentation.

## 0.2.0 - 2026-11-08

- Add Dudos Desk with installed-app discovery, portable EXE selection and independent autostart/tray switches.
- Preserve paths, startup modes and language from both previous settings formats; Dudos stays disabled until explicitly enabled.
- Keep the language selector visible while the app list scrolls in short OBS docks.
- Extend settings migration, executable validation, dock layout and release DLL tests; refresh all three localized screenshots and guides.

## 0.1.1 - 2026-11-03

- Add a separate Start in system tray checkbox for each app, for both automatic and manual starts.
- Explicit normal-window and tray launch modes with Reward Desk 0.10.2+ and Shoutout Desk 0.7.1+.
- Preserve old executable paths, switches and previous startup behavior when reading 0.1.0 settings; write the upgraded format only after a user change.
- Verify independent checkbox persistence, legacy settings and invalid configurations.

## 0.1.0 - 2026-11-03

- New independent OBS dock for starting Reward Desk and Shoutout Desk when OBS finishes loading.
- Automatic installed-app discovery and manual executable selection, with separate opt-in switches.
- Running-process and alternate-copy checks; desktop-user launch through Windows Explorer.
- Atomic isolated settings; corrupt or future settings fail closed and remain untouched.
- Russian, Ukrainian and English UI, OBS theme support, EXE installer and manual ZIP package.
- No Twitch login, network requests, telemetry, app database access or forced application shutdown.

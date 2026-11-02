# No Uninstall Icon (我不需要卸载图标)

A small, dependency-free Windows utility that hides selected Start menu shortcuts by
setting the `FILE_ATTRIBUTE_HIDDEN` bit on the shortcut file.

Written in C++20 with the Windows SDK only: no .NET, no Electron, no third-party
libraries. The window is drawn with Direct2D/DirectWrite on a DirectComposition
surface so it can use the Windows 11 Mica backdrop.

![Main window](docs/screenshot-light.png)

| Dark theme | About | Real window (Mica) |
|---|---|---|
| ![Dark](docs/screenshot-dark.png) | ![About](docs/about.png) | ![Window](docs/window.png) |

## What it does

* Lists every `.lnk` and `.url` under the per-user and all-users Start menu trees
  (`FOLDERID_Programs` and `FOLDERID_CommonPrograms`, resolved through the known
  folder API so redirected profiles work).
* Splits the list into two groups: entries whose name/target looks like an uninstall
  entry, and everything else. Both groups are sorted with the user's collation
  (`CompareStringEx` + `SORT_DIGITSASNUMBERS`), so Chinese names sort by pinyin,
  Japanese by kana, and `App 2` sorts before `App 10`. A leading or trailing
  uninstall word is stripped before sorting, so `卸载 Visual Studio Code` files under
  *V*.
* The checkbox means **"hide this shortcut"**. It reflects only what this tool
  manages, never the current attribute of the file.
* Toggling applies immediately, with an undo toast.
* Optional "hide automatically on sign-in": a Task Scheduler task, created without a
  UAC prompt at sign-in, re-applies the list. Either once per sign-in or as a
  resident watcher that reacts to directory changes.

## Three states, not two

This is the part that makes the tool safe to use:

| State | Checkbox | Badge |
|---|---|---|
| Not hidden | unchecked | – |
| Hidden by something else (installer, another tool, the user) | unchecked | "Hidden elsewhere" |
| Managed by this tool | checked | – |

Every managed record stores the **original attributes** of the shortcut. Unchecking
restores the previous hidden bit instead of blindly clearing it, so a shortcut that
was already hidden before this tool touched it stays hidden when it is released.

## What it does not cover

* **Pinned tiles.** Pins are stored separately (`Quick Launch\User Pinned\StartMenu`
  plus a registry/CloudStore list), so hiding the All-apps shortcut does not unpin
  anything. Only the All-apps list is affected.
* **Packaged (Store/UWP) apps.** They have no shortcut file and cannot be hidden this
  way.
* **"Show hidden files" in Explorer** may make hidden shortcuts visible again in some
  builds; the tool does not fight that setting.

## Files

| Path | Purpose |
|---|---|
| `%LOCALAPPDATA%\NoUninstallIcon\state.dat` | Managed records and settings (tab separated, escaped, atomically replaced) |
| `%LOCALAPPDATA%\NoUninstallIcon\state.dat.bak` | Copy of the previous save, used if the primary file is damaged |
| `%LOCALAPPDATA%\NoUninstallIcon\log.txt` | Rolling log, capped at 1 MB |
| Task `NoUninstallIcon-AutoHide` | Sign-in task, created only when the user enables it |

## Portable mode

Drop a file named `portable` next to `NoUninstallIcon.exe` and the program keeps
everything in `<exe folder>\data` instead of the user profile. The portable archive in
`dist/` ships that marker; delete it to go back to the normal location. If the folder
is not writable (a CD, a locked share) the program falls back to `%LOCALAPPDATA%`
instead of failing.

`NOUI_DATA_DIR` overrides both choices and is what the tests use.

The sign-in task stores an absolute path, so enabling automatic hiding while running
from a removable drive will break once the drive letter changes.

## Build

Requirements: MSVC (VS 2022/2026 with the C++ desktop workload) and the Windows 10/11
SDK. No NuGet packages are used.

```bat
build.bat            :: release
build.bat debug      :: debug
```

Output: `build\NoUninstallIcon.exe` (~400 KB, statically linked C runtime).

## Installer

```bat
ISCC.exe installer\setup.iss
```

Output: `dist\NoUninstallIcon-Setup-1.0.0.exe`. The uninstaller runs
`NoUninstallIcon.exe --cleanup` first, which deletes the sign-in task and gives every
managed shortcut its original attributes back.

## Command line

| Flag | Meaning |
|---|---|
| *(none)* | Normal window. Relaunches itself elevated once. |
| `--apply` | Apply the stored list silently and exit (used by the sign-in task). |
| `--watch`, `--background` | Apply once, then watch both Start menu trees and re-apply on change. Owns the tray icon. |
| `--background --no-tray` | Same, and never shows an icon whatever the mode says. |
| `--restore` | Restore every managed shortcut and forget them. |
| `--cleanup` | Remove the sign-in task, then `--restore` (used by the uninstaller). |
| `--screenshot <png> [--theme light\|dark] [--about]` | Off-screen render of the window, for testing. |
| `--capture <png>` | Show the real window, grab it to PNG, exit. |
| `--dump <txt>` | Scan and write a classification report. |
| `--selftest` | Hide/unhide a fixture entry and verify the invariants. |

`--screenshot`, `--capture`, `--dump` and `--selftest` never change anything and do
not request elevation.

## How the pieces agree

There is exactly one piece of persisted truth: `state.dat`. Everything else is
derived from it and can be rebuilt from it.

| Mode | Sign-in task | Watcher process | Tray icon |
|---|---|---|---|
| Off | none | none | none |
| Once | `--apply` | none | none |
| Watch | `--background` | one | shown |
| Watch (no tray) | `--background --no-tray` | one | none |

The mode travels through a small shared section (`Local\NoUninstallIcon.State`:
a version and the mode) plus a manual-reset event
(`Local\NoUninstallIcon.Changed`). The version makes a stale or out-of-order
delivery harmless; the manual reset means a change published while nobody was
listening is still seen later. Shared memory is used rather than a window
message because the watcher runs elevated and window messages are filtered
between integrity levels.

Ownership is what stops the two processes undoing each other:

- **The window** writes the task, writes the state file, and decides whether a
  watcher should exist.
- **The watcher** owns its icon, applies the hidden attributes, and exits when
  the published mode no longer wants it. It never writes the task.
- **Repairs never change the mode.** Re-adding a lost icon or starting a missing
  watcher is derived work; only the dropdown and the tray's Quit touch the mode.

Reconciliation happens on:

| Trigger | Who | What it fixes |
|---|---|---|
| Start-up | window | re-creates a missing task, starts a missing watcher |
| Mode change | window → watcher | watcher adds or removes its icon, or exits; no restart |
| `TaskbarCreated` | watcher | re-adds the icon after Explorer restarts |
| Tray Quit | watcher | closes the window first, waits for it, then exits; the mode is left alone |

A watcher that dies while the window is closed is only noticed at the next
start-up or the next sign-in. There is no third watchdog process, by design.
Not yet implemented: a watcher that dies **while the window is open** is not
noticed either. The fix would be to hold a handle to the mutex the watcher owns
and wait on it, so the wait returns when the owner dies; that is a kernel
notification rather than polling, but it is not in yet.

Only the mode travels in the broadcast; the shortcut records are still read from
disk when the watcher applies them, because a few hundred records do not belong
in a message.

## The tray icon

`src/tray.cpp` owns it; nothing else in the program touches `Shell_NotifyIcon`.

- The icon window is always created; whether an icon appears follows the mode, so
  changing modes never restarts the process.
- The window is a real (never shown) top-level window. A message-only window
  looks fine but the shell cannot deliver clicks to it.
- After every `NIM_ADD` the icon switches to `NOTIFYICON_VERSION_4`. That matters:
  in version 4 a right click arrives as **`WM_CONTEXTMENU` in the low word of the
  callback's `lParam`**, with the anchor point packed into `wParam`. Code that only
  watches for `WM_RBUTTONUP` never sees the click at all.
- Because the watcher runs elevated, `ChangeWindowMessageFilterEx` (and the older
  `ChangeWindowMessageFilter`) let the medium-integrity shell post to the window.
- Double click opens the main window; if one is already open it is activated
  instead, attaching to its thread because a background process may not take the
  foreground on its own.
- `TaskbarCreated` puts the icon back after Explorer restarts.
- Rights stay consistent: the sign-in task starts the watcher at the highest run
  level, and both the window and any further watcher inherit that level, so no
  extra elevation prompt appears in normal use.

## Why a scheduled task instead of a service

A service runs in session 0 and cannot see the signed-in user's `HKCU` Start menu
without impersonation, starts before the shell exists, and is harder to install and
remove. A Task Scheduler task with `RunLevel=HighestAvailable` and an
`InteractiveToken` principal starts elevated at sign-in without a UAC prompt, which is
exactly what is needed.

The one-shot mode waits 20 s, then applies. Watch mode also reacts to
`ReadDirectoryChangesW` notifications, so a shortcut recreated by an installer is
hidden within a second.

## Testing

```bat
set NOUI_DATA_DIR=%TEMP%\noui
set NOUI_SCAN_ROOT=%TEMP%\noui\menu
NoUninstallIcon.exe --selftest
type %TEMP%\noui\selftest.txt
```

`NOUI_SCAN_ROOT` points the scanner at a fixture folder instead of the real Start menu,
so tests never touch real shortcuts.

## Localisation

All UI strings live in `res/lang/*.lang` and are embedded as `RCDATA`. Eleven
languages ship today: English, German, French, Spanish, Italian, Brazilian Portuguese,
Russian, Korean, Japanese, Simplified Chinese and Traditional Chinese. Missing keys fall
back to English, and so does a locale with no translation. `tools/check-lang.ps1` runs at
the end of `build.bat` and reports any file whose key set differs from `en.lang`.

`res/lang/map.txt` maps Windows locales to a language file (`zh-CN`, `zh-SG` and
`zh-MY` all mean Simplified Chinese). The longest matching prefix wins, then the
primary subtag, then English. Adding a language is one `.lang` file plus one `RCDATA`
line: the table is discovered at startup, no code change needed. The picker starts
with a System entry that follows the current locale on every launch.
## About dialog

The **About** command on the right of the header opens an in-window dialog — a Fluent
`ContentDialog`-style card rather than a `MessageBox` — that states the licence and
links to the author's GitHub profile. Escape, the OK button, or a click outside the
card dismisses it.

## Licence

MIT. Copyright (c) 2026 王云正 (JerryABCD06). See [LICENSE](LICENSE).
## If it crashes

The program installs a top-level exception filter. On an unhandled exception it
writes `crash-YYYYMMDD-HHMMSS.dmp` plus `crash.txt` (exception code and faulting
address) next to `state.dat`, then exits. Attach the dump when reporting a problem —
it is the only way to tell a threading bug from a drawing bug.

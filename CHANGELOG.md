# Changelog

Notable changes, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version numbers follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Upgrading while the watcher runs left a zombie process.** Windows refuses to replace
  a running executable, so Setup asks Restart Manager to close the app first; the tray
  window did not handle `WM_CLOSE`, and `DefWindowProc` destroyed the window without
  ending the process. The survivor kept the `Local\NoUninstallIcon.Background` mutex, so
  the icon was gone, hiding went on invisibly, and no new watcher could start
  (`ERROR_ALREADY_EXISTS`) — even the "launch after install" path could not bring one
  back. The tray window now handles `WM_CLOSE` and `WM_QUERYENDSESSION` by removing the
  icon, stopping the watcher threads and exiting.

### Changed

- The installer declares `AppMutex` for the window's and the watcher's single-instance
  mutexes, so Setup notices a running instance before it touches the executable and asks
  for it to be closed, instead of failing on a file that is in use.

### Notes

- Deferred to the next version on purpose: no one upgrades *to* 0.1.0, so the defect
  cannot be reached from the first release.
- The fix cannot rescue a watcher belonging to 0.1.0, which knows no way to exit except a
  mode change. Going from 0.1.0 to a later version therefore still needs the watcher
  stopped first — switch the mode to *Off*, or end the process in Task Manager.

## [0.1.0] - 2026-11-03

First release.

### Added

- **The list.** Every `.lnk` and `.url` under the per-user and all-users Start menu,
  split into *Uninstall entries* and *Other shortcuts*, sorted with the user's own
  collation (`CompareStringEx` with digits-as-numbers, so Chinese sorts by pinyin and
  `App 2` comes before `App 10`).
- **Hiding that can be undone.** A shortcut is hidden by setting `FILE_ATTRIBUTE_HIDDEN`
  on the file. Each record keeps the attributes the file had *before*, so unchecking
  restores what was there instead of blindly clearing the bit: a shortcut that was
  already hidden by something else stays hidden when this program lets go of it, and is
  shown as "hidden elsewhere" rather than claimed.
- **Immediate apply with undo.** No OK button: a toggle takes effect at once, with a
  toast that offers Undo.
- **Group and bulk actions.** Hide all / Unhide all per group, Unhide everything, rescan,
  and search over names and paths.
- **Optional sign-in apply** through a Task Scheduler task that needs no UAC prompt at
  logon. Four modes: Off, once per sign-in, keep watching (tray icon), and watch without
  a tray icon. The window can be closed and the watching continues.
- **Two processes, deliberately.** The window, which elevates once and is heavy because
  it draws everything, and a small watcher that owns the tray icon and the attribute
  writes. Measured at roughly 82 MB and 2.5 MB of private memory respectively.
- **Eleven languages** — English, Simplified and Traditional Chinese, Japanese, German,
  French, Spanish, Russian, Korean, Brazilian Portuguese, Italian — following the system
  language by default, switchable at runtime without a restart.
- **Light and dark themes** that follow the system, including the system's own dark
  popup menus, with the Mica backdrop where Windows offers it.
- **An interface drawn by hand** with Direct2D, DirectWrite and DirectComposition: no
  child windows, no third-party libraries, a UI Automation tree for screen readers.
- **An installer** that follows the system theme, offers all eleven languages, shows the
  product name in the chosen language, and removes the uninstall shortcut that an older
  build left in the Start menu.
- **A portable archive** that keeps everything beside the executable, with a `Reset.cmd`
  that undoes the program's effect without deleting it.
- **Command line**: `--apply`, `--background`, `--restore`, `--cleanup`, `--dump`,
  `--selftest`, `--screenshot`, `--capture`, `--log`, and a few test-only switches.

### Notes

- The binaries are **unsigned on purpose**. A self-signed certificate would carry an
  untrusted publisher name, which is no better than none, so releases stay unsigned and
  Windows will show the usual warning.
- Logging is **off by default**; `--log` or `NOUI_LOG=1` turns it on.
- Requires 64-bit Windows 10 (1809 or newer) or Windows 11.

### Known limitations

- **Pinned Start tiles are not affected.** They are stored separately from the All-apps
  list, so hiding a shortcut does not unpin anything.
- **Packaged (Store/UWP) apps have no shortcut file** and cannot be hidden this way.
- With Explorer's *show hidden files* enabled, a hidden shortcut may still appear.
- The data directory is locked against other sessions, not against other machines: keep
  it on a local disk.
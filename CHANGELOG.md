# Changelog

Notable changes, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version numbers follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-11-09

### Fixed

- **Clicking a row after scrolling no longer jumps the list to the top.** Every click that
  was not on the search box went through `SetFocusTarget(List)`, whose "no row yet" branch
  invented row 0 and scrolled it into view. With no row focused — the normal state after
  the wheel or a scrollbar drag — the first click therefore moved the view before the hit
  test ran, and toggled whatever row ended up under the pointer. That branch now runs only
  for keyboard entry (Tab, and Down from the search field), which is where revealing the
  focused row belongs. Measured with two builds scrolling five notches and clicking: the
  old one toggled `Shortcut 02`, the fixed one toggled `Shortcut 14`, the row actually
  under the pointer.

- **Setup no longer offers to leave the running app alone.** Inno's Restart Manager
  dialog has a "do not close the applications" button, and choosing it let Setup copy over
  a locked executable and fail at that point. `CloseApplications=no` turns that dialog off;
  `PrepareToInstall` asks the watcher to leave, closes the open window politely, and only
  prompts before a forced close — answering *no* now cancels the installation rather than
  carrying on.

- **Upgrading while the watcher runs no longer leaves a zombie process.** Windows cannot
  replace a running executable, so Setup asks Restart Manager to close the app; the tray
  window answered `WM_CLOSE` with `DefWindowProc`, which destroyed the window without
  ending the process. The survivor kept the `Local\NoUninstallIcon.Background` mutex, so
  the icon was gone, hiding continued invisibly, and no new watcher could start
  (`ERROR_ALREADY_EXISTS`) — even "launch after install" could not bring one back. The
  tray window now ignores `WM_CLOSE` outright, and leaves only when asked properly.

### Added

- `--stop-watcher`: asks a running watcher to leave without touching `state.dat`. The
  request travels through the shared section a mode change already uses, so it reaches
  watchers built before the switch existed, and the stored mode survives unchanged.

### Changed

- The installer stops a running instance before it copies anything: `--stop-watcher`
  when the installed build supports it, then `WM_CLOSE` to the window, then — only if
  something is still alive — a prompt before forcing it closed. Declining cancels the
  installation. Silent installs force it closed without asking, because there is nobody to
  ask.

### Notes

- The forced close is a fallback for 0.1.0, whose watcher knows no other way out. From
  0.1.1 onwards the stop is always the graceful kind, and the stored mode is untouched.

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
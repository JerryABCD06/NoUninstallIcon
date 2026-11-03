# Troubleshooting

Symptom first, then what to check. Most of these end with "turn the log on and read the
right line", so the log is described first.

## Turning the log on

The log is off by default.

```bat
NoUninstallIcon.exe --log        :: this run only
set NOUI_LOG=1                   :: this session, inherited by the watcher
```

`--log` is forwarded to the elevated copy of the window and to a watcher the window
starts. It is **not** forwarded to a watcher started by the sign-in task, and not to a
watcher that was already running — that one keeps whatever it started with. For those,
set `NOUI_LOG=1` in the environment before sign-in.

Where it goes: `%LOCALAPPDATA%\NoUninstallIcon\log.txt`, or `<exe>\data\log.txt` in
portable mode.

## The icon is not in the notification area

Check in this order:

1. **Is the mode Watch?** Silent and Off have no icon by design.
2. **Is a watcher running?** Task Manager, or `tasklist | findstr NoUninstallIcon`.
   `--background` with mode Off or Once exits immediately by design.
3. **Did the window log a failed launch?** Look for `watcher launch failed` — the code
   prints the `ShellExecuteW` result, and a value of 5 means the shell refused.
4. **Did the watcher log a failed registration?** `tray: add failed (attempt=…)` with an
   error code. `err=5` means access denied, which happens in restricted or sandboxed
   sessions; it does not happen in a normal desktop session.
5. **Did Explorer restart?** The watcher re-adds the icon on `TaskbarCreated`, but only
   when the mode wants one.
6. **Has the user hidden the icon themselves?** Windows lets a user turn off a
   notification-area icon for one app. Nothing this program can do overrides that.

## The window does not start, or starts and closes at once

- **A second instance**: the existing window is brought forward and the new process
  exits. That is the single-instance rule.
- **Elevation refused**: the relaunch fails and the program shows the elevation message
  instead of running. If `NOUI_NO_ELEVATE=1` is set in the environment, the relaunch is
  skipped and the window runs at whatever level it started with, which may then fail to
  modify the all-users Start menu.
- **The state file is unreadable**: the window opens with an empty list and refuses to
  write. The log says `State file and backup are both unreadable`. Do not delete
  `state.dat.bak`; it may be the only copy.

## A shortcut stays hidden after unchecking it

The checkbox restores the attributes recorded **before** this tool touched the file. If
something else has hidden the file since, the entry stays hidden and the row shows the
"hidden elsewhere" badge — that is the correct behaviour, and the toast says so.

If the row shows unchecked with no badge but the entry is still hidden, the file's
attributes changed outside the program; press *Rescan*.

## A shortcut is hidden but the program does not list it

This is the state the intent-first ordering exists to make impossible, but an older
build or a crash in a narrow window could produce it. Recover by hiding it again from the
window (which adopts the record) or by running `--restore`, which restores every record
it knows about; a shortcut with no record has to be unhidden by hand in Explorer.

## The sign-in task is missing, or points at the wrong place

- The task is created when the mode is set, and reconciled at every window start: a
  missing task is re-created, and a task that no mode wants is removed.
- The task stores an **absolute path**. Moving or renaming the program folder leaves it
  pointing at nothing; re-select the mode to re-register it.
- The task's arguments are not validated at start-up: a task left over from an older
  version with different arguments is not corrected. Removing it
  (`schtasks /delete /tn NoUninstallIcon-AutoHide /f`) and re-selecting the mode fixes
  it.
- `--cleanup` removes it; the uninstaller runs `--cleanup` before deleting the files.

## "The state file could not be written"

A save failed and the change was undone. Causes seen in practice:

- the data directory is read-only, or on a share that went away;
- an antivirus scanner has the file open;
- the disk is full;
- the state file could not be read at start-up, in which case every save is refused for
  the life of the process.

The last case is deliberate: overwriting a file that could not be parsed risks destroying
the only record of which shortcuts were hidden.

## Everything looks empty

- First run: there is no `state.dat` yet, and nothing is hidden.
- Portable mode with an unwritable `data` folder: the program falls back to
  `%LOCALAPPDATA%` silently, so the two copies of the program use different state files.
  Check which directory it is using with `--log`.

## Task Manager shows the process in a language that does not match the program

Task Manager reads `FileDescription` from the binary and chooses the string by the
**system UI language**, not by the language the program is set to. That string is fixed
English (`No Uninstall Icon`) for exactly this reason. The in-window language setting
affects the window, the menus and the About dialog, never the process name.

## Reporting a problem

Include:

- the version (file properties, or the installer name);
- the Windows version;
- the mode, and whether the tray icon was present;
- `log.txt` from a run with `--log` or `NOUI_LOG=1`;
- the crash dump, if the program faulted — it writes `crash-*.dmp` and `crash.txt` next
  to the state file without being asked.

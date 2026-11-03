# Testing

What can be tested without touching a real Start menu, and what cannot be tested at all
from a headless session.

## Environment variables

| Variable | Effect |
|---|---|
| `NOUI_DATA_DIR` | use this directory instead of `%LOCALAPPDATA%\NoUninstallIcon` |
| `NOUI_SCAN_ROOT` | scan this directory instead of the real Start menu trees |
| `NOUI_NO_ELEVATE=1` | do not relaunch elevated |
| `NOUI_LOG=1` | write `log.txt` (any value except `0`) |
| `NOUI_KEYLOG=1` | log every key press, to find a key that never arrives |

`NOUI_DATA_DIR` and `NOUI_SCAN_ROOT` together are what keep a test away from real
shortcuts. Neither is documented as a user feature; they exist so the program can be
exercised in a temporary directory.

## Command-line test modes

| Command | What it does |
|---|---|
| `--screenshot <png> [--theme light\|dark] [--about] [--query <text>] [--scroll <px>] [--focus <n>]` | renders the window off-screen to a PNG |
| `--capture <png>` | shows the real window, grabs it, exits |
| `--preview [--query ...] [--focus ...] [--scroll ...] [--about]` | opens the window in a test state |
| `--dump <txt>` | scans and writes the classification, twice: raw and in display order |
| `--selftest` | hides and unhides a fixture entry and verifies the invariants |
| `--crash-test` | faults on purpose, to exercise the crash handler |

`--screenshot` never shows a window and is safe on a build server, provided the data
directory is temporary. `--capture` and `--preview` run the start-up reconciliation, so a
temporary data directory is required unless you want them to touch the task and the
watcher.

## A self-test run

```bat
set NOUI_DATA_DIR=%TEMP%\noui
set NOUI_SCAN_ROOT=%TEMP%\noui\menu
mkdir %TEMP%\noui\menu
copy "C:\ProgramData\Microsoft\Windows\Start Menu\Programs\*.lnk" %TEMP%\noui\menu\
NoUninstallIcon.exe --selftest
type %TEMP%\noui\selftest.txt
```

The self-test checks two things, and reports each step:

1. a fixture shortcut is hidden and then unhidden — the hidden bit really is set, and
   when it is cleared the attributes match what they were before;
2. a shortcut that was **already hidden by something else** is adopted and released —
   it must stay hidden, and must end up unmanaged, because this tool never claimed it.

`RESULT: mismatch` and exit code 1 mean one of them failed; the report shows the
`bit=`, `managed=` and `record=` values that decided it.

## What a headless session cannot verify

These need a real desktop and were verified by hand only:

- the tray icon appearing, and every click on it (the shell's delivery, the version-4
  protocol, the menu anchor);
- the notification-area behaviour after an Explorer restart;
- elevation, including whether a command-line run can post to an elevated window;
- how Windows 10 paints the window when Mica is refused;
- DPI changes on a multi-monitor setup;
- what a screen reader actually announces;
- real `SetFileAttributesW` outcomes for the all-users Start menu, which depend on the
  machine's ACLs;
- a Task Scheduler run for a domain or Microsoft account.

Anything that depends on these should be verified by hand before a release.

## A five-minute manual pass

1. Open the window. The list fills; the header counts match the rows.
2. Search for something; the list filters; clearing restores it.
3. Hide one shortcut. The checkbox appears checked, the Start menu entry disappears, the
   toast offers Undo.
4. Undo. The entry comes back; no second Undo is offered.
5. Scroll into the second group and press `Up` until the first row of that group is
   focused. It must be fully visible, not half under the group header.
6. Hold `PgDn` to the bottom and `PgUp` back. The same rows come back; nothing is skipped.
7. Right-click a row, then right-click the pinned group header. The second click must not
   open a menu for the row hidden under it.
8. Switch the mode to *Watch*: an icon appears in the notification area without a UAC
   prompt. Switch to *Silent*: it disappears while the watcher keeps running. Switch to
   *Off*: the watcher exits (Task Manager).
9. Tray menu → *Quit*: the window closes first, then the icon disappears.
10. Sign out and back in with mode *Once*: the shortcut is hidden once, and no process
    stays behind.
11. Run `Reset.cmd` from the portable archive: every hidden shortcut comes back, the
    sign-in task is gone (`schtasks /query /tn NoUninstallIcon-AutoHide`), and the tray
    icon is gone with it.

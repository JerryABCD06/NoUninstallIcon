# Architecture

This document describes how the program is put together: the processes it runs, the
one piece of state they share, the rules that keep them from fighting, and where each
responsibility lives in the source.

For the user-facing description see the [README](../README.md); for the file formats
see [state-file.md](state-file.md); for the Win32 details see
[win32-notes.md](win32-notes.md).

## The one sentence version

`state.dat` says which shortcuts should be hidden and which mode the program is in;
everything else — the window, the watcher, the tray icon, the sign-in task — is derived
from that file and can be rebuilt from it.

## Actors

| Actor | Lifetime | Owns |
|---|---|---|
| **Window** (`NoUninstallIcon.exe`) | user starts it, it exits when the window closes | the UI, the scheduled task, the decision whether a watcher should exist |
| **Watcher** (`--background`) | from sign-in (via the task) or started by the window | the tray icon, the file attributes of hidden shortcuts |
| **Command-line runs** (`--apply`, `--restore`, `--cleanup`, `--dump`, `--selftest`, `--screenshot`, `--capture`) | one shot | whatever the mode does, then exit |
| **Sign-in task** (`NoUninstallIcon-AutoHide`) | until removed | nothing by itself; it starts one of the above at sign-in |
| **Explorer** | always | paints the tray icon, delivers its clicks |

The window and the watcher are separate processes on purpose; the reasoning and the
measured cost are in the README's *Why there are two processes*. The practical
consequence is that everything below exists only because of that split.

## Modes and what they derive

`state.dat` stores one of four modes. The mode decides which of the other things must
exist; they are never configured independently.

| Mode | Sign-in task | Watcher | Tray icon |
|---|---|---|---|
| Off | removed | none | none |
| Once | `--apply` | none | none |
| Watch | `--background` | one | one |
| Silent | `--background` | one | none |

A watcher started with `--no-tray` never shows an icon whatever the mode says; that is
a manual override, not a fifth mode.

## The shortcut state model

A shortcut is described by three independent facts, and confusing them is the main
source of bugs in tools like this:

1. **What the user wants** — `desiredHidden` in the record, shown as the checkbox.
2. **Whether this tool manages it** — a record exists for it.
3. **What the file system says right now** — the `FILE_ATTRIBUTE_HIDDEN` bit.

That gives three visible states:

| Checkbox | Badge | Meaning |
|---|---|---|
| checked | — | hidden by this tool |
| unchecked | — | visible, not managed by this tool |
| unchecked | "hidden elsewhere" | hidden, but not by this tool (an installer, a GPO, the user) |

The `originalAttributes` field in each record is what makes the third case safe: when
unhiding, the program restores the bit that was there **before** it ever touched the
file, so a shortcut that someone else hid stays hidden.

## Transactions: the state lock

Every load → modify → save sequence holds `StateLock` (`src/store.h`), otherwise two
processes rewriting the whole file would erase each other's work. It has two levels:

| Level | Object | Scope | Wait |
|---|---|---|---|
| session | mutex `Local\NoUninstallIcon.StateFile` | this logon session | 2.5 s |
| machine | byte-range lock on `<data>\state.lock` | every session and every machine sharing the file | 25 × 100 ms |

The named mutex alone is not enough: it is per session, while `state.dat` is per user
and, in portable mode, can live on a share.

Three rules make the lock safe to use:

- **Reload under the lock.** A guarded writer calls `Store::Load()` after acquiring,
  because the in-memory copy is otherwise as old as the process.
- **Fail closed.** If the lock cannot be taken the operation is abandoned with a log
  line (and, in the window, an error toast) rather than writing unprotected.
- **Nested use shares the outer file lock.** `HandleUndo` takes the lock and then calls
  `HandleToggle`, which takes it again. The mutex is recursive; a byte-range lock is
  not, so a `thread_local` depth counter lets the inner acquisition skip the file lock
  and share the outer one. Releasing is based on what the object owns, not on the
  counter, so a future non-nested use cannot leak it.

## Ordering: intent before effect

A hide is written to disk **before** the attribute changes:

1. create the record (`desiredHidden = false`) and save it,
2. set `FILE_ATTRIBUTE_HIDDEN`,
3. set `desiredHidden = true` and save again.

If the process dies between 1 and 2 the record says "not hidden" — harmless, and the
row can simply be hidden again. If it dies between 2 and 3 the shortcut is hidden **and
the record exists**, so it can still be released. Without step 1 there would be a
window in which a shortcut is hidden with nothing remembering it. Group hides do the
same thing once for the whole batch instead of once per row.

Every change that cannot be persisted is undone and reported: the attribute is put
back, the record is dropped or kept as appropriate, and the toast says the state file
could not be written.

## Signalling between processes

| Object | Type | Purpose |
|---|---|---|
| `Local\NoUninstallIcon.State` | shared memory, `{LONG version; LONG mode;}` | carries the mode |
| `Local\NoUninstallIcon.Changed` | manual-reset event | "the mode changed, go and look" |
| `Local\NoUninstallIcon.MainWindow` | mutex | one window |
| `Local\NoUninstallIcon.Background` | mutex | one watcher |
| `Local\NoUninstallIcon.StateFile` | mutex | the state transaction |

The broadcast carries the **mode only**. The records are too large to send and are read
from disk by whoever needs them.

Two details matter:

- The event is **manual-reset**, and the reader clears it *before* reading the mode.
  A publish that lands during the read sets it again, so the change is not lost.
- The section and the event are created with an explicit descriptor
  (`D:(A;;GA;;;WD)S:(ML;;NW;;;LW)`) so a medium-integrity command-line run — the
  uninstaller's `--cleanup`, for example — can still open what an elevated window
  created. Without that, its "Off" publish would silently fail.

## Reconciliation

Nothing polls. Things are put right when one of these happens:

| Trigger | Who acts | What it fixes |
|---|---|---|
| Window start-up | window | installs or removes the sign-in task to match the mode, publishes the mode, starts a watcher if the mode wants one and none is running |
| Mode change | window → watcher | the watcher adds or removes its icon, or exits; no process is restarted |
| Directory change | watcher | reloads the state file and re-applies |
| Watcher start-up | watcher | exits at once if the stored mode is Off or Once |
| `TaskbarCreated` | watcher | re-adds the icon after Explorer restarts, but only when it should be visible |
| Tray *Quit* | watcher | closes the window first, then exits; the mode is left alone |
| Uninstall | `--cleanup` | closes the window, publishes Off, waits for the watcher, removes the task, restores every record |

The rules these follow:

- **Repairs never change the mode.** Re-adding an icon or starting a missing watcher is
  derived work; only the dropdown and the uninstaller touch the mode.
- **The window owns the task; the watcher never writes it.**
- **The watcher never saves the state file.** It loads and applies; saving from there
  would rewrite settings it does not own.

There is no third watchdog process. A watcher that dies while the window is closed is
only noticed at the next start-up or the next sign-in.

## Threads

| Process | Thread | Does |
|---|---|---|
| window | UI | drawing, input, every state mutation |
| window | scan | enumerates the Start menu, then exits its work and is joined |
| window | icon cache | decodes shell icons |
| window | start-up reconciliation | one task operation, then exits |
| window | autostart worker | one task operation per mode change |
| watcher | main | message loop, tray window, mode changes |
| watcher | two notify threads | one per Start menu tree, wait for changes and re-apply |

The state lock is taken by the UI thread and by the watcher's notify threads. The
watcher also has a process-local `applyMutex` so the two notify threads never apply
concurrently.

## Modules

| File | Responsibility |
|---|---|
| `src/main.cpp` | command line, elevation, single instance, mode dispatch |
| `src/app.cpp` | orchestration: handlers, reconciliation, the watcher loop, the command-line modes |
| `src/ui.cpp` | the window: layout, drawing order, input, hit testing, focus, popups |
| `src/paint.cpp` | drawing primitives on a Direct2D device context |
| `src/gfx.cpp` | D3D11 + Direct2D + DirectComposition device, swap chain, Mica |
| `src/scanner.cpp` | Start menu enumeration, uninstall heuristics, sort keys |
| `src/model.cpp` | filtered rows, counts, focus movement |
| `src/store.cpp` | `state.dat` parse/save, atomic write, `StateLock` |
| `src/apply.cpp` | the one place that sets or clears the hidden bit |
| `src/autostart.cpp` | the scheduled task |
| `src/tray.cpp` | notification-area icon, menu, clicks |
| `src/session.cpp` | shared section + event |
| `src/i18n.cpp` | language files, system-locale resolution |
| `src/a11y.cpp` | UI Automation provider |
| `src/crash.cpp` | unhandled-exception filter and dump |
| `src/util.cpp` | paths, files, logging, elevation check, formatting |

Roughly 7 000 lines of C++ including headers, no third-party dependencies, statically
linked C runtime.

## Failure philosophy

There is no silent fallback. A failed save is undone and reported; a watcher that
could not start is reported; a state file that cannot be parsed is left alone and the
program refuses to overwrite it; a language that cannot be written is rolled back in
the interface. The log is off by default, so the user is told rather than told to go and
read a file.

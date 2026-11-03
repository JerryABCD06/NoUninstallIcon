# Files and formats

Everything the program persists, where it lives, and how it is written. If a file is
not listed here, the program does not create it.

## Where the data lives

| Mode | Directory |
|---|---|
| Installed | `%LOCALAPPDATA%\NoUninstallIcon\` |
| Portable | `<folder containing the exe>\data\` |

Portable mode is chosen when a file named `portable` sits next to the executable. The
folder is used in place of `%LOCALAPPDATA%`; if it is not writable the program falls
back to the user profile and logs nothing, because logging is off by default — the
fallback is deliberately silent and only affects where the data goes.

The data directory is created on demand. Nothing is created at start-up just to hold a
file that would never be written.

## `state.dat`

The single source of truth. Tab-separated, UTF-8, CRLF line endings.

```
NOUI1
S	language	zh-Hans
S	autostart	watch
R	<key>	<path>	<originalAttributes>	<desiredHidden>	<displayName>	<target>	<firstSeen>	<lastApplied>
```

### Header

`NOUI1` on the first line. A file without it is not parsed.

### Setting lines (`S`)

`S <TAB> name <TAB> value`. Two names are used:

| Name | Values |
|---|---|
| `language` | a language code such as `zh-Hans`, or absent to follow the system locale |
| `autostart` | `off`, `once`, `watch`, `silent` |

A file without an `autostart` line is rejected as truncated — every state this program
writes contains one, so its absence means the write was cut short.

### Record lines (`R`)

Nine tab-separated fields:

| # | Field | Meaning |
|---|---|---|
| 1 | `R` | line type |
| 2 | key | normalised lower-case full path; the record's identity |
| 3 | path | the shortcut as it should be opened |
| 4 | originalAttributes | the attributes **before** this tool first touched the file, in hex |
| 5 | desiredHidden | `1` if the user wants it hidden, `0` otherwise |
| 6 | displayName | what the list shows |
| 7 | target | the shortcut's target (used by "open target location") |
| 8 | firstSeen | Unix seconds |
| 9 | lastApplied | Unix seconds; written by the window, not by the watcher |

`originalAttributes` is what makes unhiding safe. If the file was already hidden by
something else, clearing the checkbox restores that bit instead of making the shortcut
appear.

A record line with fewer than nine fields is treated as corruption, not skipped: a
skipped record would be dropped for good at the next save.

### Escaping

Four characters are escaped in every field, backslash first:

| Character | Written as |
|---|---|
| `\` | `\\` |
| tab | `\t` |
| newline | `\n` |
| carriage return | `\r` |

Because of that, a path written by hand needs its backslashes doubled:
`C:\\Users\\me\\...`. Getting this wrong is the most common way a hand-made test file
fails to parse.

### Writing

`Store::Save` never edits the file in place:

1. the document is serialised into a string,
2. written to `state.dat.<pid>.tmp` and flushed,
3. the previous `state.dat` is copied to `state.dat.bak` — unless the data that was
   loaded came from the backup, in which case copying the damaged primary would destroy
   the only good copy,
4. the temporary file replaces `state.dat` with `MoveFileEx(..., MOVEFILE_WRITE_THROUGH)`.

The result is that a reader sees either the old file or the new one, never a half one,
and that the backup is only replaced by a primary that was known to load.

### Reading

`Store::Load` tries `state.dat`, then `state.dat.bak`. Four outcomes:

| Situation | Result |
|---|---|
| neither file exists | first run: empty state, writes allowed |
| the primary parses | normal |
| the primary fails, the backup parses | recovered, and the backup is protected from being overwritten by the damaged primary |
| both fail | `readOnly_` is set: `Save` refuses, so neither file is touched |

The last case is a deliberate refusal to work rather than a guess. It shows up as a
program that starts with an empty list and refuses to hide anything, with the reason in
the log if logging is on.

## `state.lock`

A zero-byte file next to `state.dat`, created on demand, used for the cross-session
byte-range lock described in [architecture.md](architecture.md). It holds no data. It
is safe to delete while the program is not running; it is never deleted by the program.

## `log.txt`

Off by default. Written only when `--log` is passed or `NOUI_LOG` is set to anything
other than `0`.

- Append-only; one line per event, `YYYY-MM-DD HH:MM:SS  text`.
- Deleted (not trimmed) when it is found to be larger than 1 MB at the first write of a
  process.
- No cross-process lock: the window and the watcher can both append, and a line can be
  lost or awkwardly interleaved. This is a debugging tool, not an audit trail.

## `selftest.txt`

Written by `--selftest` next to the state file: what the fixture run did and
`RESULT: ok` or `RESULT: mismatch`. The exit code carries the same verdict.

## Crash files

Written by the unhandled-exception filter, independently of logging:

| File | Content |
|---|---|
| `crash-YYYYMMDD-HHMMSS.dmp` | a minidump of the faulting process |
| `crash.txt` | exception code and the faulting address |

They land in the data directory. In portable mode this is still
`%LOCALAPPDATA%\NoUninstallIcon\` — the dump path does not follow the portable marker;
that is a known inconsistency, not a feature.

## What is not stored anywhere

- Window size, position and scroll offset.
- The search text.
- Which row had focus.
- The theme (it follows Windows).

The window always opens at its default size, with an empty search box.

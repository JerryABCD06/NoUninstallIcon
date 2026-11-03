# Building, versioning and packaging

## Requirements

- MSVC with the C++ desktop workload (VS 2022 or newer).
- The Windows 10/11 SDK.
- No NuGet packages, no vcpkg, no third-party libraries of any kind.
- Inno Setup 6 or newer, only for the installer.

The C runtime is linked statically (`/MT`), so the executable has no redistributable
dependency.

## Build

```bat
build.bat            :: release into build\
build.bat debug      :: debug build
build.bat sign       :: release, then sign build\NoUninstallIcon.exe
```

`build.bat` also runs `tools\check-lang.ps1`, which compares every language file's key
set with `en.lang`.

Output: `build\NoUninstallIcon.exe`, about 560 KB.

## Version

The version lives in four places and they must agree:

| Where | What |
|---|---|
| `res/app.rc` | `FILEVERSION` / `PRODUCTVERSION` numbers, and the `FileVersion` / `ProductVersion` strings |
| `res/app.manifest` | the `assemblyIdentity` version, embedded in every binary |
| `installer/setup.iss` | `#define AppVersion`, which also names the setup file |
| `tools/package.ps1` | the artefact names it expects to produce and verify |

Current version: **0.1.0**.

## The display name

`FileDescription` and `ProductName` are the strings Task Manager and Explorer show, and
they are read from the binary by those programs — not through this program's own
language setting. They are therefore fixed English, `No Uninstall Icon`, with spaces.
Everything that is an identifier rather than a display name keeps the unspaced form:
`NoUninstallIcon.exe`, window classes, mutex and section names, the registry key, the
task name.

## Signing

Signing is a **local test facility**, not part of the release.

- `build.bat sign` signs `build\NoUninstallIcon.exe` with `.signing\NoUninstallIcon.pfx`
  if that file exists. `.signing\` is git-ignored.
- A release build is **unsigned on purpose**. A self-signed binary carries an untrusted
  publisher; SmartScreen treats it no better than an unsigned one, and some users trust
  it less.
- `tools\package.ps1` refuses to package a build that is already signed, and verifies
  that both artefacts came out unsigned.

## Packaging

```powershell
powershell -File tools\package.ps1          # unsigned release
powershell -File tools\package.ps1 -Sign    # test build, signed
```

It compiles the installer, builds the portable zip, and verifies the signatures:

```
[package] ok (unsigned): exe 562 KB, setup 2294 KB, zip 287 KB
```

Output:

| Artefact | Content |
|---|---|
| `dist\NoUninstallIcon-Setup-0.1.0.exe` | installer, including the `--cleanup` hook |
| `dist\NoUninstallIcon-portable-0.1.0.zip` | `NoUninstallIcon.exe`, `Reset.cmd`, `README.md`, `docs\`, `LICENSE`, `portable` |

## The installer

Inno Setup, `installer/setup.iss`. It installs to `{autopf}\NoUninstallIcon`, offers a
desktop shortcut, and creates the Start menu entry.

Four things about it are worth knowing:

**The wizard follows the system theme.** `WizardStyle=modern dynamic`; Inno Setup
documents the three appearance modes as `light`, `dark` and `dynamic` ("automatically
follows Windows system settings"). Change it to `modern light` or `modern dark` to force
one.

**It ships every language the program has.** Eleven `[Languages]` entries pointing at the
compiler's own `.isl` files: English, Simplified and Traditional Chinese, Japanese,
German, French, Spanish, Russian, Korean, Brazilian Portuguese and Italian. The language
dialog is shown on start-up when more than one language is available, and the choice is
remembered between runs.

**The product name is localized.** `AppName` is a custom message, `{cm:AppName}`, whose
value per language is the same string as that language's `app.title` — so the installer
and the program agree word for word. The expansion happens in the language chosen at
install time, and the result is what lands in the Start menu folder, the shortcut names,
and the Apps & Features entry. The two constraints that come with this:

- `AppId` is a fixed GUID. If it were left to default from `AppName`, the identity would
  change with the language and the installer would treat each language as a different
  product.
- The installation directory stays `{autopf}\NoUninstallIcon`. A localized directory name
  would move an upgrade to a second folder when the language changes.

**It removes the uninstall shortcut an older version left behind.** Deleting the `[Icons]`
entry does not delete a shortcut that a previous installer created: an upgrade installs
over the old one and leaves it in place. `CurStepChanged(ssPostInstall)` therefore walks
`{group}` and deletes any `.lnk` whose target is this installation's `unins000.exe` — the
application shortcut points at the exe and survives. Tested by installing a build that had
the shortcut and upgrading to one that does not. Three limits are worth knowing: it needs
`WScript.Shell` to read a shortcut's target and stays silent (logged) if that is
unavailable; it only looks at `{group}`, which Inno points at the *previous* group on
upgrade, so a language change is still covered; and an older install in a different Start
menu folder is out of reach.
There is deliberately **no uninstall shortcut** in the Start menu. A program whose whole
purpose is to remove "卸载 …" entries from that menu should not add one; the product is
removed through *Settings → Apps → Installed apps*, or by running `unins000.exe` from the
installation folder.

Uninstall order matters and is implemented in `[Code]`, not `[UninstallRun]`:

1. `CurUninstallStepChanged(usUninstall)` runs **before** `[UninstallDelete]`.
2. It executes `NoUninstallIcon.exe --cleanup` hidden and waits for it.
3. A non-zero exit is logged, and shown to the user unless the uninstall is silent.
4. Only then are the files deleted.

`--cleanup` itself closes an open window, records the Off mode, publishes it, waits for
the watcher, removes the sign-in task, and restores every managed shortcut. If the
executable is missing the installer cannot clean up at all and says nothing; that case
is a known gap.

## Language files

`res/lang/*.lang`, one file per language, compiled into the binary as `RCDATA`
resources:

| Resource id | Content |
|---|---|
| 100 | `map.txt`, the system-locale → language table |
| 101–111 | the language files |

Format: UTF-8 with a BOM, `key=value` per line, `#` for comments. The first line is
`lang.native_name`, which is what the picker shows; `lang.code` is the identifier used
by `state.dat`.

Adding a language means adding a file, assigning the next resource id in `res/app.rc`,
and adding it to the `map.txt` table if it should be picked automatically. The build
fails the parity check if a key is missing.

Placeholders are `{0}`, `{1}` and are substituted with a simple formatter; there is no
plural handling and no right-to-left layout support.

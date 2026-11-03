# Win32 notes

The details that took the longest to get right, and why the code looks the way it does.
Everything here is something that failed at least once during development.

## The window

- A single top-level `WS_POPUP` window with `WS_EX_NOREDIRECTIONBITMAP`. There are no
  child HWNDs: the list, search box, buttons, dropdowns and dialogs are all drawn by
  hand and hit-tested by rectangle.
- Rendering is D3D11 → DXGI swap chain → Direct2D device context, composed with
  DirectComposition (`DCompositionCreateDevice`, `CreateTargetForHwnd`).
- Mica is requested through `DwmSetWindowAttribute`; when the system refuses it the
  window simply paints its own background. Nothing in the layout depends on it.
- The process is `PerMonitorV2` DPI aware. `WM_DPICHANGED` recomputes the scale factor;
  all layout is in DIPs and multiplied by `scale_` only at the edge.
- Text is DirectWrite. Caret position, selection and hit testing in the search box are
  computed from the text layout, not from character counts, so proportional and complex
  scripts behave.
- IME: `WM_IME_STARTCOMPOSITION` / `COMPOSITION` / `ENDCOMPOSITION` are handled and the
  composition string is drawn at the caret. `WM_IME_CHAR` is swallowed, because the
  result already arrives through the composition message.

## Popup menus and dark mode

The five popup menus — the row context menu, the search box menu, the language list, the
autostart list (four in the window process) and the tray menu (in the watcher) — are plain
Win32 popup menus created with `CreatePopupMenu` and shown with `TrackPopupMenu`. They are drawn by the **theme engine**,
not by Direct2D, and `DWMWA_USE_IMMERSIVE_DARK_MODE` only affects the window frame. Out of
the box they would stay light in dark mode.

`ApplyNativeDarkMode(window, dark)` in `util.cpp` fixes that with the three uxtheme entry
points that the shell itself uses:

| Entry point | How it is reached | What it does |
|---|---|---|
| `SetPreferredAppMode(AllowDark)` | ordinal **135** | tells the theme engine this process follows the system app mode |
| `AllowDarkModeForWindow(hwnd, dark)` | ordinal **133** | opts one window in |
| `FlushMenuThemes()` | ordinal **136** | drops the cached menu theme so the next menu is drawn in the new mode |
| `SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr)` | documented function, undocumented theme name | switches that window's theme |

They are unnamed exports, so they can only be resolved by ordinal — which is why the
code degrades instead of failing: if `uxtheme.dll` does not export them, the pointers stay
null and the menus keep their light appearance. Nothing else breaks.

Two conditions are checked before resolving anything:

- **The build number.** These entry points first appeared in Windows 10 1809 (17763).
  Earlier builds have the same ordinal slots occupied by **different functions**, and
  `GetProcAddress` returns a pointer for whatever is there, so the "is it null" test
  would not protect anything. `DarkModeMenuApiAvailable()` reads the real build through
  `RtlGetVersion` and returns false below 17763, leaving the menus light.
- **High contrast is deliberately not checked.** Nothing else in the interface honours
  high-contrast colours — the window paints from its own light/dark palette — so a
  high-contrast menu would be the only element that did. If the window ever grows
  high-contrast support, add the `SPI_GETHIGHCONTRAST` test here at the same time.

The window applies this from `ApplyTheme()`, which already runs on `WM_SETTINGCHANGE`, so
flipping the system theme at run time also changes the menus. The watcher calls it from
`tray::ShowMenu` because it has no window of its own that would run a theme pass.

`AllowDark` rather than `ForceDark` is deliberate: the program's palette comes from the
same `AppsUseLightTheme` value, so letting the theme engine read it too keeps the menus
and the window from disagreeing.

## Hit testing, since there are no child windows

Every control is a rectangle computed by `ComputeLayout()`. The order of the tests in
`OnLeftDown` is load-bearing:

1. the About overlay, when open — it swallows everything;
2. the toast, including its *Undo* button — a click on the toast must not reach the list;
3. the search box and the toolbar controls;
4. the scrollbar, **before** the rows, because `RowAtPoint` ignores x — testing rows first
   meant that dragging the thumb over a row toggled that row;
5. group headers, which are overlays: a click anywhere in one is consumed;
6. rows.

The group header is *sticky*: once it scrolls past the top of the list it is painted at
a fixed position. That painted band is not where the header's content position says it
is, so the hit test has to compute the same band, and `EnsureVisible` has to leave the
same band clear or the focused row ends up half behind it.

## The tray icon

The single most error-prone part of the program. What matters:

- The icon window is a **real, never-shown top-level window**
  (`WS_POPUP | WS_EX_TOOLWINDOW`). A message-only window looks like the obvious choice
  and does not work: the shell cannot deliver clicks to it.
- `NOTIFYICONDATAW` uses `uID = 1` and `uCallbackMessage = WM_APP + 10`, which is above
  `WM_USER` and therefore needs `ChangeWindowMessageFilterEx` to pass the integrity
  boundary when the watcher runs elevated. The legacy `ChangeWindowMessageFilter` is
  called too, for older shells.
- After every `NIM_ADD` the icon switches to **`NOTIFYICON_VERSION_4`**. That single
  call changes the protocol: in version 4 a right click arrives as **`WM_CONTEXTMENU`
  in the low word of the callback's `lParam`**, with the anchor point packed into
  `wParam`. Code that watches only `WM_RBUTTONUP` never sees the click at all, and that
  is exactly the bug that kept the menu from opening.
- The shell reports both `WM_RBUTTONUP` and `WM_CONTEXTMENU` for one physical click, so
  the menu is opened on `WM_CONTEXTMENU` when version 4 was accepted and on
  `WM_RBUTTONUP` otherwise. Opening it on both nested the menu into itself.
- `TaskbarCreated` is the shell telling every listener that Explorer restarted and the
  icon is gone. Re-adding unconditionally would also bring back an icon that Silent mode
  must not show, so the handler checks the desired visibility first.
- After `TrackPopupMenu` the window posts itself `WM_NULL`: without it the menu can leave
  the window in a state where the next click is not delivered.

## Integrity levels

The window relaunches itself elevated once, and the sign-in task starts the watcher at
the highest available level. That creates two classes of problem:

**Messages from a lower-integrity process do not reach a higher one.** The shell runs at
medium; the watcher's tray window runs high. `ChangeWindowMessageFilterEx` is the
sanctioned way to allow specific messages through. The same rule means a medium-integrity
`--cleanup` cannot post `WM_CLOSE` to an elevated window — the code reports that case
rather than pretending it closed the window.

**Named objects are not shared across integrity levels by default.** A section or event
created by an elevated process may be unopenable by a medium one. The session objects are
created with an explicit descriptor and a low mandatory label so that both sides can
attach; without it the uninstaller's "Off" publish failed silently.

## Accessibility

A UI Automation provider is implemented by hand (`src/a11y.cpp`): a fragment root, a
small tree of elements, `IRawElementProviderSimple` / `IInvokeProvider`-style behaviour
for the buttons, and focus events. The list publishes one element per visible row plus
the containers. Announcements for state changes go through `Notify` and are what a
screen reader hears after a hide or unhide.

Known gaps: the search field exposes no `ValuePattern`, so a screen reader cannot read
its contents; the toast is not in the tree at all.

## The scheduled task

Registered through the Task Scheduler COM API, not `schtasks`.

| Setting | Value | Why |
|---|---|---|
| principal | current user, `TASK_LOGON_INTERACTIVE_TOKEN` | the process must run in the user's session to own a tray icon |
| run level | `TASK_RUNLEVEL_HIGHEST` | the all-users Start menu needs elevation |
| trigger | logon, 20 s delay | Explorer may not be up yet, and the icon needs the shell |
| execution time limit | `PT0S` (unlimited) for watchers, `PT10M` for `--apply` | the default three-day limit silently kills a resident watcher |
| multiple instances | `TASK_INSTANCES_IGNORE_NEW` | one watcher |
| battery | do not stop on battery, do not require AC | a resident tool should not depend on the power state |
| start when available | yes | a missed logon trigger still runs |

Because the principal is an interactive token with the highest run level, the task
starts elevated **without a UAC prompt**. The consent happened when the task was
registered, which itself requires administrator rights.

## Elevation of the window

`ShellExecuteEx` with the `runas` verb, once, at start-up. The manifest is
`asInvoker` — the elevation is explicit, not a manifest requirement, so command-line
modes can run without a prompt. `NOUI_NO_ELEVATE=1` in the environment skips the
relaunch; it exists for testing and is not documented as a user feature.

When the relaunch happens, the original command line is not reconstructed: only
`--log` is forwarded. `--about` and `--theme` are therefore lost on an elevated start.
That is a known limitation, not an accident of the current code.

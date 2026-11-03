# Rendering and the list

How the window is drawn, why it is drawn by hand, and how the list stays cheap with a few
hundred entries.

This is the layer below [architecture.md](architecture.md): that document explains what
the program does, this one explains how a frame reaches the screen.

## Layers

```
input  →  MainWindow (layout, hit testing, state)
       →  Draw(): header, toolbar, list, footer, focus ring, toast, About
       →  Painter (filled rects, text, lines, clipping) on an ID2D1RenderTarget
       →  ID2D1DeviceContext  (the window's swap chain, or an off-screen target)
       →  ID3D11Device        (BGRA support; hardware, WARP as a fallback)
       →  DXGI flip-sequential swap chain for composition
       →  DirectComposition visual attached to the window
       →  DWM composites it over the Mica backdrop
```

There are no child HWNDs anywhere. Every control — checkbox, search box, dropdown,
scrollbar, toast, About card — is a rectangle computed by `ComputeLayout()` and drawn by
hand. That is what makes the window able to be one translucent Mica surface instead of a
grid of grey child windows, and it is also why all the hit testing is explicit.

## The device stack

| Layer | Detail |
|---|---|
| D3D11 | `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, feature level 11.1 then 11.0 then 10.1; if hardware creation fails the device is created on WARP, so the window still works in a VM or with a broken driver |
| Swap chain | `CreateSwapChainForComposition`, `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, two buffers, `B8G8R8A8_UNORM`, **premultiplied alpha** |
| D2D | `ID2D1DeviceContext` on the swap chain's back buffer; `D2D1_UNIT_MODE_DIPS`; per-primitive antialiasing; **grayscale** text antialiasing, because ClearType cannot be composited onto a translucent surface |
| DComp | The swap chain is set as a visual's content, and the visual is the root of a composition target created for the window |

`GfxBegin` clears to **transparent**, not to a background colour: what shows through is
whatever DWM put behind the window — the Mica backdrop when the system supports it, and
the desktop otherwise. Where a region must be opaque (a sticky group header, the About
card, the toast) the drawing code fills it with an opaque colour.

`GfxResize` releases the D2D target bitmap, calls `ResizeBuffers`, and creates a new
target from the new back buffer. Forgetting to release the old target before resizing is
the classic way to make `ResizeBuffers` fail with `DXGI_ERROR_INVALID_CALL`.

`Present(1, 0)` is used, so the frame is paced by the monitor rather than being queued.

## Mica

Three DWM attributes are set when the window is created:

| Attribute | Value |
|---|---|
| `DWMWA_USE_IMMERSIVE_DARK_MODE` | follows the system light/dark setting |
| `DWMWA_WINDOW_CORNER_PREFERENCE` | rounded corners |
| `DWMWA_SYSTEMBACKDROP_TYPE` | `DWMSBT_MAINWINDOW` — the Mica material |

All three are best effort: on Windows 10 the calls fail and the window draws on whatever
the desktop shows through the transparent clear. Nothing in the layout depends on Mica.

## The painter

`Painter` is a thin wrapper over an `ID2D1RenderTarget`:

- **Text formats are cached** by family, size (in tenths of a DIP) and weight. The cache
  is a `std::map`, and `Format()` returns a borrowed pointer owned by the painter.
- **Brushes are cached** by packed RGBA. Every `FillRect`, `Line`, `Circle` and `Text`
  call takes a colour, not a brush; the painter finds or creates the brush.
- `Hairline()` returns `1 / scale`, so a "one pixel" line really is one pixel at any DPI.
- **Clipping is a stack**: `PushClip` / `PopClip` must be balanced to zero. The list
  pushes the list rectangle, draws, and pops.
- Fonts: `Segoe UI Variable Text` for body, `Segoe UI Variable Display` for the title,
  `Segoe Fluent Icons` for glyphs (search, clear, refresh, check, globe, info). The
  system font fallback is attached to every text format, so a language the font lacks
  still renders.

## Layout

All geometry is in DIPs (`1/96 inch`); the only place `scale_` appears is at the edge,
when a rectangle is converted to pixels or to a point. The constants that define the
window:

| Constant | Value | What it is |
|---|---|---|
| `kRowHeight` | 48 | one shortcut row |
| `kHeaderHeight` | 34 | a group header, pinned or not |
| — | 10 | the gap between groups |
| `kToolbarTop` / `kToolbarHeight` | 80 / 36 | the search row |
| `kFooterHeight` | 46 | the status bar |
| `kCheckLeft` / `kCheckSize` | 28 / 20 | the checkbox |
| `kIconLeft` / `kTextLeft` | 64 / 96 | the icon and the text column |
| minimum window | 820 × 560 DIPs | enforced in `WM_GETMINMAXINFO` |

`ComputeLayout()` is called at the start of drawing and at the start of hit testing, so
both always agree on where a control is.

## The display list

The list is not built from the model on every frame. `RebuildDisplay()` converts
`model_->rows` into a flat `entries_` vector:

```
Entry { bool header; int value; float top; float height; }
```

`value` is the group number for a header, or the **row index** for a row. `top` is the
absolute offset in DIPs from the top of the content, and `height` is 34 or 48. A header
is inserted whenever the group changes, plus a 10 DIP gap between groups. The total is
cached as `contentHeightCache_`, which is what the scrollbar and every clamp use.

`RebuildDisplay` runs when the model changes — after a scan finishes, after a filter
keystroke, after a row is hidden — not per frame.

## Drawing the list

`DrawList` walks `entries_` in order and stops early:

```cpp
for (const Entry& entry : entries_) {
    float top = layout.list.top + entry.top - scroll_;
    if (top + entry.height < layout.list.top) continue;   // above the view
    if (top > layout.list.bottom) break;                  // below the view: done
    ...
}
```

So the cost is proportional to the visible rows, not to the number of shortcuts. The
skipped-prefix scan is linear, which matters not at all for a few hundred entries and is
the price of not maintaining an index from scroll offset to first visible entry.

Everything else in the list is derived from the same offsets: `RowAtPoint` (which does
not care about x), `EnsureVisible`, the page keys, and the sticky header, which is drawn
after the rows and paints an opaque band over the row that has scrolled under it.

## Icons

Shell icons are slow to obtain, so they are fetched off the UI thread:

1. The UI thread asks `IconFor(path)`; a miss puts the path on a queue and returns null.
2. `IconWorkerMain` pops the queue, calls `SHGetFileInfo` with `SHGFI_ICON`, converts the
   `HICON` to a 32-bit premultiplied BGRA `IWICBitmap`, stores it under a mutex, and posts
   `WM_APP_ICON_READY` to the window.
3. The window copies the finished bitmaps into `ID2D1Bitmap`s on the UI thread — D2D
   resources are not thread safe — and repaints.

The cache is keyed by shortcut path and lives for the process. The worker thread is
started when the window is created and joined in `WM_DESTROY`; leaving it running past
destruction would post to a dead HWND.

Off-screen rendering (`--screenshot`) has no worker, so `IconFor` extracts the icon
inline when the painter targets an off-screen bitmap. Otherwise every screenshot would
be missing its icons.

## Text and the search box

The search text is a `std::wstring`; the caret is a character index into it. There is no
cached text layout: click-to-place and selection dragging build one for the current text
and hit-test it with `HitTestPoint`, which is exact — kerning included — and constant
time. Measuring every prefix, the obvious alternative, was O(n) layouts per mouse move.
`Home` and `End` set the index directly.

The composition string for an IME is drawn at the caret, and `WM_IME_CHAR` is ignored
because the result already arrives through `WM_IME_COMPOSITION`.

## Focus and hover

Three pieces of transient state, none of them persisted:

| State | Meaning | Drawn as |
|---|---|---|
| `focusRow_` | the row the keyboard acts on | focus ring around the row |
| `hotRow_` | the row under the pointer | subtle fill |
| `hotControl_` | the toolbar control under the pointer | subtle fill |

They are independent: moving the mouse does not move the keyboard focus, and pressing a
key does not fabricate a hover. Hover is cleared on `WM_MOUSELEAVE`, and a click on a
group header clears `focusRow_` rather than leaving a row selected behind the header.

## Accessibility publishing

`PublishAccessibility(layout)` is called from `Draw()`, after the list has been drawn and
before the toast and the About card. That is deliberate: the tree is published from the
same rectangles that were just painted, so what a screen reader reports and what is on
screen cannot disagree. The cost is that the tree is rebuilt per frame; for a list of a
few dozen visible rows that is cheaper than keeping it in sync incrementally.

## Off-screen rendering

`RenderToPng` creates its own WIC bitmap render target at a fixed size and DPI and runs
the same `Draw()` through it. It exercises the real drawing and layout code without a
window, a device, or a desktop — which is why the screenshots in this repository are a
rendering regression test as much as documentation.

`--capture` is different: it shows the real window and copies its rectangle off the
screen with `BitBlt`, so it exercises the swap chain, DirectComposition and DWM as well.

## Performance notes

- One `Present` per repaint, paced at the monitor's refresh.
- Nothing animates. The only periodic work is the search caret's blink — a 530 ms timer
  that runs while the search box has focus and invalidates the window. The toast uses a
  one-shot 6 s timer, and a 1 s timer announces the search text to a screen reader after
  typing stops.
- Text formats and brushes are cached, and `entries_` is only rebuilt when the model
  changes.
- A full Start menu of ~350 entries costs one scan (on a worker thread) and a display
  list of ~360 entries; drawing touches only the ~15 that are visible.

# InferenceOS GUI Architecture

The InferenceOS graphical interface is a layered software-rendered desktop over the same kernel
services and VFS namespace as the standalone CUI. GUI state does not participate in filesystem
layout or durability decisions. If graphics startup or a required GUI module fails, the system
keeps the CUI usable as the recovery surface.

## Reference display and dependency direction

The qualified reference profile requests 1024x768 BGRX8888 GOP through OVMF on QEMU standard VGA.
The framebuffer abstraction preserves the firmware-provided stride. Client surfaces and the shadow
buffer use 32-bit pixels; rendering is clipped before it reaches the target.

```text
Desktop / GUI terminal / File Explorer
                  |
          application models
                  |
      retained window manager
                  |
      2D primitives and PSF2 text
                  |
       graphics surface abstraction
                  |
        GOP framebuffer from UEFI
```

Input follows a separate shared path:

```text
PS/2 keyboard and mouse
          |
normalized version-1 input events
          |
standalone CUI or focused GUI consumer
```

No filesystem or VFS implementation may depend on graphics, windows, icons, coordinates, colors,
or GUI state. GUI, Shell, and application code likewise may not access raw block or InferenceOS-FS
structures.

## Graphics and text

A graphics surface records a pixel pointer, width, height, and stride. The version-1 renderer
provides clipped pixel writes, filled rectangles, lines, borders, a software pointer, and raster
text. Text uses a validated 8x16 PSF2 bitmap font. Invalid dimensions, overflow, malformed font
headers, unsupported glyph geometry, and out-of-range operations fail or clip without writing
outside the surface.

Rendering uses a framebuffer-sized shadow surface. Clients draw into owned surfaces and invalidate
changed regions; the compositor copies the resulting dirty region to the physical framebuffer.

## Input event ABI

Normalized input events are size- and version-prefixed and carry:

- key, pointer-move, or pointer-button event type;
- timestamp ticks;
- pressed, repeat, Shift, Control, Alt, and Caps Lock flags;
- key code or pointer button;
- absolute pointer position and relative movement; and
- printable text when a key maps to a character.

The shared queue holds 128 events. Pointer motion is clamped to the configured display bounds, and
overflow is counted rather than corrupting the queue. Supported special keys include Backspace,
Enter, Escape, arrows, modifiers, and Caps Lock. Pointer buttons are left, right, and middle.

## Window manager and compositor

The retained window manager supports up to 16 windows. Each window has a generation-checked handle,
owner identity, client surface, screen bounds, visibility, focus, and z-order. It provides create,
destroy, move, show/hide, raise, invalidate, owner-checked render, and compose operations.

The compositor:

- clips windows and invalidations to the screen;
- paints the desktop background and visible windows in z-order;
- tracks focus on the highest visible window;
- merges invalidations into a bounded dirty rectangle;
- draws the software pointer last; and
- copies only the dirty area from shadow memory to the GOP framebuffer.

Rendering a window through the Shell GUI-view service additionally validates that the window and
view handles belong to the calling process and application, require the expected rights, refer to
the same target, and use a strictly increasing render sequence.

## Desktop and GUI lifecycle

The `gui` command asks the Shell runtime to start the desktop and terminal modules in order:

1. confirm the CUI is usable and no GUI is already running;
2. confirm the desktop and terminal modules are available;
3. open and validate the GOP framebuffer;
4. validate the PSF2 font;
5. start the desktop module and window manager;
6. start the terminal module and its window; and
7. compose the first frame and report GUI ready.

Failure unwinds any modules and windows already started. Teardown destroys the terminal window,
stops terminal and desktop modules in reverse order, hides the pointer, and returns input/control to
the independently usable CUI. The current diagnostic distinguishes missing modules, framebuffer,
font, desktop, terminal, and composition failures.

## GUI terminal

The terminal is a client surface inside a managed window. It uses the same command registry,
parser, handlers, and command context as the standalone console. Printable key-press events feed the
shared CUI console; non-key and key-release events are ignored. Text wraps to the next row, scrolls
by one font row at the bottom, and supports the console's clear sequence.

Consequently, the GUI terminal and standalone CUI have the same path, storage, diagnostic, and
durability semantics. They are two presentations of one command engine, not separate shells.

## File Explorer

File Explorer is split into independently testable layers:

| Layer | Responsibility |
|---|---|
| Shell client/provider | Request paged directory/type/search views and rendering through versioned Shell IPC; reconnect after Shell generation changes. |
| Model | Hold at most 64 display-safe entries, selection, current opaque directory handle, and 16 back-history entries. |
| Controller | Navigate directories, create a directory, remove an authorized empty directory, and refresh after mutations. |
| Window | Render rows, selection, extension-free names, and resolved type/folder/generic icons. |
| Properties | Copy only the selected display-safe name, object kind, size, generic attributes, opaque handle, and allowed operations. |
| Diagnostic inspector | With separate authority, show filesystem, file, hash, and bounded FAT pages. |

The ordinary model cannot represent extensions, extension hashes, canonical internal names,
companion records, FAT/cluster locations, or registry contents. Names such as `REPORT.TXT` are
received as `REPORT`. Colliding hidden names are deterministically labeled, for example `REPORT`
and `REPORT (2)`, without renaming either persistent object.

The icon catalog resolves opaque type/icon capabilities to text, image, application, folder, or
generic-file presentation. Unknown or absent file mappings use the generic icon; directories use
the folder icon. File Explorer never derives a type by parsing a hidden extension.

### Input behavior

- Left-click selects the row under the pointer.
- Up and Down move the selection within the visible model.
- Enter activates the selection; a directory with enumerate rights becomes the new view.
- Backspace navigates to the previous opaque directory handle.
- In the privileged diagnostic inspector, Left/Right changes pages and Escape closes it.

Properties and ordinary rendering remain display-safe even when the diagnostic inspector is
available. Opening the inspector is a distinct authorized action and does not widen the ordinary
DTO or application contract.

## Shell and storage boundaries

Directory, type, search, and GUI-render requests are sent to the trusted Shell service. Shell
validates the caller-supplied wire structure and kernel-supplied caller identity, then invokes the
kernel file-view or GUI-view service. The file-view service enumerates through VFS and converts
entries to display-safe replies. Shell and File Explorer do not parse filesystem directory records
or communicate with block devices.

The complete application-facing schema and authorization rules are in
[application contracts](applications.md). The on-disk and extension-hiding rules are in
[InferenceOS-FS](inferenceos-fs.md).

## Implementation and validation references

- [`graphics.h`](../src/gui/graphics/include/inferenceos/gui/graphics.h) and
  [`input.h`](../src/gui/input/include/inferenceos/gui/input.h) define rendering and input.
- [`window.h`](../src/gui/window/include/inferenceos/gui/window.h) defines window ownership and
  composition.
- [`bootstrap.c`](../src/shell/bootstrap.c), [`desktop.c`](../src/gui/desktop/desktop.c), and
  [`terminal.c`](../src/gui/terminal/terminal.c) implement startup, recovery, and the shared console.
- [`file_explorer.h`](../src/gui/file_explorer/include/inferenceos/gui/file_explorer.h) defines the
  File Explorer model, client, controller, view, properties, and diagnostic inspector.
- [`graphics_test.c`](../tests/unit/graphics_test.c),
  [`window_manager_test.c`](../tests/unit/window_manager_test.c),
  [`gui_runtime_test.c`](../tests/integration/gui_runtime_test.c), and File Explorer tests under
  [`tests`](../tests/) validate these layers.

The GUI components are compiled and host-tested, but the repository does not yet link the final
kernel and static GUI application ELFs required for a clean-checkout QEMU boot. See
[build instructions](build.md#package-reference-images) and [limitations](limitations.md).

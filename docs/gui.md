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
Desktop / File Explorer / filtered viewers
                    |
            application models
                  |
      retained window manager
                  |
    2D primitives and raster text
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
text. The boot UI uses a validated 12x24 JetBrains Mono-derived atlas with 4-bit antialias coverage,
real mixed-case glyphs, and per-channel blending. The legacy 8x16 PSF2 parser remains available for
compatibility. Invalid dimensions, overflow, malformed font headers, unsupported glyph geometry,
and out-of-range operations fail or clip without writing outside the surface.

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

The desktop reserves a red `X` button in the top-right corner. Clicking it
closes GUI mode and its managed windows, stops the desktop module, clears the framebuffer, and
restores a fresh CUI prompt. Pressing `Escape` provides the same transition when pointer input is
unavailable.

Rendering a window through the Shell GUI-view service additionally validates that the window and
view handles belong to the calling process and application, require the expected rights, refer to
the same target, and use a strictly increasing render sequence.

## Desktop and GUI lifecycle

The `gui` command asks the Shell runtime to start the graphical desktop:

1. confirm the CUI is usable and no GUI is already running;
2. confirm the desktop module is available;
3. open and validate the GOP framebuffer;
4. validate the alpha4 console font;
5. start the desktop module and window manager;
6. start the managed File Explorer and filtered viewer windows; and
7. compose the frame and report GUI ready.

Failure unwinds any modules and windows already started. Teardown destroys the managed windows,
stops the desktop module, hides the pointer, and returns input/control to the independently usable
CUI. The current diagnostic distinguishes missing modules, framebuffer, font, desktop, and
composition failures.

## Optional GUI terminal

The terminal component remains available for explicit configurations and tests, but the production
GUI does not launch it automatically. GUI mode therefore opens without a command-prompt window.
When explicitly enabled, the terminal is a managed client surface using the same command registry,
parser, handlers, and command context as the standalone console.

The standalone CUI remains the recovery and administration surface before GUI startup and after GUI
shutdown.

## File Explorer

File Explorer is split into independently testable layers:

| Layer | Responsibility |
|---|---|
| Shell client/provider | Request paged directory/type/search views and rendering through versioned Shell IPC; reconnect after Shell generation changes. |
| Model | Hold at most 64 display-safe entries, selection, current opaque directory handle, and 16 back-history entries. |
| Controller | Navigate directories, create a directory, remove an authorized empty directory, and refresh after mutations. |
| Window | Render a scrollable icon grid, selection, extension-free names, and resolved type/folder/generic icons. |
| Properties | Copy only the selected display-safe name, object kind, size, generic attributes, opaque handle, and allowed operations. |
| Diagnostic inspector | With separate authority, show filesystem, file, hash, and bounded FAT pages. |

The ordinary model cannot represent extensions, extension hashes, canonical internal names,
companion records, FAT/cluster locations, or registry contents. Names such as `REPORT.TXT` are
received as `REPORT`. Colliding hidden names are deterministically labeled, for example `REPORT`
and `REPORT (2)`, without renaming either persistent object.

The icon catalog resolves opaque type/icon capabilities to text, image, application, folder, or
generic-file presentation. Text files use a lined-page icon, images use a picture icon,
applications use an application tile, directories use a folder icon, and unknown mappings use a
generic page icon. File Explorer never derives a type by parsing a hidden extension.

### Input behavior

- Left-click selects the icon under the pointer; double-click activates it and opens a directory.
- Left and Right move between icons; Up and Down move between grid rows and scroll as needed.
- When File Explorer has focus, Enter activates the selection; a directory with enumerate rights
  becomes the new view and its files are rendered.
- Backspace navigates to the previous opaque directory handle.
- In the privileged diagnostic inspector, Left/Right changes pages and Escape closes it.

Properties and ordinary rendering remain display-safe even when the diagnostic inspector is
available. Opening the inspector is a distinct authorized action and does not widen the ordinary
DTO or application contract.

## DOC Viewer application

GUI mode also starts the optional `DOC Viewer` ring-3 application. Its managed window is titled
`DOC Files App` and occupies the lower-right desktop region below the TXT Viewer. The application asks
Shell for a paged `TYPE_VIEW` of the mounted root using the boot-generation opaque capability for
the authoritative DOC type. It never parses names or receives `.DOC`, extension bytes, or hashes.
The window opens as a File Explorer view and its header displays the current display-safe folder,
starting with `Folder: /`.

The returned model contains every navigable folder plus regular DOC files; regular files of other
types remain hidden. It supports selection, arrow-key scrolling, double-click or Enter to open a
folder, and Backspace to return. When no root filesystem is mounted, the application remains
available with an empty view rather than preventing GUI startup. The module is packaged separately
as `doc-viewer.elf` with application identity 4103.

## TXT Viewer application

GUI mode also starts the optional `TXT Viewer` ring-3 application. Its managed window is titled
`TXT Files App` and occupies the upper-right desktop region. File Explorer occupies the larger left
region, with more display area than both filtered viewer applications combined. TXT Viewer requests
a paged `TYPE_VIEW` of the mounted root using only the boot-generation opaque capability for the
authoritative TXT type. Its File Explorer view starts at `Folder: /` and updates the display-safe
current-folder label during navigation.

The returned grid contains every navigable folder plus regular TXT files, with other regular-file
types hidden. It supports the same selection, scrolling, double-click/Enter navigation, and
Backspace history as File Explorer. A missing root mount produces an empty view without preventing
GUI startup. The module is packaged separately as `txt-viewer.elf` with application identity 4104.

## Shell and storage boundaries

Directory, type, search, and GUI-render requests are sent to the trusted Shell service. Shell
validates the caller-supplied wire structure and kernel-supplied caller identity, then invokes the
kernel file-view or GUI-view service. The file-view service enumerates through VFS and converts
entries to display-safe replies. Shell and File Explorer do not parse filesystem directory records
or communicate with block devices.

For an InferenceOS-FS mount, the filesystem driver follows the validated FAT directory chain,
decodes healthy directory records, suppresses internal companion records, and supplies an internal
type identity and binary FNV-1a prefilter to VFS. For regular files in a `TYPE_VIEW`, the kernel
first compares the binary prefilter and then confirms the authoritative identity, preventing a hash
collision from returning the wrong type. Directories remain visible for navigation but carry no
file-type identity. The kernel converts file identities to opaque boot-scoped capabilities before
entries cross the Shell boundary. Thus the program reflects the FAT-backed namespace without
receiving raw FAT values, cluster locations, extensions, fingerprints, or hashes.

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

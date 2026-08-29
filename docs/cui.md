# InferenceOS CUI

The InferenceOS character user interface is the independent recovery, administration, and
diagnostic surface for the demonstrator. The standalone console owns command entry; an optional GUI
terminal component can use the same parser, registry, handlers, and command context in explicit
configurations, but it is not launched by the production GUI.

The prompt is:

```text
InferenceOS>
```

The command list is assembled at runtime. `help` reports the commands actually registered for the
current boot; commands whose backing service is unavailable fail without leaving the console.

## Command-line grammar

Version 1 accepts printable ASCII, Backspace, and Enter with a maximum 255-byte command payload and
at most 32 arguments. Arguments are separated by spaces. Double quotes group spaces into one
argument, and only `\"` and `\\` escapes are recognized inside quotes.

```text
write /DOCS/REPORT.TXT "persistent data"
append /DOCS/REPORT.TXT " and more"
```

Pipes, redirection, expansion, globbing, scripting, and command chaining are deliberately absent.
The characters `|`, `<`, `>`, `$`, `*`, `?`, `;`, and backtick are rejected. Unterminated quotes,
unsupported escapes, control characters, overly long input, and excess arguments are also rejected.

Errors have a stable console shape:

```text
error: <symbol>: <message>
```

A parse failure is reported with symbols such as `line_too_long`, `invalid_character`,
`invalid_syntax`, or `too_many_arguments`. Unknown commands use `command_not_found`; handler
failures identify their cause, such as `invalid_arguments`, `not_found`, `already_exists`,
`read_only`, or `not_empty`. After an error, the console clears the current line and prints a new
prompt. When a known command has invalid arguments, the next line prints its exact syntax. For
example, `fileinfo` alone reports `invalid_arguments` followed by `usage: fileinfo <path>`.

## Core and interface commands

| Command | Purpose |
|---|---|
| `help [command]` | List every registered command with its syntax, or show exact usage for one command. |
| `version` | Print the demonstrator version. |
| `clear` | Clear the active text console and return the cursor to the upper-left corner. |
| `gui` | Start the graphical desktop without opening a command-prompt window. It fails safely if the required desktop module, framebuffer, font, or composition is unavailable. |

`gui` does not replace the CUI. GUI startup failure leaves the standalone console usable, and GUI
teardown restores CUI ownership and records a diagnostic reason.

## Device and filesystem commands

Devices use stable command names such as `disk0`. Up to eight block devices can be registered with
the version-1 command context.

| Syntax | Purpose and important behavior |
|---|---|
| `devices` | List registered devices with status, logical sector size, sector count, and byte capacity. |
| `diskinfo diskN` | Show device geometry plus detected filesystem, classification state, and current mount state. A valid volume is reported as `filesystem=InferenceOS-FS`; blank media reports `filesystem=none`; partitioned, foreign, or unreadable media reports `filesystem=unknown` with a precise `filesystem_state`. |
| `format diskN` | Create InferenceOS-FS on an unmounted device. The device must use 512-byte sectors and meet the 50,000,000,000-byte minimum. |
| `mount diskN /` | Probe and mount the device as the one VFS root. Reports read-write, diagnostic-read-only, or rejected state. |
| `unmount /` | Refuse active operations, flush the device, detach the root, and invalidate the mount. |
| `fsinfo` | Report filesystem identity, format and mount state, capacity, geometry, free-space state, record sizes, hash algorithm, and registry state. |
| `sync` | Persist filesystem/cache generations and complete the required device flush. |
| `reboot` | Synchronize required storage state and request a restart. |
| `shutdown` | Synchronize required storage state and request a halt/power-off. |

`format` is destructive to the selected device and is rejected while a root filesystem is mounted.
`sync`, `unmount`, `reboot`, and `shutdown` report success only after every required write and flush
has succeeded. A failed flush is an error, not a successful durable operation.

The detailed version-1 disk format and mount classifications are in
[InferenceOS-FS](inferenceos-fs.md).

## File commands

| Syntax | Purpose |
|---|---|
| `create <path>` | Create an empty regular file. |
| `write <path> <text>` | Replace the file content with the supplied argument bytes. |
| `append <path> <text>` | Append the supplied argument bytes. |
| `type <path>` | Write file content to the console. |
| `rename <source> <destination>` | Rename a regular file; an extension change recomputes companion metadata before commit. |
| `delete <path>` | Delete a regular file through the ordered filesystem transaction. |
| `search <extension>` | Recursively list files with the exact extension. Accepts `DOC` or `.DOC` case-insensitively and prints extension-hidden absolute locations. |

Quoted input is needed when `write` or `append` content contains spaces. Version 1 does not provide
redirection, stdin streams, or binary escape syntax; these commands pass the parsed argument bytes
to the shared file-operation interface.

`search` sends the extension through the shell-facing kernel service; the CUI never receives the
computed hash or raw directory metadata. InferenceOS-FS uses the companion hash only to prefilter
healthy file pairs and then compares the authoritative canonical extension exactly. Results are
listed one location per line, `no matches` is printed for an empty result, and `results truncated`
is printed after the first 16 locations when more matches exist. Missing, additional, malformed,
or overlong extension arguments produce `usage: search <extension>` through the normal command
error path.

## Directory commands

| Syntax | Purpose |
|---|---|
| `dir [path]` | List the current directory or the supplied path using display-safe entries. |
| `cd <path>` | Change the calling console's current directory. |
| `pwd` | Print the current directory. |
| `mkdir <path>` | Create a directory. |
| `rmdir <path>` | Remove an empty directory. |

Absolute and relative paths are supported. `.` names the current directory, `..` names its parent,
and resolving `..` at `/` remains at `/`. Paths are limited to 255 bytes and 16 components.
Persistent names follow the version-1 uppercase 8.3 rules described in
[InferenceOS-FS](inferenceos-fs.md#names-and-primary-directory-records).

Ordinary `dir` output comes from the display-safe model. It shows extension-free names, object kind,
permitted size, and a generic read-only flag. It never exposes extensions, extension hashes,
companions, FAT chains, record locations, or registry entries. Hidden-name collisions remain
independently selectable through deterministic labels such as `REPORT` and `REPORT (2)`.

## Privileged diagnostic commands

These commands require a kernel-minted diagnostic capability bound to the caller. They are not an
ordinary file/application interface.

| Syntax | Diagnostic information |
|---|---|
| `fsinfo` | With diagnostic authority, adds authoritative free-space, layout, hash, mount, and registry health fields. |
| `fileinfo <path>` | Internal canonical name, object type, attributes, size, first cluster, and primary/companion locations. |
| `hashinfo <path>` | Canonical extension, FNV-1a hash values, companion version/commit state, checksum, CRC, and validation result. |
| `fatinfo <path>` | A bounded, validated cluster chain and end-of-chain state. |

Diagnostic reads remain bounded by validated filesystem geometry. A capability may restrict the
available diagnostic scopes; absent, stale, wrong-process, or insufficient authority is rejected.
Extension hiding is therefore an ordinary-interface contract, not encryption from privileged
diagnostics or raw-disk analysis.

## Example workflow

```text
devices
format disk0
mount disk0 /
mkdir /DOCS
create /DOCS/REPORT.TXT
write /DOCS/REPORT.TXT "persistent data"
dir /DOCS
type /DOCS/REPORT.TXT
sync
hashinfo /DOCS/REPORT.TXT
gui
shutdown
```

Ordinary `dir /DOCS` displays `REPORT`, not `REPORT.TXT`; `hashinfo` reveals the extension and hash
only because it is the explicit privileged diagnostic path. The standalone console and GUI
terminal observe the same mounted namespace.

## Implementation and validation references

- [`cui.h`](../src/cui/include/inferenceos/cui.h), [`parser.c`](../src/cui/parser.c), and
  [`console.c`](../src/cui/console.c) define the shared grammar and console.
- [`fs_commands.c`](../src/cui/fs_commands.c), [`file_commands.c`](../src/cui/file_commands.c),
  [`directory_commands.c`](../src/cui/directory_commands.c), and
  [`diagnostic_commands.c`](../src/cui/diagnostic_commands.c) define the command sets.
- [`cui_parser_test.c`](../tests/unit/cui_parser_test.c),
  [`fs_commands_test.c`](../tests/integration/fs_commands_test.c), and the system suites under
  [`tests/system`](../tests/system/) validate parser, filesystem, shared-namespace, and recovery
  behavior.

The repository currently compiles these runtime components and validates them with hosted tests,
but final kernel and application ELF link targets are not yet integrated. See
[build instructions](build.md#package-reference-images) and [limitations](limitations.md) before
interpreting the command workflow as a completed clean-checkout boot claim.

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
write /DOCS/REPORT "persistent data"
append /DOCS/REPORT " and more"
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
`unexpected_format`, `read_only`, or `not_empty`. After an error, the console clears the current line and prints a new
prompt. When a known command has invalid arguments, the next line prints its exact syntax. For
example, `fileinfo` alone reports `invalid_arguments` followed by
`usage: fileinfo <display-path>`.

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
| `write <display-path> <text>` | Initialize an empty regular file with supported text using the extension-hidden name shown by `dir`. |
| `append <display-path> <text>` | Append supported text to an empty or content-validated text file using the extension-hidden name shown by `dir`. |
| `cat <display-path>` | Display a content-validated text file using its extension-hidden name. |
| `type <path>` | Write file content to the console. |
| `rename <display-source> <display-destination>` | Rename or move a displayed regular file while preserving its hidden authoritative extension. |
| `delete <display-path>` | Delete the exact displayed regular file through the ordered filesystem transaction. |
| `search <extension>` | Recursively list files with the exact extension. Accepts `DOC` or `.DOC` case-insensitively and prints extension-hidden absolute locations. |

`write`, `append`, `cat`, `rename`, and `delete` resolve extension-hidden paths exactly as rendered by
`dir`, with ASCII case-insensitive matching consistent with canonical 8.3 names. Older or externally
produced disks may already contain a collision label; one containing
spaces must be quoted, for example
`write "/DOCS/REPORT (2)" "second"`. The labels are ranked by object identity for the current
complete directory view; after a separate namespace change, run `dir` again before using a numeric
label. New creates and renames reject any file or directory whose extension-hidden base already
exists, so normal CUI mutations cannot introduce another collision. The implementation never
guesses a hidden extension or bypasses the VFS.

Quoted input is also needed when `write` or `append` content contains spaces. Neither command uses
the hidden extension as an allowlist. `write` initializes an existing empty file; when the displayed
name does not exist, it creates an empty extensionless file at that path and initializes it in the
same command. If creation discovers that the displayed base already exists after an initial lookup
miss, `write` retries trusted resolution instead of returning `already_exists`. `append` accepts an
existing empty target or scans all existing bytes before mutation.
The current CUI text encoding
is printable ASCII plus tab, carriage return, and line feed; any other byte, or any attempt to
`write` a non-empty file, returns `unexpected_format` without mutation. Thus an empty file may be
initialized regardless of its hidden extension, while an existing image or other binary file is
rejected by content. Version 1 does not provide redirection, stdin streams, Unicode input, or binary
escape syntax.

`cat` applies the same content-based text policy without consulting a hidden extension or embedded
file-type allowlist. It validates the complete file before displaying content, so `cat REPORT`
can read `REPORT.TXT` or `REPORT.DOC`, while binary content returns `unexpected_format` without
printing a partial prefix. `cat` is read-only and never creates or modifies a file.

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
Relative `create` and `mkdir` operands are bound to the current directory's resolved VFS identity;
the filesystem does not re-resolve them from root. Consequently, files created after `cd /TEST`
are listed by `dir .` in `/TEST`, not by `dir /`.
Persistent names follow the version-1 uppercase 8.3 rules described in
[InferenceOS-FS](inferenceos-fs.md#names-and-primary-directory-records).

Ordinary `dir` output comes from the display-safe model. It shows extension-free names, object kind,
permitted size, and a generic read-only flag. It never exposes extensions, extension hashes,
companions, FAT chains, record locations, or registry entries. Legacy hidden-name collisions remain
independently selectable through deterministic labels such as `REPORT` and `REPORT (2)`, but new
filesystem mutations enforce a unique displayed base name within each directory.

## Privileged diagnostic commands

These commands require a kernel-minted diagnostic capability bound to the caller. They are not an
ordinary file/application interface.

| Syntax | Diagnostic information |
|---|---|
| `fsinfo` | With diagnostic authority, adds authoritative free-space, layout, hash, mount, and registry health fields. |
| `fileinfo <display-path>` | Resolve the displayed file, then show its internal canonical name, object type, attributes, size, first cluster, and primary/companion locations. |
| `hashinfo <display-or-canonical-path>` | Resolve a displayed name (with canonical-path fallback), then show its extension, FNV-1a hash values, companion version/commit state, checksum, CRC, and validation result. |
| `fatinfo <display-or-canonical-path>` | Resolve a displayed name (with canonical-path fallback), then show a bounded, validated cluster chain and end-of-chain state. |

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
write /DOCS/REPORT "persistent data"
append /DOCS/REPORT " and more"
cat /DOCS/REPORT
fileinfo /DOCS/REPORT
dir /DOCS
type /DOCS/REPORT.TXT
sync
hashinfo /DOCS/REPORT
fatinfo /DOCS/REPORT
gui
shutdown
```

Ordinary `dir /DOCS` displays `REPORT`, not `REPORT.TXT`. The eight display-oriented commands are
`write`, `append`, `cat`, `rename`, `delete`, `fileinfo`, `hashinfo`, and `fatinfo`; `create` and `type`
continue to accept their existing canonical typed paths. `hashinfo` and `fatinfo` also retain canonical-path
fallback for privileged diagnosis of files that cannot enter the healthy displayed view.
`hashinfo` reveals the extension and hash only because it is
an explicit privileged diagnostic path. The standalone console and GUI terminal observe the same
mounted namespace.

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

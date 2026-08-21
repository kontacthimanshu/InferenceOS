# Character Command Contract

## Prompt and Parsing

- Ready prompt: `InferenceOS>`.
- Printable ASCII, Backspace, Enter, and 255 characters plus terminator.
- ASCII-space tokenization with strict command arity; unsupported quoting is rejected. `write` and `append` treat the remainder after the path as text.
- Empty, unknown, and malformed input always return safely to the prompt with deterministic bounded output.

## Surface

| Commands | Contract |
|---|---|
| `help`, `version`, `clear` | Command/build identity and display control. |
| `devices`, `diskinfo` | Generic block-device discovery/status. |
| `format`, `mount`, `unmount`, `fsinfo`, `sync` | One-volume lifecycle and diagnostics. |
| `dir`, `cd`, `pwd`, `mkdir`, `rmdir` | Directory operations through VFS. |
| `create`, `write`, `append`, `type`, `rename`, `delete` | Regular-file operations through VFS. |
| `fileinfo`, `hashinfo`, `fatinfo` | Validated read-only filesystem diagnostics. |
| `reboot`, `shutdown` | Filesystem flush → cache/device flush → restart/halt. |

## Diagnostic Fields

- `diskinfo`: identity, sector size/count, capacity, status.
- `fsinfo`: signature/version, geometry, FAT/root/free counts, record sizes, algorithm, mount state.
- `fileinfo`: canonical name, type, attributes, size, first cluster, validated record locations.
- `hashinfo`: extension bytes/length, algorithm, stored/recomputed hash, version/commit, checksum/CRC/overall validity.
- `fatinfo`: bounded cluster sequence and EOC classification.

Commands never print unvalidated memory/disk bytes, continue corrupt traversal, or translate an error into success.

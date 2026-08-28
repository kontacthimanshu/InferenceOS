# CUI Contract: `search`

## Syntax

```text
search <extension>
```

Accepted examples: `search doc`, `search .DOC`, `search .DoC`.

## Output

- One absolute extension-hidden location per line.
- `no matches` when the successful reply is empty.
- `results truncated` after listed paths when more than 16 matches exist.
- No extension hash, stored hash, recomputed hash, companion field, or raw filename extension.

## Errors

Missing/additional arguments and invalid extension text return invalid arguments so the existing CUI usage renderer shows `usage: search <extension>`. An unavailable mount or corrupt traversal uses the existing status-to-CUI error rendering.

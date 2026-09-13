# Compiler warnings

The four WTL applications compile at `/W4` through `iw_flags`. That shared
configuration suppresses only warnings inherent to the retained framework and
language mode:

- `C4100`, unused parameters in fixed WTL handler signatures;
- `C4239`, legacy ATL temporary-reference binding accepted by `/permissive`;
- `C4131`, old-style declarations in vendored BSD regex C sources;
- `C4996`, while retained historical CRT calls are compiled deliberately.

ImageWalker 3.0 uses `/permissive-`, `/W4` and no suppressions.

## Policy

Conversions at Win32 boundaries use the target type named by the compiler.
Pointer-sized values are not narrowed, and C sources keep C-compatible casts.
Both Debug and Release are compiled during validation, so configuration-specific
diagnostics stay visible.

`dd.ps1` preserves the raw build log and groups diagnostics by warning code and
by source file, and prints that summary after every build. Header amplification
is judged by unique source locations, not by the number of translation units
repeating one diagnostic.

## Remaining diagnostics

The maintenance pass removed the non-portable `CString` variadic arguments,
assignment-in-condition warnings, the reported unused locals, and the tracked
conversion/shadowing families without adding suppressions. Boundary tests cover
JFIF density saturation, pixel-channel rounding, large quantizer moments and
print-range narrowing. Explicit `/EHsc` also restores destructor unwinding when
dependency configuration replaces CMake's cached defaults. Other compiler
diagnostics may remain; this is not a blanket warning-free guarantee.
Remaining work is tracked in [todo.md](todo.md). For the diagnostics emitted by
the current build, run `.\dd.ps1 test` and read its grouped summary; incremental
build totals cover only the translation units that were rebuilt.

# Compiler warnings

Compile the four WTL applications at `/W4` through `iw_flags`. Allow only these
framework and language-mode suppressions:

- `C4100`, unused parameters in fixed WTL handler signatures;
- `C4239`, legacy ATL temporary-reference binding accepted by `/permissive`;
- `C4131`, old-style declarations in vendored BSD regex C sources;
- `C4996`, while retained historical CRT calls are compiled deliberately.

Use `/permissive-`, `/W4` and no suppressions for ImageWalker 3.0.
Apply that same policy to modern `libiw` sources. Limit the library's legacy
warning exceptions to its native-page compatibility sources, not its consumers.

## Policy

Conversions at Win32 boundaries use the target type named by the compiler.
Pointer-sized values are not narrowed, and C sources keep C-compatible casts.
Both Debug and Release are compiled during validation, so configuration-specific
diagnostics stay visible.

`dd.ps1` preserves the raw build log and groups diagnostics by warning code and
by source file, and prints that summary after every build. Header amplification
is judged by unique source locations, not by the number of translation units
repeating one diagnostic.

## Validation

Pass explicit character pointers rather than `CString` objects to variadic
functions. Keep assignments out of conditions, remove unused locals, and fix
conversion and shadowing warnings without adding suppressions.

Test JFIF density saturation, pixel-channel rounding, large quantizer moments
and print-range narrowing at their boundaries. Preserve exception unwinding
in file-operation and worker recovery paths: use `/EHs` in the shared codec
backend, whose callbacks unwind through C library frames, and `/EHsc` in the
applications. Do not propagate the backend's compatibility flags to 3.0.

Run `.\dd.ps1 test` and read its grouped diagnostic summary; incremental totals
cover only the translation units rebuilt. Track follow-up work in
[todo.md](todo.md), and use a full Debug and Release rebuild before claiming
that a warning family is resolved.

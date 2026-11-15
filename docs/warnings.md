# Compiler warnings

The four WTL applications compile at `/W4` through `iw_flags`, which allows only
these framework and language-mode suppressions:

- `C4100`, unused parameters in fixed WTL handler signatures;
- `C4239`, the ATL temporary-reference binding that `/permissive` accepts;
- `C4131`, old-style C declarations, which no current source needs;
- `C4996`, the historical CRT calls compiled deliberately.

ImageWalker 3.0 and the modern shared imaging sources compile with
`/permissive-`, `/W4` and no suppressions. Keep the historical exceptions on
`iw_flags` and the legacy test sources that need them, and never extend them to
3.0 or the modern imaging sources.

## Policy

Fix a warning rather than suppressing it. Conversions at Win32 boundaries use
the target type the compiler names, pointer-sized values are never narrowed,
and C sources keep C-compatible casts. Pass explicit character pointers, not
`CString` objects, to variadic functions, keep assignments out of conditions,
and remove unused locals.

Test JFIF density saturation, pixel-channel rounding, large quantizer moments
and print-range narrowing at their boundaries.

## clang-tidy

`.clang-tidy` enables clang-tidy's bug-finding and performance checks, less
those that flag idioms used deliberately. Check ImageWalker 3.0 and the modern
shared code with it, never the preserved releases: configure a build tree with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and run Visual Studio's `clang-tidy`, in
`VC\Tools\Llvm\x64\bin`, with `-p` on that tree. Fix each finding; a
deliberate idiom keeps a `NOLINT` or `NOLINTNEXTLINE` comment that names the
check and gives the reason.

## Validation

After every build `dd.ps1` keeps the raw build log and prints a summary of its
diagnostics grouped by warning code and by source file. Judge header
amplification by unique source locations, not by the number of translation
units repeating one diagnostic. An incremental build reports only the
translation units it rebuilt, so rebuild Debug and Release in full before
calling a warning family resolved; `.\dd.ps1 test` compiles both, keeping
configuration-specific diagnostics visible.

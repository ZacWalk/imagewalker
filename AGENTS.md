# ImageWalker agent notes

[README.md](README.md) introduces the five applications - ImageWalker 1.0 to
2.3, which are preserved releases, and ImageWalker 3.0 - gives the build
commands and indexes every document.

## Read before changing

| Document | Read before |
|---|---|
| [docs/app-rules.md](docs/app-rules.md) | Any change to settings, logging, workers, resources or the 3.0 file tasks |
| [docs/porting.md](docs/porting.md) | Any change to the 1.0 SDK or 2.x WTL releases |
| [docs/architecture-2x.md](docs/architecture-2x.md) | Any change to ImageWalker 2.0, 2.2 or 2.3 |
| [docs/reliability.md](docs/reliability.md) | Any change to file writes, pixels, metadata or threading |
| [docs/imaging.md](docs/imaging.md) | Any change to shared image loading, codecs or manipulation |
| [docs/workflows-3.0.md](docs/workflows-3.0.md) | Any change to the ImageWalker 3.0 task views |
| [docs/build.md](docs/build.md) | Any change to CMake, presets, tests, help or dependencies |
| [docs/warnings.md](docs/warnings.md) | Fixing or suppressing a compiler warning or a clang-tidy finding |
| [docs/known-issues.md](docs/known-issues.md) | Fixing or finding anything a user of a release would notice |

## Rules

- Describe each release by its name, year and feature additions, never as a
  presentation of another version or as a snapshot of the code. Write
  development guidance as instructions, not as a record of changes.
- Never edit `dd.ps1` or `.dd/`: they are the vendored dd runtime, replaced
  only as a whole ([build.md](docs/build.md#the-dd-driver)). Project commands
  belong in `tools/dd-project.ps1` and project settings in `dd.psd1`.
- Format ImageWalker 3.0 and the modern shared code with `.clang-format`;
  never reformat what `.clang-format-ignore` lists, the preserved releases
  among it ([porting.md](docs/porting.md#formatting)).
- Use `.\dd.ps1 launch` for an interactive session; `run` kills the process
  after 120 seconds.
- Preview with `.\dd.ps1 clean --dry-run`, and delete only when deletion is
  explicitly authorised. Never delete `build/` wholesale.

## Validation

Validate a source edit with the narrowest relevant test, such as
`.\dd.ps1 test --app iw23 --label headless`, then run
`.\dd.ps1 test --label headless`. The unfiltered gate also opens visible
application smoke windows; reserve it for an explicit interactive check. Validate help edits with
`.\dd.ps1 check-help --yes`. [build.md](docs/build.md#tests) describes the
suites and the environment some of them need.

dd documents its
[two modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes) -
CLI, and MCP for an MCP client - and its
[commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands) in its
own repository; `.\dd.ps1 commands --json` lists ImageWalker's own.

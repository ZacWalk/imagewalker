# @NAME@

[![Build and test](https://github.com/OWNER/@NAME@/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/@NAME@/actions/workflows/ci.yml)

Replace `OWNER/@NAME@` in the badge URLs with this project's GitHub owner and
repository name after publishing it. The scaffold does not create a remote repository.

@SUMMARY@ using the dd build system.

Dependency URLs, exact Git commits or archive SHA-256 hashes and methods are recorded in
`cmake/dd-dependencies.json`. `dd dep install NAME` adds a declaration; CMake uses
FetchContent (default) or ExternalProject to fetch/build it under the ignored build
directory. Application-owned recipes can consume pins without automatic integration.

## Build and test

PowerShell 7.4+, Git, CMake 3.24+ and Ninja are required. Use MSVC with a Windows SDK
on Windows or GCC on Linux. GUI apps currently support Windows only.

```powershell
pwsh -NoProfile -File ./dd.ps1 dep install
pwsh -NoProfile -File ./dd.ps1 toolchain --dry-run
pwsh -NoProfile -File ./dd.ps1 test
```

If tools are missing, review the toolchain plan and install through your OS provider
or use `dd toolchain --yes` in a suitably privileged terminal.

## Debug in VS Code

Install the recommended Microsoft C/C++ extension and open this folder. Select the
matching **dd: Windows Debug** or **dd: Linux Debug** configuration in Run and Debug,
set a breakpoint, and press **F5**. The pre-launch task builds the Debug app through
the vendored driver before the debugger starts it. Edit the launch configuration's
`args` array to pass application arguments.

For WSL, open the folder through VS Code's WSL extension and install the C/C++ extension
in WSL. Native Linux debugging requires GDB (`sudo apt-get install gdb` on Ubuntu).
The build system does not install the debugger or editor extensions automatically.

Both compiler settings and IntelliSense use C++20. The Debug build generates
`compile_commands.json` for include paths and compiler definitions. Reconfigure the
launch paths and manifest together if the target's output path changes.

## Continuous integration

GitHub Actions validates dependency declarations, builds Debug and Release and runs
CTest. GUI apps also run the configured Windows window smoke tests. CLI CI runs on
Windows and Ubuntu; GUI CI runs on Windows only.

## Project commands and targets

Add project-owned PowerShell commands in the optional `commands` hashtable in
`dd.psd1`; discover them with `dd commands --json` or `dd help NAME`. Scripts accept
a JSON request on stdin and return schema 1 JSON (`data` and `files`) on stdout.
Write commands require `--yes`; preview support must be implemented by the script.
Built-in command names cannot be replaced. See the dd repository's
[extension guide](https://github.com/ZacWalk/dd/blob/main/docs/extensions.md).

Add one manifest target per CMake executable. `dd targets --json` lists them, and
`dd run TARGET -- ARGS` selects one. Set `project.default-target` to allow `dd run`
without an ID when several targets exist. For F5, preview
`dd targets --vscode --dry-run` then apply with `--yes` to add missing launch entries.

`dd run` waits with a bounded timeout. `dd launch TARGET -- ARGS` starts a persistent
Release process and returns its PID and log paths. `dd build --app ID,ID` selects
CMake targets; `dd test --app ID --label REGEX --name REGEX` filters tests using the
target's `test-label`. Tests still build the preset's default targets in both configs.
See the [adoption guide](https://github.com/ZacWalk/dd/blob/main/docs/adoption.md)
for phase-specific presets, application-owned dependencies and native requirements.

## AI tools

The project-local MCP server is configured in `.vscode/mcp.json`, which invokes
`./dd.ps1 mcp`, and runs directly with PowerShell 7.4+ without external packages or a
build step. Its default configuration does not execute project builds/tests; add
`--allow-execution` only for a trusted project. See [AGENTS.md](AGENTS.md).
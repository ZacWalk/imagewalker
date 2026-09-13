# Working on this app

Use the vendored dd build system; project settings live in dd.psd1 and CMake.
The manifest is a data-only hashtable loaded with Import-PowerShellDataFile, never
dot-sourced or evaluated. Preserve its schema and use arrays for targets.
Do not customize .dd/ for application behavior.

## dd has two modes

**CLI mode** is the default and the single behavior owner. Each verb runs once, prints
one schema 1 result envelope, and exits:

```powershell
pwsh -NoProfile -File ./dd.ps1 doctor --json
pwsh -NoProfile -File ./dd.ps1 dep install --json
pwsh -NoProfile -File ./dd.ps1 test --json
```

**MCP mode** starts with `dd mcp`. The process becomes a stdio JSON-RPC server and stays
alive until stdin closes, adapting typed MCP requests onto CLI mode — each tool call runs
as a child `dd` invocation and returns that command's envelope:

```powershell
pwsh -NoProfile -File ./dd.ps1 mcp
```

MCP mode owns stdout for protocol messages, so it prints no result envelope, rejects
`--json`, and sends diagnostics to stderr. Its workspace boundary is the project root.
Use CLI mode for ordinary work; MCP mode is only for an MCP client.

- Native builds use MSVC on Windows and GCC on Linux. GUI support is Windows-only.
- Use --non-interactive and --json for automation. Result schema 1 includes ok,
  exitCode, data, errors and logs. Preserve nonzero exit codes and read full logs.
- Dependency URLs, full commit or SHA-256 pins and methods are in cmake/dd-dependencies.json.
  dd dep modifies declarations only; CMake FetchContent/ExternalProject downloads and
  builds in the preset's tree. Never reset local cache edits, update pins,
  install system packages, commit or push without authorization.
- Existing apps may declare dependencies.owner = 'application'; their CMake owns pins
  and dd reports an unknown inventory. Do not replace it with an empty managed list.
- Requirements, configure/build/test preset mappings and target paths live in dd.psd1.
  Build and test both configurations by default. Test filters never permit an empty run.
- Preview mutations with --dry-run. dd init only applies to an empty folder.
- CMake and tests execute project and dependency code; only run them for a trusted workspace.
- Discover optional project scripts with `dd commands --json` and `dd help NAME`.
  Keep scripts project-owned; never override built-ins. Scripts consume one schema 1
  JSON stdin request and return one JSON response with data/files on stdout. Write
  commands need --yes; dry-runs execute code and are not a sandbox.
- Use `dd targets --json` for multi-app IDs and defaults, then `dd run ID -- ARGS`.
  Use `dd launch ID -- ARGS` only when a persistent process is intended; it survives
  the driver and returns a PID and log paths. `run` remains bounded.
  Preview/apply missing F5 entries via `dd targets --vscode --dry-run` / `--yes`.
  Targets of kind `library` build, test and answer --app, but never run or launch and
  get no debugger entry; keep project.default-target on an executable target.
- Project-local MCP mode is configured in .vscode/mcp.json, which invokes `./dd.ps1 mcp`
  and uses PowerShell 7.4+ with no external packages. Accept the client's trust prompt to
  start it. Add --allow-execution to its args only after the user authorizes project-code
  execution, including custom dry-runs. Register it with `dd ide --mcp` when absent.
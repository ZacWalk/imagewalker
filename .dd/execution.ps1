function Invoke-DDStreamingProcess($Process, [string]$Directory, [int]$Timeout, [string]$Phase) {
    $logDir = Join-Path ([IO.Path]::GetTempPath()) 'dd-logs'
    [IO.Directory]::CreateDirectory($logDir) | Out-Null
    $path = Join-Path $logDir ([guid]::NewGuid().ToString('N') + '.log')
    $writer = [IO.StreamWriter]::new($path, $false, [Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    if ($null -ne $script:DDLogs) { $script:DDLogs.Add($path) }
    $output = [Text.StringBuilder]::new()
    $errors = [Text.StringBuilder]::new()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $nextHeartbeat = 5
    $timedOut = $false
    try {
        $Process.StandardInput.Close()
        $streams = @(
            @{ reader = $Process.StandardOutput; task = $Process.StandardOutput.ReadLineAsync(); buffer = $output; done = $false },
            @{ reader = $Process.StandardError; task = $Process.StandardError.ReadLineAsync(); buffer = $errors; done = $false }
        )
        while (-not $Process.HasExited -or @($streams | Where-Object { -not $_.done }).Count) {
            foreach ($stream in $streams) {
                while (-not $stream.done -and $stream.task.IsCompleted) {
                    $line = $stream.task.GetAwaiter().GetResult()
                    if ($null -eq $line) { $stream.done = $true; break }
                    $null = $stream.buffer.AppendLine($line)
                    $writer.WriteLine($line)
                    # Unprefixed so VS Code $msCompile/$gcc problem matchers still anchor at column 1.
                    if ($Phase) { [Console]::Error.WriteLine($line) }
                    $stream.task = $stream.reader.ReadLineAsync()
                }
            }
            if ($timer.Elapsed.TotalSeconds -ge $Timeout) {
                $timedOut = $true
                if (-not $Process.HasExited) { $Process.Kill($true); $null = $Process.WaitForExit(5000) }
                break
            }
            if ($Phase -and $timer.Elapsed.TotalSeconds -ge $nextHeartbeat) {
                Write-DDProgress "$Phase still running ($([int]$timer.Elapsed.TotalSeconds)s); log: $path"
                $nextHeartbeat += 5
            }
            if (-not $Process.HasExited) { $null = $Process.WaitForExit(100) }
            elseif (@($streams | Where-Object { -not $_.done }).Count) { $pending = @($streams | Where-Object { -not $_.done } | ForEach-Object task); $null = [Threading.Tasks.Task]::WaitAny([Threading.Tasks.Task[]]$pending, 100) }
        }
        if ($timedOut) { Stop-DD "Execution timed out after $Timeout seconds. Log: $path" 1 }
        return @{ exitCode = $Process.ExitCode; stdout = $output.ToString(); stderr = $errors.ToString(); log = $path; timedOut = $false }
    }
    finally { $writer.Dispose() }
}

function Start-DDApplication([string]$Binary, [string[]]$Arguments, [string]$Directory) {
    if (-not (Test-Path -LiteralPath $Binary -PathType Leaf)) { Stop-DD "Launch binary not found: $Binary" 3 }
    $logDir = Join-Path ([IO.Path]::GetTempPath()) 'dd-launch'
    [IO.Directory]::CreateDirectory($logDir) | Out-Null
    $id = [guid]::NewGuid().ToString('N')
    $stdout = Join-Path $logDir "$id.stdout.log"
    $stderr = Join-Path $logDir "$id.stderr.log"
    if ($IsWindows) {
        if (-not ('DD.DetachedApplication' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
namespace DD {
    public static class DetachedApplication {
        [StructLayout(LayoutKind.Sequential)] struct Security {
            public int Size; public IntPtr Descriptor; public int Inherit;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct Startup {
            public int Size; public string Reserved; public string Desktop; public string Title;
            public int X, Y, Width, Height, Columns, Rows, Fill, Flags;
            public short Show, ReservedSize; public IntPtr ReservedBytes, Input, Output, Error;
        }
        [StructLayout(LayoutKind.Sequential)] struct StartupEx {
            public Startup Info; public IntPtr Attributes;
        }
        [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {
            public IntPtr Process, Thread; public int Id, ThreadId;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateFile(string path, uint access, uint share, ref Security security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcess(string application, StringBuilder command, IntPtr processSecurity, IntPtr threadSecurity,
            bool inherit, uint flags, IntPtr environment, string directory, ref StartupEx startup, out ProcessInfo process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returned);
        [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr list);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
        public static int Start(string binary, string arguments, string directory, string stdout, string stderr) {
            var security = new Security { Size = Marshal.SizeOf<Security>(), Inherit = 1 };
            var handles = new IntPtr[3];
            IntPtr attributes = IntPtr.Zero, values = IntPtr.Zero;
            bool initialized = false;
            try {
                handles[0] = CreateFile("NUL", 0x80000000, 3, ref security, 3, 0x80, IntPtr.Zero);
                handles[1] = CreateFile(stdout, 0x40000000, 3, ref security, 1, 0x80, IntPtr.Zero);
                handles[2] = CreateFile(stderr, 0x40000000, 3, ref security, 1, 0x80, IntPtr.Zero);
                foreach (var handle in handles) if (handle == new IntPtr(-1)) throw new Win32Exception();
                IntPtr size = IntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
                attributes = Marshal.AllocHGlobal(size);
                if (!InitializeProcThreadAttributeList(attributes, 1, 0, ref size)) throw new Win32Exception();
                initialized = true;
                values = Marshal.AllocHGlobal(3 * IntPtr.Size);
                Marshal.Copy(handles, 0, values, 3);
                if (!UpdateProcThreadAttribute(attributes, 0, new IntPtr(0x20002), values, new IntPtr(3 * IntPtr.Size), IntPtr.Zero, IntPtr.Zero)) throw new Win32Exception();
                var startup = new StartupEx { Attributes = attributes, Info = new Startup {
                    Size = Marshal.SizeOf<StartupEx>(), Flags = 0x100, Input = handles[0], Output = handles[1], Error = handles[2] } };
                if (!CreateProcess(binary, new StringBuilder("\"" + binary + "\" " + arguments), IntPtr.Zero, IntPtr.Zero, true,
                    0x00080208, IntPtr.Zero, directory, ref startup, out var process)) throw new Win32Exception();
                CloseHandle(process.Thread); CloseHandle(process.Process);
                return process.Id;
            } finally {
                if (initialized) DeleteProcThreadAttributeList(attributes);
                if (attributes != IntPtr.Zero) Marshal.FreeHGlobal(attributes);
                if (values != IntPtr.Zero) Marshal.FreeHGlobal(values);
                foreach (var handle in handles) if (handle != IntPtr.Zero && handle != new IntPtr(-1)) CloseHandle(handle);
            }
        }
    }
}
'@
        }
        $quoted = foreach ($argument in $Arguments) {
            '"' + ([regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1')) + '"'
        }
        $processId = [DD.DetachedApplication]::Start($Binary, ($quoted -join ' '), $Directory, $stdout, $stderr)
        $started = [DateTime]::UtcNow.ToString('o')
    }
    else {
        foreach ($tool in @('setsid','nohup')) { if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) { Stop-DD "Persistent launch requires $tool." 3 } }
        $shell = 'out=$1; err=$2; shift 2; nohup setsid "$@" </dev/null >"$out" 2>"$err" & echo $!'
        $result = Invoke-DDProcess /bin/sh (@('-c', $shell, 'dd-launch', $stdout, $stderr, $Binary) + $Arguments) $Directory 10
        if ($result.stdout.Trim() -notmatch '^[0-9]+$') { Stop-DD 'Could not identify the launched process.' 1 }
        $processId = [int]$result.stdout.Trim()
        $started = [DateTime]::UtcNow.ToString('o')
    }
    return @{ status = 'started'; pid = $processId; startedAt = $started; executable = $Binary; stdoutLog = $stdout; stderrLog = $stderr }
}

function Invoke-DDTests([string]$Root, $Manifest, [string]$Config, $Options, $Applications) {
    $mapping = Get-DDPresetMapping $Manifest $Config
    $filters = @()
    if ($Options.label) { $filters += @('-L', $Options.label) }
    if ($Options.name) { $filters += @('-R', $Options.name) }
    if ($Applications.Count) {
        $labels = @($Applications | ForEach-Object { $_['test-label'] })
        if ($labels.Count -ne $Applications.Count -or @($labels | Where-Object { -not $_ }).Count) { Stop-DD 'Selected apps must declare test-label to scope CTest.' }
        $labelRegex = '^(' + (($labels | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
        $filters += @('-L', $labelRegex)
    }
    $base = @('--preset', $mapping.test) + $filters
    $listing = Invoke-DDProcess ctest ($base + @('--show-only=json-v1')) $Root
    try { $selected = $listing.stdout | ConvertFrom-Json -AsHashtable } catch { Stop-DD 'CTest did not return test discovery JSON.' 1 }
    if (-not $selected.tests.Count) { Stop-DD "No tests selected for $Config. Refusing an empty test run." 2 }
    $junit = Join-Path ([IO.Path]::GetTempPath()) ('dd-ctest-' + [guid]::NewGuid().ToString('N') + '.xml')
    $test = Invoke-DDProcess ctest ($base + @('--output-on-failure','--no-tests=error','--timeout', '120', '--output-junit', $junit)) $Root -Log -AllowFailure -Progress "$Config test"
    $failures = @()
    if (Test-Path -LiteralPath $junit) {
        $settings = [Xml.XmlReaderSettings]::new(); $settings.DtdProcessing = 'Prohibit'; $settings.XmlResolver = $null
        $reader = [Xml.XmlReader]::Create($junit, $settings)
        try { $document = [Xml.XmlDocument]::new(); $document.XmlResolver = $null; $document.Load($reader) } finally { $reader.Dispose() }
        foreach ($case in $document.SelectNodes('//testcase[failure or error]')) {
            $failures += @{ name = $case.GetAttribute('name'); duration = $case.GetAttribute('time'); detail = $case.InnerText.Substring(0, [Math]::Min(2000, $case.InnerText.Length)) }
        }
    }
    if ($test.exitCode) {
        $detail = if ($failures.Count) { $failures | ConvertTo-Json -Depth 5 -Compress } else { (($test.stdout + $test.stderr) -split '\r?\n' | Select-Object -Last 25) -join "`n" }
        $exception = [InvalidOperationException]::new("CTest failed ($Config, exit $($test.exitCode)). $detail Log: $($test.log)")
        $exception.Data['exitCode'] = 1; $exception.Data['testFailures'] = $failures
        throw $exception
    }
    return @{ configuration = $Config; status = 'tested'; tests = @($selected.tests.name); log = $test.log; report = $junit }
}
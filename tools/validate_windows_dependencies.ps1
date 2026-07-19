[CmdletBinding()]
param(
    [string]$BundleDir = 'build/bin',
    [string[]]$EntryPoints = @(
        'SaidaEngine.exe',
        'SaidaEngineRuntime.exe',
        'SaidaEngineHub.exe',
        'saida_tool.exe'
    ),
    [string]$OutputPath = 'build/release/windows-dependencies.json',
    [string]$Objdump = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bundle = if ([System.IO.Path]::IsPathRooted($BundleDir)) {
    [System.IO.Path]::GetFullPath($BundleDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $BundleDir))
}
$output = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    [System.IO.Path]::GetFullPath($OutputPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $OutputPath))
}
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'build')).TrimEnd('\', '/')
if (-not $output.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar,
                            [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputPath must stay under $buildRoot"
}
if (-not (Test-Path -LiteralPath $bundle -PathType Container)) {
    throw "Missing Windows bundle: $bundle"
}

if ($Objdump) {
    $objdumpPath = if ([System.IO.Path]::IsPathRooted($Objdump)) {
        [System.IO.Path]::GetFullPath($Objdump)
    } else {
        (Get-Command $Objdump -ErrorAction Stop).Source
    }
} else {
    $command = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if (-not $command) { $command = Get-Command objdump -ErrorAction SilentlyContinue }
    if (-not $command) { throw "Required tool not found: objdump" }
    $objdumpPath = $command.Source
}
if (-not (Test-Path -LiteralPath $objdumpPath -PathType Leaf)) {
    throw "objdump not found: $objdumpPath"
}

$systemDlls = @(
    'advapi32.dll', 'bcrypt.dll', 'cabinet.dll', 'cfgmgr32.dll',
    'comctl32.dll', 'comdlg32.dll', 'crypt32.dll', 'd3d11.dll',
    'd3d12.dll', 'dbghelp.dll', 'dwmapi.dll', 'dxgi.dll', 'gdi32.dll',
    'imm32.dll', 'iphlpapi.dll', 'kernel32.dll', 'msvcrt.dll',
    'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'powrprof.dll',
    'propsys.dll', 'rpcrt4.dll', 'setupapi.dll', 'shell32.dll',
    'shlwapi.dll', 'user32.dll', 'ucrtbase.dll', 'version.dll',
    'vulkan-1.dll', 'winmm.dll', 'wintrust.dll', 'ws2_32.dll'
)
$forbiddenDynamicRuntimes = @(
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll'
)

function Is-System-Dll([string]$Name) {
    $lower = $Name.ToLowerInvariant()
    $lower -like 'api-ms-win-*' -or
        $lower -like 'ext-ms-win-*' -or
        $systemDlls -contains $lower
}

function Relative-Path([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith(
            $bundle.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "PE file escapes bundle: $full"
    }
    $full.Substring($bundle.TrimEnd('\', '/').Length + 1).Replace('\', '/')
}

function Inspect-Pe([string]$Path) {
    $format = (& $objdumpPath -f $Path 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $format -notmatch 'file format pei-x86-64') {
        throw "Expected a Windows x64 PE file: $(Relative-Path $Path)"
    }
    $headers = (& $objdumpPath -p $Path 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "objdump could not inspect: $(Relative-Path $Path)"
    }
    $imports = @([regex]::Matches($headers, 'DLL Name:\s+([^\s]+)') |
        ForEach-Object { $_.Groups[1].Value } |
        Sort-Object -Unique)
    [ordered]@{
        path = (Relative-Path $Path)
        bytes = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
        imports = $imports
    }
}

$dllIndex = @{}
foreach ($dll in Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.dll') {
    $key = $dll.Name.ToLowerInvariant()
    if ($dllIndex.ContainsKey($key)) {
        throw "Ambiguous bundled DLL name: $($dll.Name)"
    }
    $dllIndex[$key] = $dll.FullName
}

$pending = New-Object System.Collections.Generic.Queue[string]
foreach ($entryPoint in $EntryPoints) {
    if ([System.IO.Path]::IsPathRooted($entryPoint) -or $entryPoint.Contains('..')) {
        throw "Unsafe entry point: $entryPoint"
    }
    $path = [System.IO.Path]::GetFullPath((Join-Path $bundle $entryPoint))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Windows entry point: $entryPoint"
    }
    $pending.Enqueue($path)
}

$visited = @{}
$records = New-Object System.Collections.Generic.List[object]
while ($pending.Count -gt 0) {
    $path = $pending.Dequeue()
    $key = $path.ToLowerInvariant()
    if ($visited.ContainsKey($key)) { continue }
    $visited[$key] = $true

    $record = Inspect-Pe $path
    $classified = New-Object System.Collections.Generic.List[object]
    foreach ($import in @($record.imports)) {
        $lower = $import.ToLowerInvariant()
        if ($forbiddenDynamicRuntimes -contains $lower) {
            throw "Forbidden dynamic MinGW runtime imported by $($record.path): $import"
        }
        if (Is-System-Dll $import) {
            $classification = 'system'
        } elseif ($dllIndex.ContainsKey($lower)) {
            $classification = 'bundled'
            $pending.Enqueue([string]$dllIndex[$lower])
        } else {
            throw "Missing non-system DLL imported by $($record.path): $import"
        }
        $classified.Add([ordered]@{
            name = $import
            classification = $classification
        })
    }
    $record.imports = $classified.ToArray()
    $records.Add($record)
}

$commit = (& git -C $root rev-parse HEAD).Trim()
$commitTime = (& git -C $root show -s --format=%cI HEAD).Trim()
$report = [ordered]@{
    schema = 1
    engineCommit = $commit
    generatedAtUtc = ([DateTimeOffset]::Parse($commitTime).UtcDateTime.ToString('o'))
    architecture = 'x86_64'
    bundle = [System.IO.Path]::GetFileName($bundle.TrimEnd('\', '/'))
    entryPoints = @($EntryPoints | ForEach-Object { $_.Replace('\', '/') })
    files = @($records.ToArray() | Sort-Object path)
}

$parent = Split-Path -Parent $output
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    $output,
    (($report | ConvertTo-Json -Depth 10).TrimEnd() + "`r`n"),
    $encoding)

Write-Host "WINDOWS DEPENDENCY VERIFY PASS"
Write-Host "  entry points: $($EntryPoints.Count)"
Write-Host "  PE files in closure: $($records.Count)"
Write-Host "  report: $output"

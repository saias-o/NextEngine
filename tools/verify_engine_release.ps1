[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$SaidaTool = '',
    [string]$DesktopRuntime = '',
    [string]$WebPlayerDir = 'build-web-player',
    [string]$AuthoringWasmDir = 'build-authoring-wasm',
    [string]$AuthoringRuntimeDir = 'build-web',
    [string]$FixturesDir = 'tests/fixtures/compat'
)

# Recomputes every hash in an engine release manifest and, when a saida_tool is
# supplied, re-reads the format/contract versions from it. Exit 0 only if the
# bundle on disk matches the manifest byte for byte and version for version.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $ManifestPath)) { throw "missing manifest: $ManifestPath" }
$manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json

$failures = New-Object System.Collections.Generic.List[string]

function Hash-Of([string]$FullPath) {
    if (-not (Test-Path -LiteralPath $FullPath)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $FullPath).Hash.ToLowerInvariant()
}

function Check-File([string]$FullPath, $Record, [string]$Label) {
    if (-not (Test-Path -LiteralPath $FullPath)) {
        $failures.Add("$Label missing on disk: $FullPath"); return
    }
    $item = Get-Item -LiteralPath $FullPath
    if ($item.Length -ne $Record.bytes) {
        $failures.Add("$Label size mismatch: expected $($Record.bytes), got $($item.Length)")
    }
    $actual = Hash-Of $FullPath
    if ($actual -ne $Record.sha256) {
        $failures.Add("$Label sha256 mismatch: expected $($Record.sha256), got $actual")
    }
}

function Check-Bundle($Bundle, [string]$Directory, [string]$Label) {
    if (-not $Bundle) { return }
    foreach ($record in $Bundle.files) {
        Check-File (Join-Path $Directory $record.path) $record "$Label/$($record.path)"
    }
}

$artifacts = $manifest.artifacts

if ($artifacts.PSObject.Properties.Name -contains 'saidaTool') {
    $toolFile = if ($SaidaTool) { $SaidaTool } else { $artifacts.saidaTool.path }
    Check-File $toolFile $artifacts.saidaTool 'saidaTool'
}
if ($artifacts.PSObject.Properties.Name -contains 'desktopRuntime') {
    $runtimeFile = if ($DesktopRuntime) { $DesktopRuntime } else { $artifacts.desktopRuntime.path }
    Check-File $runtimeFile $artifacts.desktopRuntime 'desktopRuntime'
}
Check-Bundle $artifacts.webPlayer $WebPlayerDir 'webPlayer'
Check-Bundle $artifacts.authoringWasm $AuthoringWasmDir 'authoringWasm'
Check-Bundle $artifacts.authoringRuntime $AuthoringRuntimeDir 'authoringRuntime'

foreach ($record in $manifest.fixtures) {
    Check-File (Join-Path $FixturesDir $record.path) $record "fixtures/$($record.path)"
}

# When the tool is available, the announced format versions must still match.
if ($SaidaTool -and (Test-Path -LiteralPath $SaidaTool)) {
    $described = (& $SaidaTool describe-engine | Out-String | ConvertFrom-Json)
    if ($described.engineVersion -ne $manifest.engineVersion) {
        $failures.Add("engineVersion mismatch: manifest $($manifest.engineVersion), tool $($described.engineVersion)")
    }
    $manifestFormats = $manifest.formats | ConvertTo-Json -Depth 6 -Compress
    $toolFormats = $described.formats | ConvertTo-Json -Depth 6 -Compress
    if ($manifestFormats -ne $toolFormats) {
        $failures.Add("formats mismatch between manifest and tool")
    }
}

if ($failures.Count -gt 0) {
    Write-Host "ENGINE RELEASE VERIFY FAILED ($($failures.Count) issue(s)):"
    foreach ($f in $failures) { Write-Host "  - $f" }
    exit 1
}

$fixtureCount = @($manifest.fixtures).Count
Write-Host "ENGINE RELEASE VERIFY PASS"
Write-Host "  commit: $($manifest.engineCommit)  engineVersion: $($manifest.engineVersion)"
Write-Host "  fixtures: $fixtureCount frozen files matched"
exit 0

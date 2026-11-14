param(
    [string]$KeyFile = '',
    [string]$DataDirectory = '',
    [string]$BindAddress = '0.0.0.0',
    [ValidateRange(1, 65535)][int]$Port = 8788,
    [string]$PackagePath = '/firmware.ota'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$node = Get-Command node -ErrorAction SilentlyContinue
if (!$node) { throw 'Node.js 20 or newer was not found.' }
if (!$KeyFile) { $KeyFile = Join-Path $repoRoot 'app/ota_key_local.json' }
if (!$DataDirectory) { $DataDirectory = Join-Path $repoRoot '.ota-data' }
$KeyFile = [System.IO.Path]::GetFullPath($KeyFile)
$DataDirectory = [System.IO.Path]::GetFullPath($DataDirectory)
if (!(Test-Path -LiteralPath $KeyFile -PathType Leaf)) {
    throw 'The actual OTA key file is missing; supply -KeyFile with your existing private JSON file.'
}
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build')).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
$buildPrefix = $buildRoot + [System.IO.Path]::DirectorySeparatorChar
if ($DataDirectory.Equals($buildRoot, [System.StringComparison]::OrdinalIgnoreCase) -or $DataDirectory.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The persistent OTA data directory must be outside build/.'
}
& $node.Source (Join-Path $repoRoot 'tools/ota/ota.mjs') admin --key-file $KeyFile --data-dir $DataDirectory --host $BindAddress --port $Port --path $PackagePath
if ($LASTEXITCODE -ne 0) { throw 'OTA administration server stopped with an error.' }

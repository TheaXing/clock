param(
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$OtaConfig = '',
    [string]$KeyFile = '',
    [string]$OutputDirectory = '',
    [switch]$ActiveBuzzer,
    [switch]$Factory
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$node = Get-Command node -ErrorAction SilentlyContinue
if (!$node) { throw 'Node.js 20 or newer was not found.' }
if (!$OtaConfig) { $OtaConfig = Join-Path $repoRoot 'app/ota_config_local.h' }
if (!$KeyFile) { $KeyFile = Join-Path $repoRoot 'app/ota_key_local.json' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot "build/ota-packages/v$Version" }
$OtaConfig = [System.IO.Path]::GetFullPath($OtaConfig)
$KeyFile = [System.IO.Path]::GetFullPath($KeyFile)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$privateRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build/ota-private'))
$privateDirectory = Join-Path $privateRoot ([Guid]::NewGuid().ToString('N'))
$generatedConfig = Join-Path $privateDirectory 'ota_config_local.h'
$support = Join-Path $repoRoot 'tools/ota/build-support.mjs'
$buildLock = $null
try {
    $lockDirectory = Join-Path $repoRoot 'build'
    [System.IO.Directory]::CreateDirectory($lockDirectory) | Out-Null
    try {
        $buildLock = [System.IO.File]::Open((Join-Path $lockDirectory 'ota-build.lock'),
            [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
    } catch {
        throw 'Another OTA build is running in this workspace. Wait for it to finish before compiling another version.'
    }
    & $node.Source $support prepare --config $OtaConfig --key-file $KeyFile --version $Version --output-dir $OutputDirectory --generated-config $generatedConfig
    if ($LASTEXITCODE -ne 0) { throw 'OTA build configuration validation failed.' }
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -Ota -OtaConfig $generatedConfig -ActiveBuzzer:$ActiveBuzzer
    if (!$?) { throw 'OTA firmware build failed.' }
    $binaryDirectory = Join-Path $repoRoot "build/Ota$Configuration"
    $appBinary = Join-Path $binaryDirectory 'Clock_CubeMX.bin'
    $bootBinary = Join-Path $binaryDirectory 'Clock_Bootloader.bin'
    $factoryOption = if ($Factory) { 'true' } else { 'false' }
    & $node.Source $support publish --config $generatedConfig --key-file $KeyFile --version $Version --output-dir $OutputDirectory --app $appBinary --boot $bootBinary --factory $factoryOption
    if ($LASTEXITCODE -ne 0) { throw 'OTA artifact verification or packaging failed.' }
} finally {
    if ($null -ne $buildLock) {
        $buildLock.Dispose()
    }
    $resolvedPrivate = [System.IO.Path]::GetFullPath($privateDirectory)
    $allowedPrefix = $privateRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if (!$resolvedPrivate.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a private configuration directory outside the build workspace.'
    }
    if (Test-Path -LiteralPath $resolvedPrivate) {
        Remove-Item -LiteralPath $resolvedPrivate -Recurse -Force
    }
}

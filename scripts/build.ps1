param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Clean,
    [switch]$RgbTest,
    [switch]$ActiveBuzzer,
    [switch]$Ota,
    [string]$OtaConfig = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$bundles = Join-Path $env:LOCALAPPDATA 'stm32cube/bundles'
$gcc = Get-ChildItem -Path "$bundles/gnu-tools-for-stm32/*/bin/arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
$ninja = Get-ChildItem -Path "$bundles/ninja/*/bin/ninja.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if ($gcc) { $env:PATH = "$($gcc.DirectoryName);$env:PATH" }
if ($ninja) { $env:PATH = "$($ninja.DirectoryName);$env:PATH" }
if (!(Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) { throw 'arm-none-eabi-gcc was not found.' }
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (!$cmake) { throw 'CMake was not found. Run from the STM32 build terminal or add CMake to PATH.' }
Push-Location $repoRoot
try {
    $rgb = if ($RgbTest) { 'ON' } else { 'OFF' }
    $active = if ($ActiveBuzzer) { 'ON' } else { 'OFF' }
    $buildPreset = if ($Ota) { "Ota$Configuration" } else { $Configuration }
    $otaEnabled = if ($Ota) { 'ON' } else { 'OFF' }
    $configureArgs = @('--preset', $buildPreset, "-DCLOCK_RGB_TEST=$rgb", "-DCLOCK_BUZZER_ACTIVE=$active", "-DCLOCK_OTA_ENABLED=$otaEnabled")
    if ($OtaConfig) {
        $otaConfigPath = (Resolve-Path -LiteralPath $OtaConfig).Path
        $configureArgs += "-DCLOCK_OTA_CONFIG_FILE=$otaConfigPath"
    }
    & $cmake.Source @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    $buildArgs = @('--build', '--preset', $buildPreset)
    if ($Clean) { $buildArgs += '--clean-first' }
    & $cmake.Source @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
} finally { Pop-Location }

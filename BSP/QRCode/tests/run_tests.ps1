$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/tests/qr'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for these native tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$sources = @('BSP/QRCode/tests/test_clock_qr.c', 'BSP/QRCode/clock_qr.c', 'BSP/QRCode/qrcodegen.c', 'app/page/wifi_page.c', 'app/font/font16_maple.c', 'app/font/font24_maple.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$includes = @('BSP/QRCode', 'BSP/ST7789', 'BSP/RTC', 'app', 'app/page') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS {1} {2} /Fo"{3}\\" /Fe"{3}/test_clock_qr.exe" && "{3}/test_clock_qr.exe" "{3}"' -f $devCmd, ($includes -join ' '), ($sources -join ' '), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'QR reference/page tests failed.' }

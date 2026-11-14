$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/tests/esp-tracked'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for these native tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$commands = @(
    ('"{0}" -no_logo -arch=x64' -f $devCmd),
    ('cl /nologo /utf-8 /std:c11 /W4 /WX /I"{0}/BSP/ESP_AT" "{1}/test_esp_parse.c" "{0}/BSP/ESP_AT/esp_at_parse.c" /Fo"{2}\\" /Fe"{2}/test_esp_parse.exe"' -f $repoRoot, $PSScriptRoot, $testBuild),
    ('"{0}/test_esp_parse.exe"' -f $testBuild),
    ('cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}/stubs" /I"{0}/BSP/ESP_AT" /I"{0}/app" "{1}/test_esp_transport.c" "{0}/BSP/ESP_AT/esp_at.c" "{0}/BSP/ESP_AT/esp_at_parse.c" "{0}/app/weather.c" /Fo"{2}\\" /Fe"{2}/test_esp_transport.exe"' -f $repoRoot, $PSScriptRoot, $testBuild),
    ('"{0}/test_esp_transport.exe"' -f $testBuild),
    ('"{0}/test_esp_transport.exe" --command-mode-boot' -f $testBuild)
)
& cmd.exe /d /s /c ($commands -join ' && ')
if ($LASTEXITCODE -ne 0) { throw 'ESP transport/parser tests failed.' }

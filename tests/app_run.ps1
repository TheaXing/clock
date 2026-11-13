param([string]$Compiler = 'cl.exe')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'build/host-app-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe = Join-Path $output 'app_task_test.exe'
Push-Location $output
try {
    & $Compiler /nologo /std:c11 /W4 /WX /wd4244 /D_CRT_SECURE_NO_WARNINGS /utf-8 `
        "/FI$PSScriptRoot\app_stubs\test_config.h" "/I$PSScriptRoot\app_stubs" `
        "/I$repo\app" "/I$repo\app\page" "/I$repo\BSP\RTC" "/I$repo\BSP\AHT20" "/I$repo\BSP\ESP_AT" `
        "$PSScriptRoot\app_task_test.c" "$repo\app\app.c" "$repo\app\wifi.c" "$repo\app\weather.c" "/Fe:$exe"
    if ($LASTEXITCODE -ne 0) { throw 'Application task test compilation failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Application task test failed.' }
}
finally { Pop-Location }

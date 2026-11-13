param([string]$Compiler='cl.exe')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repo 'build/host-alarm-page'
$null=New-Item -ItemType Directory -Force -Path $out
Push-Location $out
try {
    & $Compiler /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS "/I$repo/app/alarm" "/I$repo/app" "/I$repo/BSP/RTC" "/I$repo/BSP/ST7789" "$PSScriptRoot/alarm_page_test.c" "$repo/app/alarm/alarm_page.c" "$repo/app/alarm/alarm_menu.c" "$repo/app/alarm/alarm_engine.c" /Fe:alarm_page_test.exe
    if($LASTEXITCODE -ne 0){throw 'Alarm page compilation failed'}
    & '.\alarm_page_test.exe'
    if($LASTEXITCODE -ne 0){throw 'Alarm page tests failed'}
}finally{Pop-Location}

param([string]$Compiler='cl.exe')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repo 'build/host-alarm-menu'
$null=New-Item -ItemType Directory -Force -Path $out
Push-Location $out
try {
    & $Compiler /nologo /std:c11 /W4 /WX "/I$repo/app/alarm" "/I$repo/BSP/RTC" "$PSScriptRoot/alarm_menu_test.c" "$repo/app/alarm/alarm_menu.c" /Fe:alarm_menu_test.exe
    if($LASTEXITCODE -ne 0){throw 'Alarm menu compilation failed'}
    & '.\alarm_menu_test.exe'
    if($LASTEXITCODE -ne 0){throw 'Alarm menu tests failed'}
}finally{Pop-Location}

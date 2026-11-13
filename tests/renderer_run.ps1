param([string]$Compiler = 'cl.exe')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path ([IO.Path]::GetTempPath()) 'clock_renderer_tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe = Join-Path $output 'renderer_test.exe'
Push-Location $output
try {
    & $Compiler /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /utf-8 `
        "/I$PSScriptRoot\stubs_renderer" "/I$repo\BSP\ST7789" `
        "$PSScriptRoot\renderer_test.c" "$repo\BSP\ST7789\st7789.c" "$repo\app\font\font42_maple.c" "/Fe:$exe"
    if ($LASTEXITCODE -ne 0) { throw 'Renderer test compilation failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Renderer test failed.' }
    $uiExe = Join-Path $output 'renderer_ui_test.exe'
    & $Compiler /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /utf-8 `
        "/I$PSScriptRoot\stubs_renderer" "/I$repo\BSP\ST7789" "/I$repo\app" `
        "$PSScriptRoot\renderer_ui_test.c" "$repo\app\ui.c" "/Fe:$uiExe"
    if ($LASTEXITCODE -ne 0) { throw 'UI queue test compilation failed.' }
    & $uiExe
    if ($LASTEXITCODE -ne 0) { throw 'UI queue test failed.' }
    $pageExe = Join-Path $output 'renderer_page_test.exe'
    & $Compiler /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS `
        /source-charset:.936 /execution-charset:.936 `
        "/I$PSScriptRoot\stubs_renderer" "/I$repo\BSP\ST7789" "/I$repo\BSP\RTC" "/I$repo\app" "/I$repo\app\page" `
        "$PSScriptRoot\renderer_page_test.c" "$repo\app\page\main_page.c" "/Fe:$pageExe"
    if ($LASTEXITCODE -ne 0) { throw 'Page test compilation failed.' }
    & $pageExe
    if ($LASTEXITCODE -ne 0) { throw 'Page test failed.' }
}
finally { Pop-Location }

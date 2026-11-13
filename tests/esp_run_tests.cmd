@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
if not exist build\tests\esp_parse mkdir build\tests\esp_parse
if not exist build\tests\esp_transport mkdir build\tests\esp_transport
cl /nologo /std:c11 /W4 /WX /I BSP\ESP_AT tests\esp_parse_test.c BSP\ESP_AT\esp_at_parse.c /Fo:build\tests\esp_parse\ /Fe:build\tests\esp_parse\esp_parse_test.exe /Fd:build\tests\esp_parse\esp_parse_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\esp_parse\esp_parse_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c11 /W4 /WX /I tests\esp_stubs /I BSP\ESP_AT /I app tests\esp_transport_test.c BSP\ESP_AT\esp_at.c BSP\ESP_AT\esp_at_parse.c app\weather.c /Fo:build\tests\esp_transport\ /Fe:build\tests\esp_transport\esp_transport_test.exe /Fd:build\tests\esp_transport\esp_transport_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\esp_transport\esp_transport_test.exe
exit /b %errorlevel%

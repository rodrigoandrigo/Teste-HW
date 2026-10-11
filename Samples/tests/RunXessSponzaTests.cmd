@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /EHsc /std:c++17 /W4 SponzaTests.cpp ..\SponzaScene.cpp /Fe:SponzaTests.exe d3d12.lib
if errorlevel 1 exit /b 1
SponzaTests.exe "%~dp0..\.." --xess
set xessSponzaTestExit=%errorlevel%
popd
exit /b %xessSponzaTestExit%

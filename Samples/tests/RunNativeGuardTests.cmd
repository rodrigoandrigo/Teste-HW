@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /EHsc /W4 /Od /Fe:NativeGuardTests.exe /Fo:.\ ..\NativeGuard.cpp NativeGuardTests.cpp
if errorlevel 1 exit /b 1
NativeGuardTests.exe
set sampleTestExit=%errorlevel%
popd
exit /b %sampleTestExit%

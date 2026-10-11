@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /EHsc /std:c++17 /W4 Fsr3ProviderTests.cpp /Fe:Fsr3ProviderTests.exe d3d12.lib
if errorlevel 1 exit /b 1
Fsr3ProviderTests.exe "C:\Users\rodri\Dev1\Projetos\FidelityFX-SDK-2_3_0\Kits\FidelityFX\signedbin\amd_fidelityfx_upscaler_dx12.dll" --upscale
set fsrUpscaleTestExit=%errorlevel%
popd
exit /b %fsrUpscaleTestExit%

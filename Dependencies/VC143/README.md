# XeSS Visual C++ runtime dependencies

The x64 folder contains unmodified Microsoft VC143 Release OneCore runtime
DLLs, file version 14.44.35211.0, from the Visual Studio redistributable directory:

`VC/Redist/MSVC/14.44.35112/onecore/x64/Microsoft.VC143.CRT`

They are packaged at the application root for both x64 Debug and Release.
XeSS requires the Release DLL names even when Teste-HW itself is a Debug build.
This version exceeds the XeSS SDK minimum of 14.40.33810. The OneCore binaries
use API-set imports rather than the desktop runtime's KERNEL32 imports.
All 144 named runtime imports required by the bundled libxess.dll are exported.
The original Microsoft signatures are preserved.

These are redistributable runtime binaries, subject to the applicable Visual
Studio license. Availability and XeSS initialization still require Xbox tests.

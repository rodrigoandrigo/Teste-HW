# Teste-HW

Teste-HW is an x64 UWP/AppContainer hardware capability diagnostic application designed primarily for Xbox Series consoles in Developer Mode. Its purpose is to determine which low-level Windows facilities are actually usable from a packaged application, especially the facilities needed by an emulator, dynamic binary translator, or user-mode Linux runtime.

The application performs functional probes rather than relying only on DLL presence, API contracts, or CPUID feature bits. Every probe records a status, duration, HRESULT, Win32 error, and diagnostic details where applicable. Reports can be exported as UTF-8 text or JSON.

> This project is a diagnostic tool. A `PASS` result means that the specific operation described by that result succeeded. It does not imply that every operation offered by the same subsystem is available.

## Target

The interface uses XAML only, without a custom D3D11 renderer, swap chain,
render thread, or FPS overlay. Xbox layout scaling is disabled at launch.
Graphics devices are created only by the diagnostic suites when requested.

- UWP/AppContainer
- Xbox Series consoles in Developer Mode
- Windows x64
- Windows SDK 10.0.26100.0
- Visual Studio C++ UWP toolchain

ARM64 is present in the generated project configurations but is not a validation target. The implemented instruction and virtualization probes are intended for x64.

## Diagnostic suites

### CPU and instruction set

- CPU vendor, brand, family, model, and stepping
- Raw CPUID feature leaves
- Hypervisor CPUID identification
- SSE, AVX, AVX2, AES, PCLMULQDQ, BMI, and related features
- XSAVE, OSXSAVE, XGETBV, and extended-state layout
- RDTSC and RDTSCP execution
- FSGSBASE availability
- Actual instruction execution under structured exception handling

### Virtual memory and JIT

- Native page size and allocation granularity
- Physical memory and commit limits
- Reserve, commit, decommit, and release lifecycle
- Large contiguous virtual-address reservations
- Page-protection transitions
- RW-to-RX dynamic-code transitions
- Generated x64 code execution
- Large executable allocations

The package declares the UWP `codeGeneration` capability.

### Exceptions and traps

- Invalid instruction (`UD2`)
- Breakpoint trap (`INT3`)
- Invalid-address access violations
- Read-only and no-access pages
- Guard-page first-access behavior
- Execution from a non-executable page
- Integer division by zero
- Explicit structured exceptions and exception parameters
- Recovery after handled traps

### Virtualization

#### AMD-V / AMD SVM

The AMD-V probe reads the SVM capability leaves exposed by CPUID and records
the VMCB revision, ASID count, and feature flags when available. It also emits
and executes `VMRUN`, `VMLOAD`, `VMSAVE`, `STGI`, `CLGI`, `SKINIT`, and
`INVLPGA` under structured exception handling. This distinguishes CPU feature
advertisement from direct instruction accessibility inside the AppContainer.

On the tested Xbox Series S AppContainer, CPUID did not advertise SVM. Leaf
`0x8000000A` was readable but returned a zero VMCB revision, zero ASIDs, and no
SVM feature flags. Every tested SVM instruction was blocked with
`EXCEPTION_ACCESS_VIOLATION` (`0xC0000005`). The generated-code pages themselves
were executable, so this result represents the virtualization boundary rather
than a failure of the JIT allocation path.

#### KVM

KVM is a Linux kernel interface rather than a native Windows virtualization
API. The KVM probe searches for explicit Windows device bridges at known KVM
device names and records the exact Win32 result. If a bridge opens, it performs
an experimental `KVM_GET_API_VERSION` request. A DLL name is not treated as
evidence of KVM availability.

The current probe checks `\\.\KVM`, `\\.\Global\KVM`, and
`\\?\GLOBALROOT\Device\KVM`. On the tested console all three returned Win32
error 2 (`ERROR_FILE_NOT_FOUND`), indicating that no KVM device bridge was
present in the device namespace visible to the package. This is different from
finding a device that exists but returns `ERROR_ACCESS_DENIED`.

#### Windows Hypervisor Platform

The WHPX probe dynamically loads `WinHvPlatform.dll` and, when available, attempts the complete minimum chain:

1. Query hypervisor capabilities.
2. Create a partition.
3. Configure one virtual processor.
4. Set up the partition.
5. Create vCPU 0.
6. Read vCPU registers.
7. Delete the vCPU and partition.

#### Host Compute Service

The `vmcompute.dll` probe resolves and calls `HcsGetServiceProperties`. This distinguishes access to the Host Compute Service from simple DLL discovery.

#### Hyper-V WMI

The WMI probe attempts to:

1. Initialize COM and COM security.
2. Create `WbemLocator`.
3. Connect to `ROOT\virtualization\v2`.
4. Execute a WQL query against `Msvm_ComputerSystem`.
5. Enumerate a returned object.

Each boundary receives an independent result so that the first inaccessible layer is visible.

### File and shared-memory mappings

- Anonymous page-file mappings
- Coherence between multiple views
- Named mapping reopen through AppContainer-compatible APIs
- Allocation-granularity-aligned offsets
- File-backed persistence after flush and reopen
- Executable mapped views and generated-code execution

### Timers and synchronization

- QueryPerformanceCounter resolution and monotonicity
- Precise system time
- `Sleep(0)` and `Sleep(1)` behavior
- TSC/QPC frequency correlation
- Manual-reset and auto-reset events
- Semaphores and mutexes
- SRW locks and condition variables
- Waitable timers

### Threads and SMP

- Practical simultaneous-thread capacity, with a safety ceiling of 512 threads
- Per-thread affinity across every processor allowed by the process mask
- Confirmed migration using `GetCurrentProcessorNumber`
- Safe priority levels from `THREAD_PRIORITY_LOWEST` through `THREAD_PRIORITY_HIGHEST`
- Restoration of the original affinity and priority
- CPU throughput with 1, 2, 4, and up to all available logical processors
- Speedup and parallel-efficiency calculations

`THREAD_PRIORITY_TIME_CRITICAL` is deliberately not tested.

### Filesystem and I/O

- Package `LocalFolder` and `TemporaryFolder`
- `CreateFile2FromAppW`
- Sequential 1 MiB reads and writes
- Random 4 KiB reads and writes
- Throughput, IOPS, and latency
- Data validation
- `FlushFileBuffers`
- File-size verification and cleanup

The benchmark currently uses a 64 MiB file. Results can be influenced by the system cache and should not be interpreted as sustained storage performance.

### COM and WinRT activation matrix

The controlled activation matrix tests known, side-effect-free factories rather than enumerating and activating arbitrary registry entries.

COM probes include:

- WIC Imaging Factory and WIC Imaging Factory 2
- WMI Locator
- MMDevice Enumerator
- MSXML6 DOM Document
- SAPI SpVoice
- DirectShow Filter Graph
- Shell Link
- Task Scheduler

WinRT activation-factory probes include:

- ApplicationData and KnownFolders
- MemoryManager
- BitmapDecoder
- MediaEncodingProfile
- DeviceInformation
- StreamSocket
- CryptographicBuffer
- Package metadata

An activation `PASS` proves only that the class or factory was obtained. It does not prove that privileged or capability-gated methods will succeed.

### DirectML

The DirectML suite dynamically loads its dependencies so that a missing runtime cannot prevent the application from starting. It attempts to:

1. Load `d3d12.dll` and resolve `D3D12CreateDevice`.
2. Create a D3D12 device with minimum feature level 11.0.
3. Load `DirectML.dll` and resolve `DMLCreateDevice`.
4. Create a DirectML device.
5. Query the maximum DirectML feature level.
6. Query FLOAT32, FLOAT16, UINT8, and INT8 tensor support.
7. Create and compile a FLOAT32 Identity operator.

The system DirectML runtime is not bundled with the application.

### Intel XeSS Super Resolution

The XeSS suite uses Intel XeSS SDK 3.0.2 from the sibling
`XeSS_SDK_3_0_2` directory. The package includes the unmodified D3D12 runtime
`libxess.dll`, Intel's license, and the third-party program notices. Both x64
Debug and Release packages also include the Microsoft VC143 Release OneCore
runtime DLLs (`msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`), file
version 14.44.35211.0, from `Dependencies/VC143/x64`. These satisfy the SDK's
minimum runtime version; Xbox loading and initialization are tested separately.
The x64 packages also include the unmodified `libxell.dll` (low latency) and
`libxess_fg.dll` (frame generation) from the same SDK. These two libraries are
tested by separate XeLL and XeSS-FG buttons.
The probe:

1. Loads `libxess.dll` from the application package.
2. Resolves the required XeSS exports dynamically.
3. Reports the loaded XeSS runtime version.
4. Creates a D3D12 device and verifies Shader Model 6.4.
5. Creates an XeSS D3D12 context.
6. Checks the driver result and Intel XeFX version.
7. Queries resource requirements and the optimal input-resolution range for
   1920x1080 Performance mode.
8. Initializes XeSS pipelines and resources with high-resolution motion
   vectors.
9. Reads back the initialization parameters and destroys the context.

If the initial runtime load fails, the suite probes all 40 direct dependency
modules and the delay-loaded D3D12 module from the SDK 3.0.2 import table.
Each dependency result records module loading, every required named export,
the exact Win32 errors, and elapsed time. The original XeSS loader error is
preserved. Each module is checked with `GetModuleHandleExW` (with an owned
reference) and `LoadLibraryExW` using `LOAD_LIBRARY_SEARCH_SYSTEM32`.
The three Visual C++ runtime DLLs are additionally checked through
`LoadPackagedLibrary`, independently of the system lookup. Every attempted
method and its error are recorded, together with the method selected for
export resolution. A failed lookup does not mark the module blocked when
another method succeeds. All acquired references are released after inspection
and exports are not invoked. Checks run on the target device.
A failed dependency load can itself have a transitive cause. Successful export
resolution proves availability only, not permission or functional compatibility.
If the SDK runtime changes, its recorded import matrix must be updated.

This diagnostic does not submit an upscaling frame. A successful initialization
proves that the packaged runtime, D3D12 driver, pipeline compilation, and
internal resource allocation are usable; it does not validate image quality or
per-frame execution.

### Intel XeLL and XeSS Frame Generation probes

The XeLL probe dynamically loads `libxell.dll`, resolves required exports,
queries its version, creates a D3D12 context, enables and reads back low-latency
configuration, then restores pass-through mode. It tests pass-through sleep,
synthetic simulation markers, and the 64-entry frame report query. No rendered
or presented frame is implied, and latency reduction is not measured.

The XeSS-FG probe also loads `libxess_fg.dll`, queries its version, creates a
context (which checks device support), connects a XeLL context, queries the
maximum interpolation count and 1080p memory requirements, and builds pipelines
synchronously. The blocking build return code records completion; a subsequent
pipeline status query is explicitly skipped after the Xbox runtime returned
`ERROR_WRONG_CALL_ORDER` for that sequence. Asynchronous polling is not tested.
FG is destroyed before XeLL. Failed teardown retains dependencies to avoid
unloading code still owned by a context.

Proxy initialization and frame execution are explicitly skipped: the SDK
recreates a native window swap chain, while this application has a XAML-only
interface. No D3D11 renderer is restored. Both suite completions remain
`INCONCLUSIVE` if their probes succeed, or reflect actual failures; they never
claim verified latency reduction or generated frames. Exact SDK result codes,
HRESULT/Win32 errors, and per-call durations are exported. Tests run in the
background and are serialized because the SDK allows only one proxy context.

## Result statuses

| Status | Meaning |
|---|---|
| `PASS` | The described operation completed successfully. |
| `FAIL` | The API was accessible, but observed behavior did not match the expected semantics. |
| `BLOCKED` | The DLL, class, API, permission, service, or required subsystem was inaccessible. |
| `SKIPPED` | The operation was not executed because a prerequisite was unavailable, it was not applicable, or it is outside the implemented probe. |
| `INCONCLUSIVE` | Available evidence is insufficient to establish the full capability, measured effect, or exact limit. |

Suite-completion entries aggregate the status of their preceding stages. Inspect the first blocked or failed stage for the useful error code.
XeLL and XeSS-FG completions remain `INCONCLUSIVE` when all implemented probes
pass because actual latency reduction and frame generation have not been tested.

## Building

Requirements:

- Visual Studio with C++ UWP development support
- Windows 11 SDK 10.0.26100.0
- MSVC platform toolset v145, or a compatible installed toolset selected in the project

Open `Teste-HW.vcxproj` in Visual Studio, select `x64`, and build either the Debug or Release configuration.

The equivalent developer-command-prompt invocation is:

```powershell
MSBuild.exe Teste-HW.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

The generated package is placed under:

```text
AppPackages\Teste-HW\Teste-HW_1.0.0.0_x64_Debug_Test\
```

## Signing

The repository contains `Teste-HW_TemporaryKey.pfx` for development testing. It is not a production signing identity.

After every package rebuild, sign the generated MSIX again because rebuilding replaces the signed package:

```powershell
signtool.exe sign /fd SHA256 /f Teste-HW_TemporaryKey.pfx `
  AppPackages\Teste-HW\Teste-HW_1.0.0.0_x64_Debug_Test\Teste-HW_1.0.0.0_x64_Debug.msix

signtool.exe verify /pa /v `
  AppPackages\Teste-HW\Teste-HW_1.0.0.0_x64_Debug_Test\Teste-HW_1.0.0.0_x64_Debug.msix
```

## Running and exporting reports

Install the signed x64 package on the target Xbox or Windows device. Run each suite from the bottom application bar, then use **Export TXT** or **Export JSON**.

Reports are written as UTF-8 files in the package's local application-data folder. The application displays the complete output path after export.

For reliable interpretation, keep these evidence levels separate:

- Successful compilation
- Successful package generation and signature verification
- Installed AppContainer execution
- Successful API activation
- Successful functional operation
- Successful execution on Xbox hardware

Only a report produced on the target console establishes Xbox runtime behavior.

## Current Xbox observations

Reports produced during development on an Xbox Series S have shown that:

- x64 JIT code generation and RW-to-RX transitions work.
- Structured exceptions, guard pages, and access-violation recovery work.
- File mappings, executable views, affinity, priorities, and SMP execution work.
- D3D12 device creation works.
- AMD SVM is masked from CPUID: the SVM bit is clear and leaf `0x8000000A`
  reports zero revision, zero ASIDs, and zero feature flags.
- Direct probes of `VMRUN`, `VMLOAD`, `VMSAVE`, `STGI`, `CLGI`, `SKINIT`, and
  `INVLPGA` are blocked and delivered to the process as
  `EXCEPTION_ACCESS_VIOLATION` (`0xC0000005`).
- No KVM bridge is exposed at `\\.\KVM`, `\\.\Global\KVM`, or
  `\\?\GLOBALROOT\Device\KVM`; all tested paths return
  `ERROR_FILE_NOT_FOUND`.
- WHPX, `vmcompute.dll`, and Hyper-V WMI are not accessible through the tested paths.
- The system `DirectML.dll` is not loadable through the tested AppContainer path.

Taken together, the tested hardware-virtualization routes—direct AMD-V, KVM,
WHPX, Host Compute Service, and Hyper-V WMI—are unavailable to this
AppContainer. A Linux runtime on this platform therefore needs a user-mode
interpreter or dynamic binary translator, software MMU and privileged-instruction
modeling, and native threads for guest SMP.

These observations are device-, OS-, package-, and configuration-specific. Re-run the probes after system updates or manifest changes instead of treating them as permanent platform guarantees.

### Intel SDK results confirmed on October 11, 2026

The following results come from the target-console reports
`Teste-HW_20261011_114141_UTC.txt` (super resolution) and
`Teste-HW_20261011_121205_UTC.txt` (XeLL and frame generation).
The SDK distribution is named 3.0.2; runtime API versions are queried separately.

| Runtime | Version reported by API | Confirmed operations | Still untested |
|---|---|---|---|
| XeSS-SR (`libxess.dll`) | 2.0.2 | Loading, export resolution, D3D12 device, Shader Model 6.4, context creation, driver check, 1080p Performance initialization and teardown | `xessD3D12Execute`, GPU completion, output readback and image quality |
| XeLL (`libxell.dll`) | 1.3.2 | Context creation, sleep-mode configuration/readback, pass-through sleep, synthetic simulation markers, frame-report query and teardown | Effective latency reduction with rendered/presented frames |
| XeSS-FG (`libxess_fg.dll`) | 1.3.1 | Context creation, XeLL binding, resource queries, blocking pipeline compilation and teardown | Proxy swap-chain initialization, interpolation and presentation |

The original XeSS loader failure (`ERROR_MOD_NOT_FOUND`, 126) was resolved by
bundling the three Release OneCore Visual C++ runtime DLLs. The corrected
dependency probe also resolved the required SHLWAPI, SETUPAPI, ADVAPI32 and
Device Configuration exports on the console; that result alone does not verify
permission or behavior when those functions are invoked.

For XeSS-SR at 1920x1080 Performance, the runtime reported an optimal input of
835x470 and initialized successfully. The XeSS-FG runtime reported one maximum
interpolation and the following 1080p resource requirements:

- 326 descriptors.
- 15,859,712 bytes of temporary buffer heap.
- 103,940,096 bytes of temporary texture heap.
- 8,978,432 bytes of constant buffer storage.

These are queried requirements, not measured total GPU memory consumption.
The latest blocking FG pipeline build completed in 7.34 seconds; the entire
probe took 7.39 seconds. These timings do not measure per-frame performance.
The latest report contains 41 `PASS`, two `SKIPPED`, one `INCONCLUSIVE`, and
no `FAIL` or `BLOCKED` results. The pipeline-status query is deliberately skipped
after the blocking build: an earlier run returned `ERROR_WRONG_CALL_ORDER`
for that subsequent query. Non-blocking compilation and polling remain untested.

## Safety notes

- The application does not intentionally exhaust physical memory or commit.
- Large-memory tests reserve address space without committing the full range.
- The thread-capacity probe stops at its configured safety ceiling.
- Administrative COM components are activated only; no tasks or system settings are created or changed.
- Virtualization probes clean up any partition or vCPU objects they successfully create.


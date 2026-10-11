# Teste-HW

Teste-HW is an x64 UWP/AppContainer hardware capability diagnostic application designed primarily for Xbox Series consoles in Developer Mode. Its purpose is to determine which low-level Windows facilities are actually usable from a packaged application, especially the facilities needed by an emulator, dynamic binary translator, or user-mode Linux runtime.

The application performs functional probes rather than relying only on DLL presence, API contracts, or CPUID feature bits. Every probe records a status, duration, HRESULT, Win32 error, and diagnostic details where applicable. Reports can be exported as UTF-8 text or JSON.

> This project is a diagnostic tool. A `PASS` result means that the specific operation described by that result succeeded. It does not imply that every operation offered by the same subsystem is available.

## Target

The interface uses XAML, without a D3D11 renderer or FPS overlay. Xbox layout
scaling is disabled at launch. Graphics devices and temporary D3D12 swapchains
are created only when a diagnostic suite or an animated SDK sample is requested.

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

Proxy initialization and frame execution are explicitly skipped by these
initialization-only probes; use the separate animated sample sequence below
to attempt presentation. No D3D11 renderer is restored. Both suite completions remain
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
pass because those initialization-only probes do not test actual latency
reduction or frame generation. Animated samples have separate results.

## Animated XeSS SDK samples

Each sample traces baseline D3D12 device creation with the default adapter
(the same adapter/feature-level arguments used by the DirectML probe) and
explicit hardware adapters before changing debug-layer configuration. It also
records `D3D12GetDebugInterface`, debug-layer enablement, DXGI factory flags,
and default-adapter creation after configuration. Successful probe devices
report their adapter LUID and `GetDeviceRemovedReason`; failed creation cannot
provide a device-removal reason when no device was returned. Baseline devices
are released before debug-layer enablement. The baseline does not guarantee
that the process debug layer was previously disabled, and these probes do not
execute a DirectML workload. Use a freshly restarted app for comparison.

The SDK 3.0.2 scenes have independent buttons; each runs only its selected sample:

1. **Sample XeSS-FG (10s)** / `basic_sample_frame_generation`: renders color, depth and motion vectors,
   tags resources/constants, enables XeLL pacing and XeSS-FG interpolation,
   presents through the SDK proxy and queries each last-present status.
2. **Sample XeLL (10s)** / `basic_sample_low_latency`: renders the SDK scene with low-latency sleep,
   simulation/render/present markers and frame-report queries.
3. **Sample XeSS-SR DX12 (10s)** / `basic_sample_super_resolution_dx12`: renders the jittered, lower-resolution
   SDK scene, executes XeSS-SR and displays the upscaled output.

Each successfully initialized scene runs for **10 seconds**. Initialization,
pipeline compilation and teardown are outside that animation interval, so the
selected test can take longer than 10 seconds. An initialization failure records
`BLOCKED` with the exact HRESULT/Win32 or numeric SDK result; it cannot run a
fictitious ten-second animation. No other sample starts automatically.

All three samples attempt to use composition swapchains attached to a temporary XAML panel.
FG creates its input with `CreateSwapChainForComposition`, transfers ownership
to `xefgSwapChainD3D12InitFromSwapChain`, and attaches the SDK-returned swapchain
to XAML if initialization succeeds. The app no longer requires a CoreWindow HWND
or directly creates an HWND swapchain. However, the SDK internally releases the
input swapchain and recreates one for its underlying window. The Xbox trace
confirmed that this internal HWND path rejects the composition swapchain before
XAML attachment. SDK initialization and attachment failures are recorded
separately; context creation alone is not successful frame generation.

FG traces include every `DXGI_SWAP_CHAIN_DESC1` field, `GetHwnd` and
`GetFullscreenDesc` results, and initialization flags/heap pointers. Around SDK
initialization it captures new D3D12/DXGI info-queue messages (up to 64 per queue,
using existing retrieval filters). Debug-interface availability and HRESULTs are
recorded; absence of messages does not prove that the SDK call is valid.
`DXGIGetDebugInterface1` is resolved dynamically from the loaded DXGI module,
without adding a required debug-DLL import. The original SDK failure is retained.

Results are added immediately after each sample finishes and are included in
TXT/JSON exports. They include elapsed animation time, application frame count,
XeSS execute/XeLL sleep calls, generated-frame count, SDK warnings, output
readbacks and GPU-drain status. The application output is read back near 0, 1,
and 9 seconds; hashes and nonuniform pixel counts check that rendered content
exists and changes. GPU fences have a five-second timeout. This is not a
pixel-perfect image-quality comparison or proof of physical display scanout.

`PASS` requires the full interval, changing/nonuniform GPU output, successful
presentation and teardown, plus executed upscaling, active XeLL sleep with
completed timing reports, or
reported generated frames as appropriate. It does **not** prove latency
improvement; that requires external input-to-display measurements. Suspension,
hidden-window/unload cancellation or absent generated frames is not a pass.
Native faults, GPU timeouts/device loss or cleanup failure retain potentially
in-flight resources and block further samples until the application is restarted.
The SDK probe/sample paths share a lock to prevent simultaneous FG contexts.

Sources are adapted under `Samples/LL`, `Samples/FG` and `Samples/SR`.
Scene HLSL is compiled offline by `Samples/SampleShaders.targets` and the
19 shader blobs are packaged, avoiding desktop file-based runtime compilation.
The sibling SDK remains required for its headers, helper header and runtimes.
The SDK source copyright notices and packaged license notices are retained.
Build/package verification is separate from running the animations on Xbox.

### Persistent crash evidence

Before graphics or SDK initialization, each button opens a UTF-8, write-through
trace in the app's LocalState folder:

- `XeSS_sample_FG_trace.txt`
- `XeSS_sample_LL_trace.txt`
- `XeSS_sample_SR_trace.txt`

Traces are appended, not overwritten, and checkpoints are flushed synchronously.
They include initialization stages, DLL base addresses, SDK entry/return codes,
the first rendered frame, one-second progress, readbacks, cleanup and the final
result. Recoverable calling-thread native exceptions record their SEH code,
instruction address and access parameters. Cleanup is not retried after an
unsuccessful destroy. The FG cleanup now checks for an already released swapchain.
SDK DLLs are cached for the process lifetime rather than unloaded between
samples, avoiding DLL-detach races with SDK/driver callbacks; successful tests
still destroy their contexts and graphics resources.

An optional dynamically resolved vectored exception observer records native
first-chance faults, including on SDK threads, but never suppresses those faults.
`FIRST_CHANCE` does not mean a fatal crash: a library may handle that exception.
This observer is not assumed available on Xbox. Calling-thread SEH protection
remains independent of it. Fail-fast, stack overflow, OS termination or a fault
on an SDK-owned thread may still close the process; the last flushed checkpoint
remains the available evidence, not necessarily a complete crash diagnosis.

On next launch, the last 64 KiB of each trace is imported into the report.
An absent final `RUN END` is marked inconclusive/interrupted. Export TXT or JSON
after reopening the app to retrieve that recovered evidence, or download the
trace files directly from LocalState through Device Portal. The current trace
path is also included in each sample result.

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
| XeSS-SR (`libxess.dll`) | 2.0.2 | Initialization probes; 10-second animation, 570 execute/present calls, three changing GPU readbacks and teardown at 1280x720 | Pixel-perfect image quality and physical scanout |
| XeLL (`libxell.dll`) | 1.3.2 | Initialization probes; 10-second animation, 557 sleep/present calls, valid timing reports, three changing GPU readbacks and teardown | Measured input-to-display latency improvement |
| XeSS-FG (`libxess_fg.dll`) | 1.3.1 | Context creation, XeLL binding, resource queries, blocking pipeline compilation and teardown | Animation is BLOCKED at internal swapchain creation; interpolation and presentation remain untested |

### Animated Xbox results and XeSS-FG presentation limitation

`Teste-HW_20261011_134655_UTC.txt` and the accompanying SR/LL traces confirm
`PASS` for SR (10.016 seconds, 570 frames) and LL (10.006 seconds, 557 frames).
Both produced changing, nonuniform GPU readbacks and completed GPU draining.
These are functional results, not image-quality or latency-improvement benchmarks.

The FG trace ending at `2026-10-11T14:01:08.381Z` confirms successful D3D12,
composition-swapchain and context creation, followed by
`xefgSwapChainD3D12InitFromSwapChain` returning `-18`
(`XEFG_SWAPCHAIN_RESULT_ERROR_DXGI_INVALID_CALL`). New DXGI debug messages during
the SDK call report HWND-only `GetHwnd`/`GetFullscreenDesc` queries and an internal
`CreateSwapChain` call without a target window (`0x887A0001`). The earlier direct
HWND initialization path returned access denied (`0x80070005`, SDK result `-17`).
No frames were generated; cleanup succeeded without a crash.

Inspection of the SDK 3.0.2 public headers, guide and DLL export table found no
documented flag to preserve the input swapchain or an API to generate frames into
an application-owned texture without SDK-controlled presentation. Both documented
initialization paths create a swapchain for a window. `SetUiCompositionState`
controls HUD composition, not XAML/UWP presentation. Undocumented exports such as
`xefgSwapChainSetParameterP` have no supplied ABI or parameter contract and are not
treated as usable alternatives. See the [Intel initialization guide](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md#initialization)
and the matching guide in the local SDK distribution.

A presentation-decoupled API would accept frame textures, motion vectors, depth
and camera/frame constants, and write interpolation output to a D3D12 texture;
the app would synchronize and present it through its UWP composition swapchain.
This is a desired integration model, not an API exposed by the inspected SDK.
Supporting it requires a suitable Intel API/runtime or modifiable implementation
source. The distributed DLL implementation source is absent; sample sources do
not provide the internal presentation layer. The SDK binary remains unmodified.

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
The earlier initialization-only blocking FG pipeline build completed in 7.34 seconds; the entire
probe took 7.39 seconds. These timings do not measure per-frame performance.
That earlier initialization-only report contains 41 `PASS`, two `SKIPPED`, one `INCONCLUSIVE`, and
no `FAIL` or `BLOCKED` results. The pipeline-status query is deliberately skipped
after the blocking build: an earlier run returned `ERROR_WRONG_CALL_ORDER`
for that subsequent query. Non-blocking compilation and polling remain untested.

## Safety notes

- The application does not intentionally exhaust physical memory or commit.
- Large-memory tests reserve address space without committing the full range.
- The thread-capacity probe stops at its configured safety ceiling.
- Administrative COM components are activated only; no tasks or system settings are created or changed.
- Virtualization probes clean up any partition or vCPU objects they successfully create.


#include "pch.h"
#include "XessDiagnostics.h"

#include <d3d12.h>
#include <xess_d3d12.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <iomanip>
#include <sstream>

namespace
{
	using Microsoft::WRL::ComPtr;
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	using D3D12CreateDeviceFn = HRESULT (WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	using XessGetVersionFn = xess_result_t (*)(xess_version_t*);
	using XessD3D12CreateContextFn = xess_result_t (*)(ID3D12Device*, xess_context_handle_t*);
	using XessIsOptimalDriverFn = xess_result_t (*)(xess_context_handle_t);
	using XessGetIntelXeFXVersionFn = xess_result_t (*)(xess_context_handle_t, xess_version_t*);
	using XessGetPropertiesFn = xess_result_t (*)(xess_context_handle_t, const xess_2d_t*, xess_properties_t*);
	using XessGetOptimalInputResolutionFn = xess_result_t (*)(xess_context_handle_t, const xess_2d_t*,
		xess_quality_settings_t, xess_2d_t*, xess_2d_t*, xess_2d_t*);
	using XessD3D12InitFn = xess_result_t (*)(xess_context_handle_t, const xess_d3d12_init_params_t*);
	using XessD3D12GetInitParamsFn = xess_result_t (*)(xess_context_handle_t, xess_d3d12_init_params_t*);
	using XessDestroyContextFn = xess_result_t (*)(xess_context_handle_t);

	struct XessApi
	{
		XessGetVersionFn getVersion = nullptr;
		XessD3D12CreateContextFn createContext = nullptr;
		XessIsOptimalDriverFn isOptimalDriver = nullptr;
		XessGetIntelXeFXVersionFn getIntelXeFxVersion = nullptr;
		XessGetPropertiesFn getProperties = nullptr;
		XessGetOptimalInputResolutionFn getOptimalInputResolution = nullptr;
		XessD3D12InitFn initialize = nullptr;
		XessD3D12GetInitParamsFn getInitParams = nullptr;
		XessDestroyContextFn destroyContext = nullptr;
	};

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	const wchar_t* XessResultName(xess_result_t value)
	{
		switch (value)
		{
		case XESS_RESULT_SUCCESS: return L"XESS_RESULT_SUCCESS";
		case XESS_RESULT_WARNING_NONEXISTING_FOLDER: return L"XESS_RESULT_WARNING_NONEXISTING_FOLDER";
		case XESS_RESULT_WARNING_OLD_DRIVER: return L"XESS_RESULT_WARNING_OLD_DRIVER";
		case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return L"XESS_RESULT_ERROR_UNSUPPORTED_DEVICE";
		case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return L"XESS_RESULT_ERROR_UNSUPPORTED_DRIVER";
		case XESS_RESULT_ERROR_UNINITIALIZED: return L"XESS_RESULT_ERROR_UNINITIALIZED";
		case XESS_RESULT_ERROR_INVALID_ARGUMENT: return L"XESS_RESULT_ERROR_INVALID_ARGUMENT";
		case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return L"XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY";
		case XESS_RESULT_ERROR_DEVICE: return L"XESS_RESULT_ERROR_DEVICE";
		case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return L"XESS_RESULT_ERROR_NOT_IMPLEMENTED";
		case XESS_RESULT_ERROR_INVALID_CONTEXT: return L"XESS_RESULT_ERROR_INVALID_CONTEXT";
		case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return L"XESS_RESULT_ERROR_OPERATION_IN_PROGRESS";
		case XESS_RESULT_ERROR_UNSUPPORTED: return L"XESS_RESULT_ERROR_UNSUPPORTED";
		case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return L"XESS_RESULT_ERROR_CANT_LOAD_LIBRARY";
		case XESS_RESULT_ERROR_WRONG_CALL_ORDER: return L"XESS_RESULT_ERROR_WRONG_CALL_ORDER";
		case XESS_RESULT_ERROR_UNKNOWN: return L"XESS_RESULT_ERROR_UNKNOWN";
		default: return L"XESS_RESULT_UNRECOGNIZED";
		}
	}

	DiagnosticStatus XessStatus(xess_result_t value)
	{
		if (value == XESS_RESULT_SUCCESS) return DiagnosticStatus::Pass;
		if (value > XESS_RESULT_SUCCESS) return DiagnosticStatus::Inconclusive;
		return DiagnosticStatus::Blocked;
	}

	DiagnosticResult MakeResult(const wchar_t* name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Graphics / Intel XeSS";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	// Exact named imports of SDK 3.0.2 libxess.dll, SHA-256
	// 251659DD84A3E84DE67C886A4186E01F3ECA49B00641906FE38BB6B807E5D5B7.
	struct XessDependency
	{
		const wchar_t* module;
		bool delayLoad;
		std::vector<const char*> symbols;
	};

	const XessDependency XessDependencies[] = {
		{ L"SHLWAPI.dll", false, {
			"PathRemoveFileSpecW",
		} },
		{ L"SETUPAPI.dll", false, {
			"CM_Locate_DevNodeW",
			"CM_Get_Device_ID_List_SizeW",
			"CM_Get_Device_ID_ListW",
			"SetupGetInfDriverStoreLocationW",
		} },
		{ L"dxgi.dll", false, {
			"CreateDXGIFactory1",
		} },
		{ L"KERNEL32.dll", false, {
			"LoadLibraryW",
			"LoadLibraryExW",
			"K32GetModuleFileNameExW",
			"OutputDebugStringA",
			"FreeLibrary",
			"GetLastError",
			"GetModuleFileNameA",
			"GetModuleFileNameW",
			"GetModuleHandleExW",
			"GetProcAddress",
			"LoadLibraryExA",
			"LocalFree",
			"FormatMessageA",
			"GetModuleHandleExA",
			"HeapAlloc",
			"HeapFree",
			"GetProcessHeap",
			"LoadLibraryA",
			"GetCurrentProcess",
			"GetModuleHandleA",
			"K32EnumProcessModules",
		} },
		{ L"ole32.dll", false, {
			"StringFromGUID2",
		} },
		{ L"ADVAPI32.dll", false, {
			"RegGetValueA",
			"RegQueryInfoKeyA",
			"RegOpenKeyExA",
			"RegEnumKeyExA",
			"RegCloseKey",
		} },
		{ L"MSVCP140.dll", false, {
			"?write@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEBD_J@Z",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@_J@Z",
			"?_Fiopen@std@@YAPEAU_iobuf@@PEB_WHH@Z",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@_K@Z",
			"?id@?$codecvt@DDU_Mbstatet@@@std@@2V0locale@2@A",
			"??0?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z",
			"??1?$basic_istream@DU?$char_traits@D@std@@@std@@UEAA@XZ",
			"?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@PEAD_J@Z",
			"?seekg@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@_JH@Z",
			"?tellg@?$basic_istream@DU?$char_traits@D@std@@@std@@QEAA?AV?$fpos@U_Mbstatet@@@2@XZ",
			"?_Reset@_ContextCallback@details@Concurrency@@AEAAXXZ",
			"?_Xlength_error@std@@YAXPEBD@Z",
			"?_Xbad_alloc@std@@YAXXZ",
			"_Mbrtowc",
			"?_Getcvt@_Locinfo@std@@QEBA?AU_Cvtvec@@XZ",
			"?_W_Getdays@_Locinfo@std@@QEBAPEBGXZ",
			"?_W_Getmonths@_Locinfo@std@@QEBAPEBGXZ",
			"?uncaught_exception@std@@YA_NXZ",
			"??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAA@XZ",
			"??1?$basic_streambuf@DU?$char_traits@D@std@@@std@@UEAA@XZ",
			"?sputc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAAHD@Z",
			"?sputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEAA_JPEBD_J@Z",
			"?_Pninc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAPEADXZ",
			"??1?$basic_ios@DU?$char_traits@D@std@@@std@@UEAA@XZ",
			"?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QEAAXH_N@Z",
			"??0?$basic_ios@DU?$char_traits@D@std@@@std@@IEAA@XZ",
			"?_Osfx@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAXXZ",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@P6AAEAVios_base@1@AEAV21@@Z@Z",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@M@Z",
			"?flush@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@XZ",
			"??0?$basic_iostream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z",
			"??1?$basic_iostream@DU?$char_traits@D@std@@@std@@UEAA@XZ",
			"?setprecision@std@@YA?AU?$_Smanip@_J@1@_J@Z",
			"?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UEAAXXZ",
			"?_Unlock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UEAAXXZ",
			"?imbue@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAXAEBVlocale@2@@Z",
			"?setbuf@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAPEAV12@PEAD_J@Z",
			"?showmanyc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JXZ",
			"?sync@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHXZ",
			"?uflow@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAAHXZ",
			"?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JPEAD_J@Z",
			"?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MEAA_JPEBD_J@Z",
			"?_Xout_of_range@std@@YAXPEBD@Z",
			"??0?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z",
			"??1?$basic_ostream@DU?$char_traits@D@std@@@std@@UEAA@XZ",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@I@Z",
			"?_Xbad_function_call@std@@YAXXZ",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@K@Z",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@H@Z",
			"_Mtx_init_in_situ",
			"_Mtx_destroy_in_situ",
			"_Mtx_lock",
			"_Mtx_unlock",
			"?_Throw_C_error@std@@YAXH@Z",
			"?widen@?$basic_ios@DU?$char_traits@D@std@@@std@@QEBADD@Z",
			"??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV01@P6AAEAV01@AEAV01@@Z@Z",
			"?put@?$basic_ostream@DU?$char_traits@D@std@@@std@@QEAAAEAV12@D@Z",
			"?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A",
			"_Xtime_get_ticks",
			"_Query_perf_counter",
			"_Query_perf_frequency",
			"_Mtx_current_owns",
			"_Cnd_timedwait",
			"?_Throw_Cpp_error@std@@YAXH@Z",
			"?_Syserror_map@std@@YAPEBDH@Z",
			"?_Throw_future_error@std@@YAXAEBVerror_code@1@@Z",
			"?__ExceptionPtrCreate@@YAXPEAX@Z",
			"?__ExceptionPtrDestroy@@YAXPEAX@Z",
			"?__ExceptionPtrCopy@@YAXPEAXPEBX@Z",
			"?__ExceptionPtrAssign@@YAXPEAXPEBX@Z",
			"?__ExceptionPtrToBool@@YA_NPEBX@Z",
			"?__ExceptionPtrCurrentException@@YAXPEAX@Z",
			"?__ExceptionPtrRethrow@@YAXPEBX@Z",
			"_Cnd_init_in_situ",
			"_Cnd_destroy_in_situ",
			"_Cnd_wait",
			"_Cnd_broadcast",
			"_Cnd_register_at_thread_exit",
			"_Cnd_unregister_at_thread_exit",
			"?_Schedule_chore@details@Concurrency@@YAHPEAU_Threadpool_chore@12@@Z",
			"?_Release_chore@details@Concurrency@@YAXPEAU_Threadpool_chore@12@@Z",
			"?_ReportUnobservedException@details@Concurrency@@YAXXZ",
			"?GetCurrentThreadId@platform@details@Concurrency@@YAJXZ",
			"?_CallInContext@_ContextCallback@details@Concurrency@@QEBAXV?$function@$$A6AXXZ@std@@_N@Z",
			"?_Init@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEAAXXZ",
			"?_Capture@_ContextCallback@details@Concurrency@@AEAAXXZ",
			"?ReportUnhandledError@_ExceptionHolder@details@Concurrency@@AEAAXXZ",
			"??0task_continuation_context@Concurrency@@AEAA@XZ",
			"?_LogScheduleTask@_TaskEventLogger@details@Concurrency@@QEAAX_N@Z",
			"?_LogCancelTask@_TaskEventLogger@details@Concurrency@@QEAAXXZ",
			"?_LogTaskCompleted@_TaskEventLogger@details@Concurrency@@QEAAXXZ",
			"?_LogTaskExecutionCompleted@_TaskEventLogger@details@Concurrency@@QEAAXXZ",
			"?_LogWorkItemStarted@_TaskEventLogger@details@Concurrency@@QEAAXXZ",
			"?_LogWorkItemCompleted@_TaskEventLogger@details@Concurrency@@QEAAXXZ",
			"?_Rethrow_future_exception@std@@YAXVexception_ptr@1@@Z",
			"?eback@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ",
			"?gptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ",
			"?pbase@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ",
			"?pptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ",
			"?egptr@?$basic_streambuf@DU?$char_traits@D@std@@@std@@IEBAPEADXZ",
			"??4?$_Yarn@D@std@@QEAAAEAV01@PEBD@Z",
			"??Bid@locale@std@@QEAA_KXZ",
			"?_New_Locimp@_Locimp@locale@std@@CAPEAV123@AEBV123@@Z",
			"?_Locimp_Addfac@_Locimp@locale@std@@CAXPEAV123@PEAVfacet@23@_K@Z",
			"?_Init@locale@std@@CAPEAV_Locimp@12@_N@Z",
			"?in@?$codecvt@_WDU_Mbstatet@@@std@@QEBAHAEAU_Mbstatet@@PEBD1AEAPEBDPEA_W3AEAPEA_W@Z",
			"?out@?$codecvt@_WDU_Mbstatet@@@std@@QEBAHAEAU_Mbstatet@@PEB_W1AEAPEB_WPEAD3AEAPEAD@Z",
			"??0?$codecvt@_WDU_Mbstatet@@@std@@QEAA@_K@Z",
			"??1?$codecvt@_WDU_Mbstatet@@@std@@MEAA@XZ",
			"?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ",
			"?_Incref@facet@locale@std@@UEAAXXZ",
			"?id@?$codecvt@_WDU_Mbstatet@@@std@@2V0locale@2@A",
			"??0_Lockit@std@@QEAA@H@Z",
			"??1_Lockit@std@@QEAA@XZ",
			"?_Winerror_map@std@@YAHH@Z",
			"?_Getgloballocale@locale@std@@CAPEAV_Locimp@12@XZ",
			"?always_noconv@codecvt_base@std@@QEBA_NXZ",
			"?in@?$codecvt@DDU_Mbstatet@@@std@@QEBAHAEAU_Mbstatet@@PEBD1AEAPEBDPEAD3AEAPEAD@Z",
			"?out@?$codecvt@DDU_Mbstatet@@@std@@QEBAHAEAU_Mbstatet@@PEBD1AEAPEBDPEAD3AEAPEAD@Z",
			"?unshift@?$codecvt@DDU_Mbstatet@@@std@@QEBAHAEAU_Mbstatet@@PEAD1AEAPEAD@Z",
			"?_Getcat@?$codecvt@DDU_Mbstatet@@@std@@SA_KPEAPEBVfacet@locale@2@PEBV42@@Z",
			"?getloc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QEBA?AVlocale@2@XZ",
			"?_Fiopen@std@@YAPEAU_iobuf@@PEBDHH@Z",
		} },
		{ L"api-ms-win-devices-config-l1-1-1.dll", false, {
			"CM_MapCrToWin32Err",
			"CM_Get_DevNode_PropertyW",
		} },
		{ L"api-ms-win-core-errorhandling-l1-1-0.dll", false, {
			"UnhandledExceptionFilter",
			"SetUnhandledExceptionFilter",
			"RaiseException",
		} },
		{ L"api-ms-win-core-sysinfo-l1-1-0.dll", false, {
			"GetSystemInfo",
			"GetSystemTimeAsFileTime",
		} },
		{ L"api-ms-win-core-memory-l1-1-0.dll", false, {
			"VirtualProtect",
			"VirtualQuery",
		} },
		{ L"api-ms-win-core-libraryloader-l1-2-0.dll", false, {
			"GetModuleHandleW",
			"DisableThreadLibraryCalls",
		} },
		{ L"api-ms-win-core-synch-l1-2-0.dll", false, {
			"InitOnceBeginInitialize",
			"InitOnceComplete",
		} },
		{ L"api-ms-win-core-synch-l1-1-0.dll", false, {
			"SetEvent",
			"ReleaseSRWLockShared",
			"DeleteCriticalSection",
			"ResetEvent",
			"InitializeCriticalSectionAndSpinCount",
			"LeaveCriticalSection",
			"EnterCriticalSection",
			"WaitForSingleObjectEx",
			"AcquireSRWLockExclusive",
			"ReleaseSRWLockExclusive",
			"CreateEventW",
			"TryAcquireSRWLockShared",
			"TryAcquireSRWLockExclusive",
			"AcquireSRWLockShared",
		} },
		{ L"api-ms-win-core-processenvironment-l1-1-0.dll", false, {
			"GetCurrentDirectoryW",
			"SetCurrentDirectoryW",
		} },
		{ L"api-ms-win-core-file-l1-1-0.dll", false, {
			"GetFileInformationByHandle",
			"GetFinalPathNameByHandleW",
			"GetFullPathNameW",
			"GetFileAttributesW",
			"SetFileAttributesW",
			"SetFileInformationByHandle",
			"SetFilePointerEx",
			"SetFileTime",
			"GetFileAttributesExW",
			"GetDiskFreeSpaceExW",
			"FindNextFileW",
			"FindClose",
			"FindFirstFileExW",
			"CreateDirectoryW",
			"CreateFileW",
			"SetEndOfFile",
		} },
		{ L"api-ms-win-core-file-l1-2-0.dll", false, {
			"GetTempPathW",
		} },
		{ L"api-ms-win-core-file-l1-2-2.dll", false, {
			"AreFileApisANSI",
		} },
		{ L"api-ms-win-core-handle-l1-1-0.dll", false, {
			"CloseHandle",
		} },
		{ L"api-ms-win-core-io-l1-1-0.dll", false, {
			"DeviceIoControl",
		} },
		{ L"api-ms-win-core-file-l2-1-0.dll", false, {
			"MoveFileExW",
			"CreateSymbolicLinkW",
			"GetFileInformationByHandleEx",
			"CreateDirectoryExW",
			"CreateHardLinkW",
		} },
		{ L"api-ms-win-core-file-l2-1-2.dll", false, {
			"CopyFileW",
		} },
		{ L"api-ms-win-core-string-l1-1-0.dll", false, {
			"MultiByteToWideChar",
			"WideCharToMultiByte",
		} },
		{ L"api-ms-win-core-rtlsupport-l1-1-0.dll", false, {
			"RtlVirtualUnwind",
			"RtlCaptureContext",
			"RtlLookupFunctionEntry",
		} },
		{ L"api-ms-win-core-processthreads-l1-1-0.dll", false, {
			"GetStartupInfoW",
			"GetCurrentThreadId",
			"GetCurrentProcessId",
			"TerminateProcess",
		} },
		{ L"api-ms-win-core-processthreads-l1-1-1.dll", false, {
			"IsProcessorFeaturePresent",
		} },
		{ L"api-ms-win-core-debug-l1-1-0.dll", false, {
			"IsDebuggerPresent",
		} },
		{ L"api-ms-win-core-profile-l1-1-0.dll", false, {
			"QueryPerformanceCounter",
		} },
		{ L"api-ms-win-core-interlocked-l1-1-0.dll", false, {
			"InitializeSListHead",
		} },
		{ L"VCRUNTIME140.dll", false, {
			"__std_exception_destroy",
			"memcmp",
			"__std_type_info_destroy_list",
			"__C_specific_handler",
			"__current_exception_context",
			"__current_exception",
			"_purecall",
			"strchr",
			"wcsstr",
			"wcsrchr",
			"__std_exception_copy",
			"wcschr",
			"__std_type_info_compare",
			"memchr",
			"__RTDynamicCast",
			"memset",
			"__std_terminate",
			"memmove",
			"memcpy",
			"_CxxThrowException",
		} },
		{ L"VCRUNTIME140_1.dll", false, {
			"__CxxFrameHandler4",
		} },
		{ L"api-ms-win-crt-runtime-l1-1-0.dll", false, {
			"_errno",
			"_initterm",
			"_cexit",
			"_invalid_parameter_noinfo",
			"_crt_atexit",
			"_execute_onexit_table",
			"_initterm_e",
			"_initialize_onexit_table",
			"_initialize_narrow_environment",
			"_configure_narrow_argv",
			"_seh_filter_dll",
			"terminate",
			"abort",
			"_register_onexit_function",
			"_invalid_parameter_noinfo_noreturn",
			"_crt_at_quick_exit",
		} },
		{ L"api-ms-win-crt-math-l1-1-0.dll", false, {
			"ceilf",
		} },
		{ L"api-ms-win-crt-heap-l1-1-0.dll", false, {
			"calloc",
			"malloc",
			"_callnewh",
			"free",
		} },
		{ L"api-ms-win-crt-string-l1-1-0.dll", false, {
			"_stricmp",
			"strncmp",
			"strcat_s",
			"wcscpy_s",
			"strcpy_s",
		} },
		{ L"api-ms-win-crt-convert-l1-1-0.dll", false, {
			"atoi",
			"wcstol",
			"atof",
		} },
		{ L"api-ms-win-crt-filesystem-l1-1-0.dll", false, {
			"_splitpath_s",
			"_unlock_file",
			"_waccess",
			"_lock_file",
		} },
		{ L"api-ms-win-crt-environment-l1-1-0.dll", false, {
			"_wgetenv_s",
			"_dupenv_s",
			"getenv",
		} },
		{ L"api-ms-win-crt-stdio-l1-1-0.dll", false, {
			"fwrite",
			"__stdio_common_vsprintf_s",
			"fclose",
			"__acrt_iob_func",
			"__stdio_common_vfprintf",
			"__stdio_common_vsprintf",
			"fflush",
			"fopen",
			"fgetc",
			"ungetc",
			"setvbuf",
			"fgetpos",
			"_fseeki64",
			"fsetpos",
			"fread",
			"fputc",
			"_get_stream_buffer_pointers",
		} },
		{ L"api-ms-win-crt-locale-l1-1-0.dll", false, {
			"___lc_codepage_func",
		} },
		{ L"d3d12.dll", true, {
			"D3D12CreateRootSignatureDeserializer",
			"D3D12SerializeVersionedRootSignature",
			"D3D12SerializeRootSignature",
		} },
	};

	void ProbeDependencies(std::vector<DiagnosticResult>& results)
	{
		for (const auto& dependency : XessDependencies)
		{
			auto started = std::chrono::high_resolution_clock::now();
			SetLastError(ERROR_SUCCESS);
			// Acquire a reference even for existing modules so another thread cannot
			// unload the module while its exports are inspected.
			HMODULE existing = nullptr;
			const BOOL found = GetModuleHandleExW(0, dependency.module, &existing);
			const DWORD existingError = found ? ERROR_SUCCESS : GetLastError();
			SetLastError(ERROR_SUCCESS);
			HMODULE system = LoadLibraryExW(dependency.module, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			const DWORD systemError = system ? ERROR_SUCCESS : GetLastError();
			const bool runtime = std::wcscmp(dependency.module, L"MSVCP140.dll") == 0 ||
				std::wcscmp(dependency.module, L"VCRUNTIME140.dll") == 0 ||
				std::wcscmp(dependency.module, L"VCRUNTIME140_1.dll") == 0;
			HMODULE packaged = nullptr;
			DWORD packageError = ERROR_SUCCESS;
			if (runtime)
			{
				SetLastError(ERROR_SUCCESS);
				packaged = LoadPackagedLibrary(dependency.module, 0);
				packageError = packaged ? ERROR_SUCCESS : GetLastError();
			}
			HMODULE module = existing ? existing : (packaged ? packaged : system);
			const DWORD loadError = runtime ? packageError : systemError;
			std::wstring details = L"Module=" + std::wstring(dependency.module) +
				L"; ImportKind=" + (dependency.delayLoad ? L"delay-load" : L"direct") +
				L"; RequiredSymbols=" +
				std::to_wstring(dependency.symbols.size()) + L"; ";
			details += L"GetModuleHandleExW.Resolved=" + std::wstring(existing ? L"true" : L"false") +
				L", Win32Error=" + std::to_wstring(existingError) + L" (" + Hex32(existingError) + L"); ";
			details += L"LoadLibraryExW(SYSTEM32).Resolved=" + std::wstring(system ? L"true" : L"false") +
				L", Win32Error=" + std::to_wstring(systemError) + L" (" + Hex32(systemError) + L"); ";
			if (runtime)
				details += L"LoadPackagedLibrary.Resolved=" + std::wstring(packaged ? L"true" : L"false") +
					L", Win32Error=" + std::to_wstring(packageError) + L" (" + Hex32(packageError) + L"); ";
			else
				details += L"LoadPackagedLibrary=not-applicable-system-module; ";
			details += L"SelectedLoader=" + std::wstring(existing ? L"GetModuleHandleExW" :
				(packaged ? L"LoadPackagedLibrary" : (system ? L"LoadLibraryExW(SYSTEM32)" : L"none"))) + L"; ";
			DWORD firstError = module ? ERROR_SUCCESS : loadError;
			size_t missing = 0;
			if (module)
			{
				for (const auto* symbol : dependency.symbols)
				{
					SetLastError(ERROR_SUCCESS);
					const FARPROC address = GetProcAddress(module, symbol);
					const DWORD error = address ? ERROR_SUCCESS : GetLastError();
					std::wstring wideSymbol(symbol, symbol + std::strlen(symbol));
					details += L"Symbol=" + wideSymbol + L", Resolved=" +
						(address ? L"true" : L"false") + L", Win32Error=" +
						std::to_wstring(error) + L" (" + Hex32(error) + L"); ";
					if (!address)
					{
						if (missing == 0) firstError = error;
						++missing;
					}
				}
			}
			else
			{
				details += L"LoadWin32Error=" + std::to_wstring(loadError) +
					L" (" + Hex32(loadError) +
					L"); SymbolResolutionSkipped=true; a falha pode ser transitiva. ";
			}
			details += L"MissingSymbols=" + std::to_wstring(missing) +
				L"; FunctionExecutionTested=false.";
			// Each successful acquisition owns one reference, even if handles match.
			if (packaged) FreeLibrary(packaged);
			if (system) FreeLibrary(system);
			if (existing) FreeLibrary(existing);
			auto result = MakeResult(dependency.module,
				module && missing == 0 ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				module ? (missing == 0 ? L"O módulo carregou e todos os imports exigidos foram resolvidos."
					: L"O módulo carregou, mas faltam símbolos exigidos pelo XeSS.")
					: L"O loader não conseguiu carregar esta dependência do XeSS.", details);
			result.category = L"Graphics / Intel XeSS / Dependencies";
			result.hasWin32Error = true;
			result.win32Error = firstError;
			result.durationMicroseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::high_resolution_clock::now() - started).count());
			results.push_back(std::move(result));
		}
	}

	DiagnosticResult XessResult(const wchar_t* name, xess_result_t value,
		const wchar_t* success, const wchar_t* failure, const std::wstring& extra = L"")
	{
		auto status = XessStatus(value);
		std::wstring summary = value == XESS_RESULT_SUCCESS ? success : failure;
		if (value > XESS_RESULT_SUCCESS)
			summary = L"A chamada XeSS terminou com um aviso não fatal.";
		return MakeResult(name, status, summary,
			L"XeSSResult=" + std::to_wstring(static_cast<int32_t>(value)) +
			L" (" + XessResultName(value) + L")" + (extra.empty() ? L"." : L"; " + extra));
	}

	template<typename T>
	T Resolve(HMODULE module, const char* name)
	{
		return reinterpret_cast<T>(GetProcAddress(module, name));
	}

	std::wstring VersionText(const xess_version_t& version)
	{
		return std::to_wstring(version.major) + L"." + std::to_wstring(version.minor) +
			L"." + std::to_wstring(version.patch) + L" (reserved=" +
			std::to_wstring(version.reserved) + L")";
	}

	void AddCompletion(std::vector<DiagnosticResult>& results,
		std::chrono::high_resolution_clock::time_point started, bool initialized)
	{
		DiagnosticStatus aggregate = DiagnosticStatus::Pass;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Fail) { aggregate = DiagnosticStatus::Fail; break; }
			if (result.status == DiagnosticStatus::Blocked) aggregate = DiagnosticStatus::Blocked;
			else if (result.status == DiagnosticStatus::Inconclusive && aggregate == DiagnosticStatus::Pass)
				aggregate = DiagnosticStatus::Inconclusive;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
		auto completion = MakeResult(L"XeSS suite completion", aggregate,
			initialized
				? L"O contexto XeSS-SR foi criado e inicializado; a execução de um frame não faz parte desta sonda."
				: L"A cadeia XeSS-SR não chegou à inicialização; examine o primeiro resultado bloqueado.",
			L"ContextInitialized=" + std::wstring(initialized ? L"true" : L"false") +
			L"; FrameExecutionTested=false.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> XessDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();
		bool initialized = false;
		bool contextDestroyed = true;

		SetLastError(ERROR_SUCCESS);
		HMODULE xessModule = LoadPackagedLibrary(L"libxess.dll", 0);
		if (xessModule == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Load packaged libxess.dll", DiagnosticStatus::Blocked,
				L"O runtime XeSS empacotado não pôde ser carregado.",
				L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			ProbeDependencies(results);
			AddCompletion(results, started, initialized);
			return results;
		}
		results.push_back(MakeResult(L"Load packaged libxess.dll", DiagnosticStatus::Pass,
			L"O runtime XeSS foi carregado a partir do pacote do aplicativo."));

		XessApi api;
		api.getVersion = Resolve<XessGetVersionFn>(xessModule, "xessGetVersion");
		api.createContext = Resolve<XessD3D12CreateContextFn>(xessModule, "xessD3D12CreateContext");
		api.isOptimalDriver = Resolve<XessIsOptimalDriverFn>(xessModule, "xessIsOptimalDriver");
		api.getIntelXeFxVersion = Resolve<XessGetIntelXeFXVersionFn>(xessModule, "xessGetIntelXeFXVersion");
		api.getProperties = Resolve<XessGetPropertiesFn>(xessModule, "xessGetProperties");
		api.getOptimalInputResolution = Resolve<XessGetOptimalInputResolutionFn>(xessModule, "xessGetOptimalInputResolution");
		api.initialize = Resolve<XessD3D12InitFn>(xessModule, "xessD3D12Init");
		api.getInitParams = Resolve<XessD3D12GetInitParamsFn>(xessModule, "xessD3D12GetInitParams");
		api.destroyContext = Resolve<XessDestroyContextFn>(xessModule, "xessDestroyContext");
		std::vector<std::wstring> missing;
		if (!api.getVersion) missing.emplace_back(L"xessGetVersion");
		if (!api.createContext) missing.emplace_back(L"xessD3D12CreateContext");
		if (!api.isOptimalDriver) missing.emplace_back(L"xessIsOptimalDriver");
		if (!api.getIntelXeFxVersion) missing.emplace_back(L"xessGetIntelXeFXVersion");
		if (!api.getProperties) missing.emplace_back(L"xessGetProperties");
		if (!api.getOptimalInputResolution) missing.emplace_back(L"xessGetOptimalInputResolution");
		if (!api.initialize) missing.emplace_back(L"xessD3D12Init");
		if (!api.getInitParams) missing.emplace_back(L"xessD3D12GetInitParams");
		if (!api.destroyContext) missing.emplace_back(L"xessDestroyContext");
		if (!missing.empty())
		{
			std::wstring names;
			for (size_t index = 0; index < missing.size(); ++index)
			{
				if (index) names += L", ";
				names += missing[index];
			}
			results.push_back(MakeResult(L"Resolve XeSS exports", DiagnosticStatus::Blocked,
				L"O runtime não expõe todas as funções exigidas pela sonda.", L"Missing=" + names + L"."));
			FreeLibrary(xessModule);
			AddCompletion(results, started, initialized);
			return results;
		}
		results.push_back(MakeResult(L"Resolve XeSS exports", DiagnosticStatus::Pass,
			L"Todas as funções XeSS exigidas foram resolvidas."));

		xess_version_t xessVersion = {};
		xess_result_t versionResult = api.getVersion(&xessVersion);
		results.push_back(XessResult(L"xessGetVersion", versionResult,
			L"A versão do runtime XeSS foi consultada.", L"A consulta de versão XeSS falhou.",
			L"Version=" + VersionText(xessVersion)));

		SetLastError(ERROR_SUCCESS);
		HMODULE d3d12Module = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (d3d12Module == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Load d3d12.dll for XeSS", DiagnosticStatus::Blocked,
				L"O runtime D3D12 não pôde ser carregado para XeSS.",
				L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			FreeLibrary(xessModule);
			AddCompletion(results, started, initialized);
			return results;
		}

		auto createD3d12Device = Resolve<D3D12CreateDeviceFn>(d3d12Module, "D3D12CreateDevice");
		ComPtr<ID3D12Device> device;
		HRESULT deviceHr = createD3d12Device == nullptr ? HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND) :
			createD3d12Device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
		auto deviceResult = MakeResult(L"D3D12 device for XeSS",
			SUCCEEDED(deviceHr) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			SUCCEEDED(deviceHr) ? L"Um device D3D12 foi criado para a sonda XeSS."
				: L"Não foi possível criar o device D3D12 exigido por XeSS.",
			L"HRESULT=" + Hex32(static_cast<uint32_t>(deviceHr)) + L"; MinimumFeatureLevel=11.0.");
		deviceResult.hasHresult = true;
		deviceResult.hresult = deviceHr;
		results.push_back(std::move(deviceResult));
		if (FAILED(deviceHr))
		{
			FreeLibrary(xessModule);
			FreeLibrary(d3d12Module);
			AddCompletion(results, started, initialized);
			return results;
		}

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_4 };
		HRESULT shaderHr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
			&shaderModel, sizeof(shaderModel));
		bool shader64 = SUCCEEDED(shaderHr) && shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_4;
		auto shaderResult = MakeResult(L"Shader Model 6.4 requirement",
			shader64 ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			shader64 ? L"O device expõe Shader Model 6.4, requisito da implementação cross-vendor XeSS."
				: L"O device não expõe o Shader Model 6.4 exigido por XeSS.",
			L"HRESULT=" + Hex32(static_cast<uint32_t>(shaderHr)) +
			L"; HighestShaderModel=" + Hex32(static_cast<uint32_t>(shaderModel.HighestShaderModel)) + L".");
		shaderResult.hasHresult = true;
		shaderResult.hresult = shaderHr;
		results.push_back(std::move(shaderResult));

		xess_context_handle_t context = nullptr;
		xess_result_t createResult = api.createContext(device.Get(), &context);
		results.push_back(XessResult(L"xessD3D12CreateContext", createResult,
			L"Um contexto XeSS-SR foi criado sobre o device D3D12.",
			L"O runtime XeSS recusou a criação do contexto D3D12.",
			L"Context=" + Hex32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context) & 0xFFFFFFFFu))));

		if (createResult == XESS_RESULT_SUCCESS && context != nullptr)
		{
			contextDestroyed = false;
			xess_result_t driverResult = api.isOptimalDriver(context);
			results.push_back(XessResult(L"xessIsOptimalDriver", driverResult,
				L"O driver foi aceito como adequado para XeSS.",
				L"O driver não oferece suporte completo ou ideal a XeSS."));

			xess_version_t xeFxVersion = {};
			xess_result_t xeFxResult = api.getIntelXeFxVersion(context, &xeFxVersion);
			results.push_back(XessResult(L"xessGetIntelXeFXVersion", xeFxResult,
				L"A versão XeFX foi consultada.", L"A consulta da versão XeFX falhou.",
				L"Version=" + VersionText(xeFxVersion)));

			xess_2d_t outputResolution = { 1920, 1080 };
			xess_properties_t properties = {};
			xess_result_t propertiesResult = api.getProperties(context, &outputResolution, &properties);
			results.push_back(XessResult(L"xessGetProperties(1920x1080)", propertiesResult,
				L"Os requisitos internos de recursos XeSS foram consultados.",
				L"XeSS não conseguiu calcular os recursos necessários.",
				L"RequiredDescriptorCount=" + std::to_wstring(properties.requiredDescriptorCount) +
				L"; TempBufferHeapSize=" + std::to_wstring(properties.tempBufferHeapSize) +
				L"; TempTextureHeapSize=" + std::to_wstring(properties.tempTextureHeapSize)));

			xess_2d_t optimal = {}, minimum = {}, maximum = {};
			xess_result_t resolutionResult = api.getOptimalInputResolution(context, &outputResolution,
				XESS_QUALITY_SETTING_PERFORMANCE, &optimal, &minimum, &maximum);
			results.push_back(XessResult(L"xessGetOptimalInputResolution", resolutionResult,
				L"A faixa de resolução de entrada XeSS foi calculada.",
				L"XeSS não conseguiu calcular a resolução de entrada.",
				L"Output=1920x1080; Quality=Performance; Optimal=" + std::to_wstring(optimal.x) + L"x" +
				std::to_wstring(optimal.y) + L"; Minimum=" + std::to_wstring(minimum.x) + L"x" +
				std::to_wstring(minimum.y) + L"; Maximum=" + std::to_wstring(maximum.x) + L"x" +
				std::to_wstring(maximum.y)));

			xess_d3d12_init_params_t initParams = {};
			initParams.outputResolution = outputResolution;
			initParams.qualitySetting = XESS_QUALITY_SETTING_PERFORMANCE;
			initParams.initFlags = XESS_INIT_FLAG_HIGH_RES_MV;
			xess_result_t initResult = api.initialize(context, &initParams);
			initialized = initResult == XESS_RESULT_SUCCESS;
			results.push_back(XessResult(L"xessD3D12Init", initResult,
				L"XeSS alocou recursos e inicializou seus pipelines D3D12.",
				L"O contexto foi criado, mas a inicialização XeSS falhou.",
				L"Output=1920x1080; Quality=Performance; Flags=HIGH_RES_MV"));

			if (initialized)
			{
				xess_d3d12_init_params_t observedParams = {};
				xess_result_t getParamsResult = api.getInitParams(context, &observedParams);
				results.push_back(XessResult(L"xessD3D12GetInitParams", getParamsResult,
					L"Os parâmetros do contexto XeSS inicializado foram recuperados.",
					L"O contexto inicializou, mas seus parâmetros não puderam ser consultados.",
					L"Output=" + std::to_wstring(observedParams.outputResolution.x) + L"x" +
					std::to_wstring(observedParams.outputResolution.y) + L"; Quality=" +
					std::to_wstring(static_cast<uint32_t>(observedParams.qualitySetting)) +
					L"; Flags=" + Hex32(observedParams.initFlags)));
			}

			xess_result_t destroyResult = api.destroyContext(context);
			results.push_back(XessResult(L"xessDestroyContext", destroyResult,
				L"O contexto XeSS foi destruído corretamente.",
				L"A destruição do contexto XeSS falhou."));
			contextDestroyed = destroyResult == XESS_RESULT_SUCCESS;
			if (contextDestroyed) context = nullptr;
		}

		if (contextDestroyed)
		{
			FreeLibrary(xessModule);
			device.Reset();
			FreeLibrary(d3d12Module);
		}
		else
		{
			// Preserve the runtime and device if context teardown failed. Unloading
			// either dependency while XeSS may still own objects is unsafe.
			device.Detach();
		}
		AddCompletion(results, started, initialized);
		return results;
	}
}
}

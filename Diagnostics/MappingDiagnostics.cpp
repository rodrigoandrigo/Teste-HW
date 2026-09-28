#include "pch.h"
#include "MappingDiagnostics.h"

#include <fileapifromapp.h>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct ExecutionProbe
	{
		bool executed;
		DWORD exceptionCode;
		uint32_t returnValue;
	};

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	DiagnosticResult MakeResult(
		const wchar_t* name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"File / Shared Memory Mapping";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult ErrorResult(const wchar_t* name, const std::wstring& summary, DWORD error)
	{
		auto result = MakeResult(name, DiagnosticStatus::Blocked, summary,
			L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
		result.hasWin32Error = true;
		result.win32Error = error;
		return result;
	}

	ExecutionProbe ExecuteMappedCode(void* address)
	{
		ExecutionProbe probe = {};
		__try
		{
#if defined(_M_X64)
			using Function = uint32_t(*)();
			probe.returnValue = reinterpret_cast<Function>(address)();
			probe.executed = true;
#else
			(void)address;
#endif
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> MappingDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();
		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);

		constexpr ULONG64 anonymousSize = 16ull * 1024 * 1024;
		SetLastError(ERROR_SUCCESS);
		HANDLE anonymousMapping = CreateFileMappingFromApp(
			INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, anonymousSize, nullptr);
		if (anonymousMapping == nullptr)
		{
			results.push_back(ErrorResult(L"Anonymous shared mapping",
				L"CreateFileMappingFromApp could not create a pagefile-backed mapping.", GetLastError()));
		}
		else
		{
			void* firstView = MapViewOfFileFromApp(anonymousMapping, FILE_MAP_WRITE, 0, static_cast<SIZE_T>(anonymousSize));
			const DWORD firstError = firstView ? ERROR_SUCCESS : GetLastError();
			void* secondView = MapViewOfFileFromApp(anonymousMapping, FILE_MAP_READ, 0, static_cast<SIZE_T>(anonymousSize));
			const DWORD secondError = secondView ? ERROR_SUCCESS : GetLastError();
			bool coherent = false;
			if (firstView != nullptr && secondView != nullptr)
			{
				auto first = static_cast<volatile uint8_t*>(firstView);
				auto second = static_cast<volatile const uint8_t*>(secondView);
				first[0] = 0x12;
				first[systemInfo.dwPageSize] = 0x34;
				first[anonymousSize - 1] = 0x56;
				MemoryBarrier();
				coherent = second[0] == 0x12 && second[systemInfo.dwPageSize] == 0x34 && second[anonymousSize - 1] == 0x56;
			}
			if (secondView) UnmapViewOfFile(secondView);
			if (firstView) UnmapViewOfFile(firstView);
			CloseHandle(anonymousMapping);

			auto status = firstView && secondView && coherent ? DiagnosticStatus::Pass : DiagnosticStatus::Fail;
			auto result = MakeResult(L"Anonymous shared mapping", status,
				status == DiagnosticStatus::Pass ? L"Two views of a 16 MiB pagefile mapping remained coherent."
				: L"The anonymous mapping or cross-view coherence test failed.",
				L"FirstViewError=" + std::to_wstring(firstError) + L"; SecondViewError=" +
				std::to_wstring(secondError) + L"; Coherent=" + (coherent ? L"true" : L"false") + L".");
			if (firstError != ERROR_SUCCESS || secondError != ERROR_SUCCESS)
			{
				result.hasWin32Error = true;
				result.win32Error = firstError != ERROR_SUCCESS ? firstError : secondError;
			}
			results.push_back(std::move(result));
		}

		std::wstring mappingName = L"Local\\TesteHW_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
		HANDLE namedMapping = CreateFileMappingFromApp(
			INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, systemInfo.dwPageSize, mappingName.c_str());
		if (namedMapping == nullptr)
		{
			results.push_back(ErrorResult(L"Named mapping reopen",
				L"CreateFileMappingFromApp could not create a named mapping.", GetLastError()));
		}
		else
		{
			SetLastError(ERROR_SUCCESS);
			HANDLE reopened = OpenFileMappingFromApp(FILE_MAP_READ, FALSE, mappingName.c_str());
			const DWORD openError = reopened ? ERROR_SUCCESS : GetLastError();
			void* writer = MapViewOfFileFromApp(namedMapping, FILE_MAP_WRITE, 0, systemInfo.dwPageSize);
			const DWORD writerError = writer ? ERROR_SUCCESS : GetLastError();
			void* reader = reopened ? MapViewOfFileFromApp(reopened, FILE_MAP_READ, 0, systemInfo.dwPageSize) : nullptr;
			const DWORD readerError = reader ? ERROR_SUCCESS : GetLastError();
			bool coherent = false;
			if (writer && reader)
			{
				*static_cast<volatile uint32_t*>(writer) = 0x5448574D;
				MemoryBarrier();
				coherent = *static_cast<volatile const uint32_t*>(reader) == 0x5448574D;
			}
			if (reader) UnmapViewOfFile(reader);
			if (writer) UnmapViewOfFile(writer);
			if (reopened) CloseHandle(reopened);
			CloseHandle(namedMapping);
			auto status = reopened && writer && reader && coherent ? DiagnosticStatus::Pass : DiagnosticStatus::Fail;
			auto result = MakeResult(L"Named mapping reopen", status,
				status == DiagnosticStatus::Pass ? L"OpenFileMappingFromApp reopened the named object and shared coherent data."
				: L"The named mapping could not be reopened or was not coherent.",
				L"Name=" + mappingName + L"; OpenError=" + std::to_wstring(openError) +
				L"; WriterError=" + std::to_wstring(writerError) + L"; ReaderError=" + std::to_wstring(readerError) + L".");
			if (openError || writerError || readerError)
			{
				result.hasWin32Error = true;
				result.win32Error = openError ? openError : (writerError ? writerError : readerError);
			}
			results.push_back(std::move(result));
		}

		HANDLE alignmentMapping = CreateFileMappingFromApp(
			INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
			static_cast<ULONG64>(systemInfo.dwAllocationGranularity) * 2, nullptr);
		if (alignmentMapping == nullptr)
		{
			results.push_back(ErrorResult(L"Mapping offset alignment",
				L"The alignment test mapping could not be created.", GetLastError()));
		}
		else
		{
			void* aligned = MapViewOfFileFromApp(alignmentMapping, FILE_MAP_READ,
				systemInfo.dwAllocationGranularity, systemInfo.dwPageSize);
			const DWORD alignedError = aligned ? ERROR_SUCCESS : GetLastError();
			SetLastError(ERROR_SUCCESS);
			void* unaligned = MapViewOfFileFromApp(alignmentMapping, FILE_MAP_READ, 1, systemInfo.dwPageSize);
			const DWORD unalignedError = unaligned ? ERROR_SUCCESS : GetLastError();
			if (unaligned) UnmapViewOfFile(unaligned);
			if (aligned) UnmapViewOfFile(aligned);
			CloseHandle(alignmentMapping);
			const bool passed = aligned != nullptr && unaligned == nullptr && unalignedError != ERROR_SUCCESS;
			auto result = MakeResult(L"Mapping offset alignment", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"Granularity-aligned offset succeeded and offset 1 was rejected."
				: L"Mapping offsets did not follow the reported allocation granularity.",
				L"AllocationGranularity=" + std::to_wstring(systemInfo.dwAllocationGranularity) +
				L"; AlignedError=" + std::to_wstring(alignedError) + L"; UnalignedError=" +
				std::to_wstring(unalignedError) + L" (" + Hex32(unalignedError) + L").");
			if (!passed) { result.hasWin32Error = true; result.win32Error = unalignedError ? unalignedError : alignedError; }
			results.push_back(std::move(result));
		}

		auto temporaryFolder = Windows::Storage::ApplicationData::Current->TemporaryFolder;
		std::wstring filePath = temporaryFolder->Path->Data();
		filePath += L"\\TesteHW_mapping_" + std::to_wstring(GetTickCount64()) + L".bin";
		CREATEFILE2_EXTENDED_PARAMETERS parameters = {};
		parameters.dwSize = sizeof(parameters);
		parameters.dwFileAttributes = FILE_ATTRIBUTE_TEMPORARY;
		HANDLE file = CreateFile2FromAppW(
			filePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS, &parameters);
		if (file == INVALID_HANDLE_VALUE)
		{
			results.push_back(ErrorResult(L"File-backed mapping persistence",
				L"CreateFile2FromAppW could not create the temporary backing file.", GetLastError()));
		}
		else
		{
			constexpr ULONG64 fileSize = 4ull * 1024 * 1024;
			HANDLE fileMapping = CreateFileMappingFromApp(file, nullptr, PAGE_READWRITE, fileSize, nullptr);
			const DWORD mappingError = fileMapping ? ERROR_SUCCESS : GetLastError();
			void* fileView = fileMapping ? MapViewOfFileFromApp(fileMapping, FILE_MAP_WRITE, 0, static_cast<SIZE_T>(fileSize)) : nullptr;
			const DWORD viewError = fileView ? ERROR_SUCCESS : GetLastError();
			bool flushed = false;
			if (fileView)
			{
				auto bytes = static_cast<uint8_t*>(fileView);
				bytes[0] = 0xDE;
				bytes[systemInfo.dwPageSize] = 0xAD;
				bytes[fileSize - 1] = 0xEF;
				flushed = FlushViewOfFile(fileView, static_cast<SIZE_T>(fileSize)) != FALSE && FlushFileBuffers(file) != FALSE;
				UnmapViewOfFile(fileView);
			}
			if (fileMapping) CloseHandle(fileMapping);
			CloseHandle(file);

			bool persisted = false;
			DWORD reopenError = ERROR_SUCCESS;
			HANDLE reopenedFile = CreateFile2FromAppW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
			if (reopenedFile == INVALID_HANDLE_VALUE)
			{
				reopenError = GetLastError();
			}
			else
			{
				HANDLE readMapping = CreateFileMappingFromApp(reopenedFile, nullptr, PAGE_READONLY, 0, nullptr);
				void* readView = readMapping ? MapViewOfFileFromApp(readMapping, FILE_MAP_READ, 0, static_cast<SIZE_T>(fileSize)) : nullptr;
				if (readView)
				{
					auto bytes = static_cast<const uint8_t*>(readView);
					persisted = bytes[0] == 0xDE && bytes[systemInfo.dwPageSize] == 0xAD && bytes[fileSize - 1] == 0xEF;
					UnmapViewOfFile(readView);
				}
				else reopenError = GetLastError();
				if (readMapping) CloseHandle(readMapping);
				CloseHandle(reopenedFile);
			}
			DeleteFileFromAppW(filePath.c_str());

			const bool passed = fileMapping && fileView && flushed && persisted;
			auto result = MakeResult(L"File-backed mapping persistence", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"Mapped writes survived flush, close, reopen and read-only remapping."
				: L"The file-backed mapping did not complete its persistence cycle.",
				L"Size=4194304; MappingError=" + std::to_wstring(mappingError) + L"; ViewError=" +
				std::to_wstring(viewError) + L"; ReopenError=" + std::to_wstring(reopenError) +
				L"; Flushed=" + (flushed ? L"true" : L"false") + L"; Persisted=" + (persisted ? L"true" : L"false") + L".");
			if (mappingError || viewError || reopenError)
			{
				result.hasWin32Error = true;
				result.win32Error = mappingError ? mappingError : (viewError ? viewError : reopenError);
			}
			results.push_back(std::move(result));
		}

#if defined(_M_X64)
		HANDLE executableMapping = CreateFileMappingFromApp(
			INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, systemInfo.dwPageSize, nullptr);
		if (executableMapping == nullptr)
		{
			results.push_back(ErrorResult(L"Executable mapping",
				L"CreateFileMappingFromApp rejected executable protection.", GetLastError()));
		}
		else
		{
			void* writableView = MapViewOfFileFromApp(executableMapping, FILE_MAP_WRITE, 0, systemInfo.dwPageSize);
			const DWORD writableError = writableView ? ERROR_SUCCESS : GetLastError();
			if (writableView)
			{
				const uint8_t code[] = { 0xB8, 0x44, 0x33, 0x22, 0x11, 0xC3 };
				std::memcpy(writableView, code, sizeof(code));
				FlushInstructionCache(GetCurrentProcess(), writableView, sizeof(code));
				UnmapViewOfFile(writableView);
			}
			void* executableView = MapViewOfFileFromApp(executableMapping, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, systemInfo.dwPageSize);
			const DWORD executableError = executableView ? ERROR_SUCCESS : GetLastError();
			ExecutionProbe probe = {};
			if (executableView) probe = ExecuteMappedCode(executableView);
			if (executableView) UnmapViewOfFile(executableView);
			CloseHandle(executableMapping);
			const bool passed = writableView && executableView && probe.executed && probe.returnValue == 0x11223344;
			auto result = MakeResult(L"Executable mapping", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				passed ? L"Generated code executed from an executable file-mapping view."
				: L"Creation, mapping or execution of an executable view was blocked.",
				L"WritableViewError=" + std::to_wstring(writableError) + L"; ExecutableViewError=" +
				std::to_wstring(executableError) + L"; ExceptionCode=" + Hex32(probe.exceptionCode) +
				L"; ReturnValue=" + Hex32(probe.returnValue) + L".");
			if (writableError || executableError)
			{
				result.hasWin32Error = true;
				result.win32Error = writableError ? writableError : executableError;
			}
			results.push_back(std::move(result));
		}
#endif

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(L"Mapping suite completion", DiagnosticStatus::Pass,
			L"Anonymous, named, file-backed and executable mapping probes completed.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}

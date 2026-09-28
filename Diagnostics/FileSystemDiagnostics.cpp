#include "pch.h"
#include "FileSystemDiagnostics.h"

#include <fileapifromapp.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	constexpr uint64_t TestFileSize = 64ull * 1024 * 1024;
	constexpr DWORD SequentialBlockSize = 1024 * 1024;
	constexpr DWORD RandomBlockSize = 4096;
	constexpr uint32_t RandomOperations = 2048;

	struct IoMeasurement
	{
		bool succeeded = false;
		DWORD error = ERROR_SUCCESS;
		uint64_t bytes = 0;
		uint64_t operations = 0;
		double seconds = 0.0;
		bool dataValid = true;
	};

	DiagnosticResult MakeResult(
		const std::wstring& name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Filesystem / I/O";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult ErrorResult(const std::wstring& name, const std::wstring& summary, DWORD error, const std::wstring& details = L"")
	{
		auto result = MakeResult(name, DiagnosticStatus::Blocked, summary,
			details + (details.empty() ? L"" : L"; ") + L"GetLastError=" + std::to_wstring(error) + L".");
		result.hasWin32Error = true;
		result.win32Error = error;
		return result;
	}

	double ElapsedSeconds(const LARGE_INTEGER& start, const LARGE_INTEGER& end, const LARGE_INTEGER& frequency)
	{
		return frequency.QuadPart > 0 ? static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart : 0.0;
	}

	std::wstring MeasurementDetails(const IoMeasurement& measurement)
	{
		const double mib = static_cast<double>(measurement.bytes) / (1024.0 * 1024.0);
		const double throughput = measurement.seconds > 0.0 ? mib / measurement.seconds : 0.0;
		const double iops = measurement.seconds > 0.0 ? measurement.operations / measurement.seconds : 0.0;
		const double averageUs = measurement.operations > 0 ? measurement.seconds * 1000000.0 / measurement.operations : 0.0;
		std::wostringstream output;
		output << L"Bytes=" << measurement.bytes << L"; Operations=" << measurement.operations
			<< L"; Seconds=" << std::fixed << std::setprecision(6) << measurement.seconds
			<< L"; ThroughputMiBPerSecond=" << std::setprecision(2) << throughput
			<< L"; IOPS=" << std::setprecision(2) << iops
			<< L"; AverageMicrosecondsPerOperation=" << std::setprecision(2) << averageUs
			<< L"; DataValid=" << (measurement.dataValid ? L"true" : L"false")
			<< L"; Error=" << measurement.error << L".";
		return output.str();
	}

	bool Seek(HANDLE file, uint64_t offset, DWORD* error)
	{
		LARGE_INTEGER position = {};
		position.QuadPart = static_cast<LONGLONG>(offset);
		if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
		{
			*error = GetLastError();
			return false;
		}
		return true;
	}

	IoMeasurement SequentialWrite(HANDLE file, const LARGE_INTEGER& frequency)
	{
		IoMeasurement measurement;
		std::vector<uint8_t> buffer(SequentialBlockSize);
		for (size_t index = 0; index < buffer.size(); ++index) buffer[index] = static_cast<uint8_t>((index * 131u + 17u) & 0xFFu);

		LARGE_INTEGER start = {}, end = {};
		QueryPerformanceCounter(&start);
		while (measurement.bytes < TestFileSize)
		{
			DWORD written = 0;
			if (!WriteFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr) || written != buffer.size())
			{
				measurement.error = GetLastError();
				QueryPerformanceCounter(&end);
				measurement.seconds = ElapsedSeconds(start, end, frequency);
				return measurement;
			}
			measurement.bytes += written;
			++measurement.operations;
		}
		QueryPerformanceCounter(&end);
		measurement.seconds = ElapsedSeconds(start, end, frequency);
		measurement.succeeded = true;
		return measurement;
	}

	IoMeasurement SequentialRead(HANDLE file, const LARGE_INTEGER& frequency)
	{
		IoMeasurement measurement;
		std::vector<uint8_t> expected(SequentialBlockSize);
		std::vector<uint8_t> buffer(SequentialBlockSize);
		for (size_t index = 0; index < expected.size(); ++index) expected[index] = static_cast<uint8_t>((index * 131u + 17u) & 0xFFu);
		if (!Seek(file, 0, &measurement.error)) return measurement;

		LARGE_INTEGER start = {}, end = {};
		QueryPerformanceCounter(&start);
		while (measurement.bytes < TestFileSize)
		{
			DWORD read = 0;
			if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read != buffer.size())
			{
				measurement.error = GetLastError();
				QueryPerformanceCounter(&end);
				measurement.seconds = ElapsedSeconds(start, end, frequency);
				return measurement;
			}
			if (!std::equal(buffer.begin(), buffer.end(), expected.begin())) measurement.dataValid = false;
			measurement.bytes += read;
			++measurement.operations;
		}
		QueryPerformanceCounter(&end);
		measurement.seconds = ElapsedSeconds(start, end, frequency);
		measurement.succeeded = measurement.dataValid;
		return measurement;
	}

	uint32_t RandomBlockIndex(uint32_t operation)
	{
		constexpr uint32_t blockCount = static_cast<uint32_t>(TestFileSize / RandomBlockSize);
		return (operation * 4051u + 7919u) & (blockCount - 1);
	}

	IoMeasurement RandomWrite(HANDLE file, const LARGE_INTEGER& frequency)
	{
		IoMeasurement measurement;
		std::vector<uint8_t> buffer(RandomBlockSize);
		LARGE_INTEGER start = {}, end = {};
		QueryPerformanceCounter(&start);
		for (uint32_t operation = 0; operation < RandomOperations; ++operation)
		{
			const uint32_t block = RandomBlockIndex(operation);
			std::fill(buffer.begin(), buffer.end(), static_cast<uint8_t>(block & 0xFF));
			std::memcpy(buffer.data(), &block, sizeof(block));
			if (!Seek(file, static_cast<uint64_t>(block) * RandomBlockSize, &measurement.error)) break;
			DWORD written = 0;
			if (!WriteFile(file, buffer.data(), RandomBlockSize, &written, nullptr) || written != RandomBlockSize)
			{
				measurement.error = GetLastError();
				break;
			}
			measurement.bytes += written;
			++measurement.operations;
		}
		QueryPerformanceCounter(&end);
		measurement.seconds = ElapsedSeconds(start, end, frequency);
		measurement.succeeded = measurement.operations == RandomOperations;
		return measurement;
	}

	IoMeasurement RandomRead(HANDLE file, const LARGE_INTEGER& frequency)
	{
		IoMeasurement measurement;
		std::vector<uint8_t> buffer(RandomBlockSize);
		LARGE_INTEGER start = {}, end = {};
		QueryPerformanceCounter(&start);
		for (uint32_t operation = 0; operation < RandomOperations; ++operation)
		{
			const uint32_t block = RandomBlockIndex(operation);
			if (!Seek(file, static_cast<uint64_t>(block) * RandomBlockSize, &measurement.error)) break;
			DWORD read = 0;
			if (!ReadFile(file, buffer.data(), RandomBlockSize, &read, nullptr) || read != RandomBlockSize)
			{
				measurement.error = GetLastError();
				break;
			}
			uint32_t storedBlock = 0;
			std::memcpy(&storedBlock, buffer.data(), sizeof(storedBlock));
			if (storedBlock != block || buffer.back() != static_cast<uint8_t>(block & 0xFF)) measurement.dataValid = false;
			measurement.bytes += read;
			++measurement.operations;
		}
		QueryPerformanceCounter(&end);
		measurement.seconds = ElapsedSeconds(start, end, frequency);
		measurement.succeeded = measurement.operations == RandomOperations && measurement.dataValid;
		return measurement;
	}

	void AddMeasurement(
		std::vector<DiagnosticResult>& results,
		const std::wstring& location,
		const wchar_t* phase,
		const IoMeasurement& measurement)
	{
		const std::wstring name = location + L" / " + phase;
		auto result = MakeResult(name, measurement.succeeded ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			measurement.succeeded ? L"The I/O phase completed and its data validation passed."
			: L"The I/O phase failed or returned invalid data.", MeasurementDetails(measurement));
		if (measurement.error != ERROR_SUCCESS)
		{
			result.hasWin32Error = true;
			result.win32Error = measurement.error;
		}
		results.push_back(std::move(result));
	}

	void RunLocation(
		std::vector<DiagnosticResult>& results,
		const std::wstring& location,
		const std::wstring& folderPath,
		const LARGE_INTEGER& frequency)
	{
		std::wstring path = folderPath + L"\\TesteHW_io_" + std::to_wstring(GetTickCount64()) + L".bin";
		CREATEFILE2_EXTENDED_PARAMETERS parameters = {};
		parameters.dwSize = sizeof(parameters);
		parameters.dwFileAttributes = FILE_ATTRIBUTE_TEMPORARY;
		parameters.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
		HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS, &parameters);
		if (file == INVALID_HANDLE_VALUE)
		{
			results.push_back(ErrorResult(location + L" / CreateFile2FromAppW",
				L"The benchmark file could not be created.", GetLastError(), L"Path=" + path));
			return;
		}

		results.push_back(MakeResult(location + L" / CreateFile2FromAppW", DiagnosticStatus::Pass,
			L"The benchmark file was created through the UWP-compatible Win32 API.", L"Path=" + path + L"; RequestedSize=67108864 bytes."));

		auto sequentialWrite = SequentialWrite(file, frequency);
		AddMeasurement(results, location, L"Sequential write 1 MiB", sequentialWrite);

		LARGE_INTEGER flushStart = {}, flushEnd = {};
		QueryPerformanceCounter(&flushStart);
		const BOOL flushedAfterSequential = FlushFileBuffers(file);
		const DWORD flushError = flushedAfterSequential ? ERROR_SUCCESS : GetLastError();
		QueryPerformanceCounter(&flushEnd);
		auto flushResult = MakeResult(location + L" / FlushFileBuffers", flushedAfterSequential ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			flushedAfterSequential ? L"The sequential writes were flushed successfully." : L"FlushFileBuffers failed.",
			L"Seconds=" + std::to_wstring(ElapsedSeconds(flushStart, flushEnd, frequency)) + L"; Error=" + std::to_wstring(flushError) + L".");
		if (flushError) { flushResult.hasWin32Error = true; flushResult.win32Error = flushError; }
		results.push_back(std::move(flushResult));

		if (sequentialWrite.succeeded)
		{
			auto sequentialRead = SequentialRead(file, frequency);
			AddMeasurement(results, location, L"Sequential read 1 MiB", sequentialRead);
			auto randomWrite = RandomWrite(file, frequency);
			AddMeasurement(results, location, L"Random write 4 KiB", randomWrite);
			if (randomWrite.succeeded)
			{
				FlushFileBuffers(file);
				auto randomRead = RandomRead(file, frequency);
				AddMeasurement(results, location, L"Random read 4 KiB", randomRead);
			}
		}

		LARGE_INTEGER fileSize = {};
		const BOOL sizeRead = GetFileSizeEx(file, &fileSize);
		const DWORD sizeError = sizeRead ? ERROR_SUCCESS : GetLastError();
		CloseHandle(file);
		const BOOL deleted = DeleteFileFromAppW(path.c_str());
		const DWORD deleteError = deleted ? ERROR_SUCCESS : GetLastError();
		const bool cleanupPassed = sizeRead && static_cast<uint64_t>(fileSize.QuadPart) == TestFileSize && deleted;
		auto cleanup = MakeResult(location + L" / Size and cleanup", cleanupPassed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			cleanupPassed ? L"The file size was correct and the temporary benchmark file was deleted."
			: L"File-size validation or cleanup failed.",
			L"ObservedSize=" + std::to_wstring(fileSize.QuadPart) + L"; SizeError=" + std::to_wstring(sizeError) +
			L"; DeleteError=" + std::to_wstring(deleteError) + L".");
		if (sizeError || deleteError) { cleanup.hasWin32Error = true; cleanup.win32Error = sizeError ? sizeError : deleteError; }
		results.push_back(std::move(cleanup));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> FileSystemDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();
		LARGE_INTEGER frequency = {};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
		{
			results.push_back(ErrorResult(L"I/O timer", L"QueryPerformanceFrequency failed.", GetLastError()));
			return results;
		}

		ULARGE_INTEGER freeBytesAvailable = {};
		ULARGE_INTEGER totalBytes = {};
		ULARGE_INTEGER totalFreeBytes = {};
		auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
		std::wstring localPath = localFolder->Path->Data();
		if (GetDiskFreeSpaceExW(localPath.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes))
		{
			results.push_back(MakeResult(L"LocalFolder capacity", DiagnosticStatus::Pass,
				L"Storage capacity was queried successfully.",
				L"CallerAvailableBytes=" + std::to_wstring(freeBytesAvailable.QuadPart) + L"; TotalBytes=" +
				std::to_wstring(totalBytes.QuadPart) + L"; TotalFreeBytes=" + std::to_wstring(totalFreeBytes.QuadPart) + L"."));
		}
		else
		{
			results.push_back(ErrorResult(L"LocalFolder capacity", L"GetDiskFreeSpaceExW failed.", GetLastError()));
		}

		RunLocation(results, L"LocalFolder", localPath, frequency);
		auto temporaryFolder = Windows::Storage::ApplicationData::Current->TemporaryFolder;
		RunLocation(results, L"TemporaryFolder", temporaryFolder->Path->Data(), frequency);

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(L"Filesystem/I/O suite completion", DiagnosticStatus::Pass,
			L"Filesystem and I/O probes completed in both package-owned locations.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}

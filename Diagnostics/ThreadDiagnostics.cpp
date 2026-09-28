#include "pch.h"
#include "ThreadDiagnostics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	constexpr size_t ThreadSafetyCeiling = 512;
	constexpr SIZE_T ThreadStackReserve = 64 * 1024;
	constexpr uint64_t SmpIterationsPerWorker = 64ull * 1024ull * 1024ull;

	struct CapacityContext { HANDLE releaseEvent; };

	DiagnosticResult MakeResult(const wchar_t* name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Threads / SMP";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult Win32Result(const wchar_t* name, const std::wstring& summary, DWORD error)
	{
		auto result = MakeResult(name, DiagnosticStatus::Blocked, summary,
			L"GetLastError=" + std::to_wstring(error) + L".");
		result.hasWin32Error = true;
		result.win32Error = error;
		return result;
	}

	DWORD WINAPI CapacityWorker(void* parameter)
	{
		auto context = static_cast<CapacityContext*>(parameter);
		return WaitForSingleObjectEx(context->releaseEvent, INFINITE, FALSE) == WAIT_OBJECT_0
			? ERROR_SUCCESS : GetLastError();
	}

	std::wstring HexMask(DWORD_PTR mask)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(sizeof(DWORD_PTR) * 2)
			<< std::setfill(L'0') << mask;
		return output.str();
	}

	struct SmpMeasurement
	{
		uint32_t workers = 0;
		double seconds = 0.0;
		double millionIterationsPerSecond = 0.0;
	};

	SmpMeasurement MeasureSmp(uint32_t workerCount)
	{
		std::atomic<uint32_t> ready(0);
		std::atomic<bool> start(false);
		std::atomic<uint64_t> sink(0);
		std::vector<std::thread> workers;
		workers.reserve(workerCount);
		for (uint32_t worker = 0; worker < workerCount; ++worker)
		{
			workers.emplace_back([worker, &ready, &start, &sink]()
			{
				uint64_t value = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(worker) << 32);
				ready.fetch_add(1, std::memory_order_release);
				while (!start.load(std::memory_order_acquire)) YieldProcessor();
				for (uint64_t index = 0; index < SmpIterationsPerWorker; ++index)
				{
					value ^= value << 13;
					value ^= value >> 7;
					value ^= value << 17;
				}
				sink.fetch_xor(value, std::memory_order_relaxed);
			});
		}
		while (ready.load(std::memory_order_acquire) != workerCount) YieldProcessor();
		auto begin = std::chrono::high_resolution_clock::now();
		start.store(true, std::memory_order_release);
		for (auto& worker : workers) worker.join();
		auto elapsed = std::chrono::duration<double>(
			std::chrono::high_resolution_clock::now() - begin).count();
		SmpMeasurement measurement;
		measurement.workers = workerCount;
		measurement.seconds = elapsed;
		measurement.millionIterationsPerSecond = elapsed > 0.0
			? (static_cast<double>(SmpIterationsPerWorker) * workerCount / elapsed / 1000000.0) : 0.0;
		return measurement;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> ThreadDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

		// Keep all workers alive simultaneously so this measures the real handle,
		// kernel-thread and reserved-stack boundary rather than sequential creation.
		HANDLE releaseEvent = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
		if (releaseEvent == nullptr)
		{
			results.push_back(Win32Result(L"Practical simultaneous thread capacity",
				L"Não foi possível criar o evento usado pelo teste de capacidade.", GetLastError()));
		}
		else
		{
			CapacityContext context = { releaseEvent };
			std::vector<HANDLE> threads;
			threads.reserve(ThreadSafetyCeiling);
			DWORD firstFailure = ERROR_SUCCESS;
			for (size_t index = 0; index < ThreadSafetyCeiling; ++index)
			{
				SetLastError(ERROR_SUCCESS);
				HANDLE thread = CreateThread(nullptr, ThreadStackReserve, CapacityWorker, &context,
					STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
				if (thread == nullptr)
				{
					firstFailure = GetLastError();
					break;
				}
				threads.push_back(thread);
			}

			SetEvent(releaseEvent);
			bool cleanupSucceeded = true;
			for (HANDLE thread : threads)
			{
				if (WaitForSingleObjectEx(thread, 10000, FALSE) != WAIT_OBJECT_0)
					cleanupSucceeded = false;
				CloseHandle(thread);
			}
			CloseHandle(releaseEvent);

			const bool reachedCeiling = threads.size() == ThreadSafetyCeiling;
			std::wostringstream details;
			details << L"SimultaneousThreads=" << threads.size()
				<< L"; StackReservePerThread=" << ThreadStackReserve
				<< L"; SafetyCeiling=" << ThreadSafetyCeiling
				<< L"; ReachedSafetyCeiling=" << (reachedCeiling ? L"true" : L"false")
				<< L"; FirstCreationError=" << firstFailure
				<< L"; CleanupSucceeded=" << (cleanupSucceeded ? L"true" : L"false") << L".";
			auto result = MakeResult(L"Practical simultaneous thread capacity",
				cleanupSucceeded ? (reachedCeiling ? DiagnosticStatus::Inconclusive : DiagnosticStatus::Pass) : DiagnosticStatus::Fail,
				reachedCeiling
					? L"O limite de segurança foi atingido; o máximo real é pelo menos 512 threads simultâneas."
					: L"O primeiro limite prático de criação de threads foi medido.", details.str());
			if (firstFailure != ERROR_SUCCESS)
			{
				result.hasWin32Error = true;
				result.win32Error = firstFailure;
			}
			results.push_back(std::move(result));
		}

		DWORD_PTR processMask = 0;
		DWORD_PTR systemMask = 0;
		if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
		{
			results.push_back(Win32Result(L"Per-thread affinity and CPU migration",
				L"GetProcessAffinityMask falhou.", GetLastError()));
		}
		else
		{
			HANDLE currentThread = GetCurrentThread();
			DWORD_PTR originalMask = 0;
			uint32_t attempted = 0;
			uint32_t observed = 0;
			DWORD affinityError = ERROR_SUCCESS;
			std::wostringstream observations;
			for (uint32_t bit = 0; bit < sizeof(DWORD_PTR) * 8; ++bit)
			{
				DWORD_PTR targetMask = static_cast<DWORD_PTR>(1) << bit;
				if ((processMask & targetMask) == 0) continue;
				SetLastError(ERROR_SUCCESS);
				DWORD_PTR previous = SetThreadAffinityMask(currentThread, targetMask);
				if (previous == 0)
				{
					affinityError = GetLastError();
					break;
				}
				if (originalMask == 0) originalMask = previous;
				++attempted;
				DWORD actual = MAXDWORD;
				for (uint32_t retry = 0; retry < 100; ++retry)
				{
					SwitchToThread();
					actual = GetCurrentProcessorNumber();
					if (actual == bit) break;
				}
				if (actual == bit) ++observed;
				if (attempted > 1) observations << L", ";
				observations << bit << L"->" << actual;
			}
			if (originalMask != 0) SetThreadAffinityMask(currentThread, originalMask);
			const bool passed = affinityError == ERROR_SUCCESS && attempted > 0 && observed == attempted;
			auto result = MakeResult(L"Per-thread affinity and CPU migration",
				passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"A thread foi fixada e migrou individualmente para cada CPU permitida."
					: L"Afinidade ou migração não correspondeu à máscara permitida.",
				L"ProcessMask=" + HexMask(processMask) + L"; SystemMask=" + HexMask(systemMask) +
				L"; Attempted=" + std::to_wstring(attempted) + L"; Observed=" + std::to_wstring(observed) +
				L"; CPU transitions=" + observations.str() + L"; Error=" + std::to_wstring(affinityError) + L".");
			if (affinityError != ERROR_SUCCESS) { result.hasWin32Error = true; result.win32Error = affinityError; }
			results.push_back(std::move(result));
		}

		HANDLE currentThread = GetCurrentThread();
		int originalPriority = GetThreadPriority(currentThread);
		DWORD priorityError = originalPriority == THREAD_PRIORITY_ERROR_RETURN ? GetLastError() : ERROR_SUCCESS;
		bool priorityPassed = priorityError == ERROR_SUCCESS;
		std::wostringstream priorityDetails;
		priorityDetails << L"Original=" << originalPriority << L"; Requested/Observed=";
		const int priorities[] = { THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
			THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST };
		for (int priority : priorities)
		{
			SetLastError(ERROR_SUCCESS);
			BOOL set = SetThreadPriority(currentThread, priority);
			int observed = set ? GetThreadPriority(currentThread) : THREAD_PRIORITY_ERROR_RETURN;
			if (!set || observed != priority)
			{
				priorityPassed = false;
				if (priorityError == ERROR_SUCCESS) priorityError = GetLastError();
			}
			priorityDetails << priority << L"/" << observed << L" ";
		}
		if (originalPriority != THREAD_PRIORITY_ERROR_RETURN)
			SetThreadPriority(currentThread, originalPriority);
		priorityDetails << L"; Restored=" << GetThreadPriority(currentThread)
			<< L"; Error=" << priorityError << L"; TimeCriticalNotTested=true.";
		auto priorityResult = MakeResult(L"Thread priority levels",
			priorityPassed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			priorityPassed ? L"Prioridades seguras foram aplicadas, observadas e restauradas."
				: L"Uma ou mais prioridades foram rejeitadas ou não persistiram.", priorityDetails.str());
		if (priorityError != ERROR_SUCCESS) { priorityResult.hasWin32Error = true; priorityResult.win32Error = priorityError; }
		results.push_back(std::move(priorityResult));

		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);
		uint32_t logicalProcessors = (std::max)(1u, static_cast<uint32_t>(systemInfo.dwNumberOfProcessors));
		std::vector<uint32_t> workerCounts = { 1 };
		for (uint32_t count = 2; count < logicalProcessors; count *= 2) workerCounts.push_back(count);
		if (workerCounts.back() != logicalProcessors) workerCounts.push_back(logicalProcessors);
		std::vector<SmpMeasurement> measurements;
		try
		{
			for (uint32_t count : workerCounts) measurements.push_back(MeasureSmp(count));
			double baseline = measurements.front().millionIterationsPerSecond;
			for (const auto& measurement : measurements)
			{
				double speedup = baseline > 0.0 ? measurement.millionIterationsPerSecond / baseline : 0.0;
				double efficiency = measurement.workers > 0 ? speedup / measurement.workers * 100.0 : 0.0;
				std::wostringstream details;
				details << std::fixed << std::setprecision(3)
					<< L"Workers=" << measurement.workers << L"; IterationsPerWorker=" << SmpIterationsPerWorker
					<< L"; Seconds=" << measurement.seconds << L"; ThroughputMIterationsPerSecond="
					<< measurement.millionIterationsPerSecond << L"; SpeedupVs1=" << speedup
					<< L"; ParallelEfficiencyPercent=" << efficiency << L".";
				results.push_back(MakeResult((L"SMP throughput (" + std::to_wstring(measurement.workers) + L" workers)").c_str(),
					DiagnosticStatus::Pass, L"A carga CPU paralela foi concluída.", details.str()));
			}
		}
		catch (const std::exception& exception)
		{
			std::wstring message(exception.what(), exception.what() + strlen(exception.what()));
			results.push_back(MakeResult(L"SMP scalability", DiagnosticStatus::Fail,
				L"A criação ou execução dos workers SMP falhou.", L"Exception=" + message));
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		DiagnosticStatus aggregate = DiagnosticStatus::Pass;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Fail) { aggregate = DiagnosticStatus::Fail; break; }
			if (result.status == DiagnosticStatus::Blocked) aggregate = DiagnosticStatus::Blocked;
			else if (result.status == DiagnosticStatus::Inconclusive && aggregate == DiagnosticStatus::Pass)
				aggregate = DiagnosticStatus::Inconclusive;
		}
		auto completion = MakeResult(L"Thread/SMP suite completion", aggregate,
			aggregate == DiagnosticStatus::Pass ? L"Todos os testes de threads e SMP foram concluídos."
				: L"A suíte terminou com uma limitação ou falha registrada nos resultados anteriores.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}

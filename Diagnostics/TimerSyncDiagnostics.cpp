#include "pch.h"
#include "TimerSyncDiagnostics.h"

#include <intrin.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct ConditionContext
	{
		SRWLOCK lock;
		CONDITION_VARIABLE condition;
		bool ready;
		bool observed;
	};

	struct TimingStats
	{
		uint64_t minimum = (std::numeric_limits<uint64_t>::max)();
		uint64_t maximum = 0;
		uint64_t total = 0;
		uint32_t samples = 0;

		void Add(uint64_t value)
		{
			minimum = (std::min)(minimum, value);
			maximum = (std::max)(maximum, value);
			total += value;
			++samples;
		}

		double Average() const
		{
			return samples == 0 ? 0.0 : static_cast<double>(total) / samples;
		}
	};

	DiagnosticResult MakeResult(
		const wchar_t* name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Timers / Synchronization";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult ErrorResult(const wchar_t* name, const std::wstring& summary, DWORD error)
	{
		auto result = MakeResult(name, DiagnosticStatus::Blocked, summary,
			L"GetLastError=" + std::to_wstring(error) + L".");
		result.hasWin32Error = true;
		result.win32Error = error;
		return result;
	}

	std::wstring StatsText(const TimingStats& stats, const wchar_t* unit)
	{
		std::wostringstream output;
		output << L"Samples=" << stats.samples << L"; Minimum=" << stats.minimum << L" " << unit
			<< L"; Maximum=" << stats.maximum << L" " << unit
			<< L"; Average=" << std::fixed << std::setprecision(2) << stats.Average() << L" " << unit << L".";
		return output.str();
	}

	DWORD WINAPI ConditionWorker(void* parameter)
	{
		auto context = static_cast<ConditionContext*>(parameter);
		AcquireSRWLockExclusive(&context->lock);
		while (!context->ready)
		{
			if (!SleepConditionVariableSRW(&context->condition, &context->lock, 2000, 0))
			{
				ReleaseSRWLockExclusive(&context->lock);
				return GetLastError();
			}
		}
		context->observed = true;
		ReleaseSRWLockExclusive(&context->lock);
		return ERROR_SUCCESS;
	}

#if defined(_M_X64) || defined(_M_IX86)
	uint64_t ReadSerializedTsc()
	{
		_mm_lfence();
		uint64_t value = __rdtsc();
		_mm_lfence();
		return value;
	}

	uint64_t ReadSerializedRdtscp(uint32_t* auxiliary)
	{
		uint64_t value = __rdtscp(auxiliary);
		_mm_lfence();
		return value;
	}
#endif
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> TimerSyncDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

		LARGE_INTEGER frequency = {};
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
		{
			results.push_back(ErrorResult(L"QueryPerformanceCounter", L"QueryPerformanceFrequency failed.", GetLastError()));
		}
		else
		{
			TimingStats deltas;
			bool monotonic = true;
			LARGE_INTEGER previous = {};
			QueryPerformanceCounter(&previous);
			for (uint32_t index = 0; index < 100000; ++index)
			{
				LARGE_INTEGER current = {};
				QueryPerformanceCounter(&current);
				if (current.QuadPart < previous.QuadPart) monotonic = false;
				const uint64_t delta = static_cast<uint64_t>(current.QuadPart - previous.QuadPart);
				if (delta != 0) deltas.Add(delta);
				previous = current;
			}
			std::wostringstream details;
			details << L"Frequency=" << frequency.QuadPart << L" Hz; Monotonic=" << (monotonic ? L"true" : L"false")
				<< L"; NonZeroDelta " << StatsText(deltas, L"ticks")
				<< L" ApproxMinimumNanoseconds=" << std::fixed << std::setprecision(2)
				<< (deltas.samples ? (static_cast<long double>(deltas.minimum) * 1000000000.0L / frequency.QuadPart) : 0.0L) << L".";
			results.push_back(MakeResult(L"QueryPerformanceCounter", monotonic ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				monotonic ? L"QPC remained monotonic across 100,000 reads." : L"QPC moved backwards.", details.str()));
		}

		TimingStats preciseTimeDeltas;
		bool preciseTimeMonotonic = true;
		FILETIME previousTime = {};
		GetSystemTimePreciseAsFileTime(&previousTime);
		ULARGE_INTEGER previousValue = {};
		previousValue.LowPart = previousTime.dwLowDateTime;
		previousValue.HighPart = previousTime.dwHighDateTime;
		for (uint32_t index = 0; index < 10000; ++index)
		{
			FILETIME currentTime = {};
			GetSystemTimePreciseAsFileTime(&currentTime);
			ULARGE_INTEGER currentValue = {};
			currentValue.LowPart = currentTime.dwLowDateTime;
			currentValue.HighPart = currentTime.dwHighDateTime;
			if (currentValue.QuadPart < previousValue.QuadPart) preciseTimeMonotonic = false;
			const uint64_t delta = currentValue.QuadPart - previousValue.QuadPart;
			if (delta != 0) preciseTimeDeltas.Add(delta);
			previousValue = currentValue;
		}
		results.push_back(MakeResult(L"Precise system time", preciseTimeMonotonic ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			preciseTimeMonotonic ? L"GetSystemTimePreciseAsFileTime remained monotonic." : L"Precise system time moved backwards.",
			StatsText(preciseTimeDeltas, L"100ns units")));

		if (frequency.QuadPart > 0)
		{
			for (DWORD sleepDuration : { 0u, 1u })
			{
				TimingStats sleepStats;
				for (uint32_t index = 0; index < 32; ++index)
				{
					LARGE_INTEGER begin = {}, end = {};
					QueryPerformanceCounter(&begin);
					Sleep(sleepDuration);
					QueryPerformanceCounter(&end);
					const uint64_t microseconds = static_cast<uint64_t>(
						(end.QuadPart - begin.QuadPart) * 1000000ll / frequency.QuadPart);
					sleepStats.Add(microseconds);
				}
				std::wstring name = L"Sleep(" + std::to_wstring(sleepDuration) + L") behavior";
				results.push_back(MakeResult(name.c_str(), DiagnosticStatus::Pass,
					L"Scheduler delay samples were collected.", StatsText(sleepStats, L"us")));
			}
		}

#if defined(_M_X64) || defined(_M_IX86)
		int cpuid[4] = {};
		__cpuidex(cpuid, static_cast<int>(0x80000000u), 0);
		const uint32_t maximumExtended = static_cast<uint32_t>(cpuid[0]);
		bool rdtscpAvailable = false;
		bool invariantTsc = false;
		if (maximumExtended >= 0x80000001u)
		{
			__cpuidex(cpuid, static_cast<int>(0x80000001u), 0);
			rdtscpAvailable = (static_cast<uint32_t>(cpuid[3]) & (1u << 27)) != 0;
		}
		if (maximumExtended >= 0x80000007u)
		{
			__cpuidex(cpuid, static_cast<int>(0x80000007u), 0);
			invariantTsc = (static_cast<uint32_t>(cpuid[3]) & (1u << 8)) != 0;
		}

		if (frequency.QuadPart > 0)
		{
			LARGE_INTEGER qpcBegin = {}, qpcEnd = {};
			QueryPerformanceCounter(&qpcBegin);
			const uint64_t tscBegin = ReadSerializedTsc();
			Sleep(100);
			uint32_t auxiliary = 0;
			const uint64_t tscEnd = rdtscpAvailable ? ReadSerializedRdtscp(&auxiliary) : ReadSerializedTsc();
			QueryPerformanceCounter(&qpcEnd);
			const double seconds = static_cast<double>(qpcEnd.QuadPart - qpcBegin.QuadPart) / frequency.QuadPart;
			const double frequencyMhz = seconds > 0.0 ? static_cast<double>(tscEnd - tscBegin) / seconds / 1000000.0 : 0.0;
			std::wostringstream details;
			details << L"ElapsedQpcSeconds=" << std::fixed << std::setprecision(9) << seconds
				<< L"; TscDelta=" << (tscEnd - tscBegin)
				<< L"; EstimatedTscFrequencyMHz=" << std::setprecision(3) << frequencyMhz
				<< L"; RDTSCP=" << (rdtscpAvailable ? L"yes" : L"no")
				<< L"; RDTSCP_AUX=" << auxiliary
				<< L"; InvariantTSC=" << (invariantTsc ? L"yes" : L"no") << L".";
			results.push_back(MakeResult(L"TSC/QPC correlation", DiagnosticStatus::Pass,
				L"Serialized TSC was correlated against QPC over a 100 ms interval.", details.str()));
		}
#endif

		HANDLE manualEvent = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
		HANDLE autoEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		if (!manualEvent || !autoEvent)
		{
			DWORD error = GetLastError();
			if (manualEvent) CloseHandle(manualEvent);
			if (autoEvent) CloseHandle(autoEvent);
			results.push_back(ErrorResult(L"Event semantics", L"CreateEventExW failed.", error));
		}
		else
		{
			SetEvent(manualEvent);
			DWORD manualFirst = WaitForSingleObjectEx(manualEvent, 0, FALSE);
			DWORD manualSecond = WaitForSingleObjectEx(manualEvent, 0, FALSE);
			ResetEvent(manualEvent);
			DWORD manualReset = WaitForSingleObjectEx(manualEvent, 0, FALSE);
			SetEvent(autoEvent);
			DWORD autoFirst = WaitForSingleObjectEx(autoEvent, 0, FALSE);
			DWORD autoSecond = WaitForSingleObjectEx(autoEvent, 0, FALSE);
			CloseHandle(manualEvent);
			CloseHandle(autoEvent);
			const bool passed = manualFirst == WAIT_OBJECT_0 && manualSecond == WAIT_OBJECT_0 && manualReset == WAIT_TIMEOUT &&
				autoFirst == WAIT_OBJECT_0 && autoSecond == WAIT_TIMEOUT;
			std::wostringstream details;
			details << L"ManualFirst=" << manualFirst << L"; ManualSecond=" << manualSecond
				<< L"; ManualAfterReset=" << manualReset << L"; AutoFirst=" << autoFirst << L"; AutoSecond=" << autoSecond << L".";
			results.push_back(MakeResult(L"Event semantics", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"Manual-reset and auto-reset events followed expected semantics." : L"Event behavior differed from Win32 semantics.", details.str()));
		}

		HANDLE semaphore = CreateSemaphoreExW(nullptr, 0, 2, nullptr, 0, SEMAPHORE_ALL_ACCESS);
		if (!semaphore)
		{
			results.push_back(ErrorResult(L"Semaphore semantics", L"CreateSemaphoreExW failed.", GetLastError()));
		}
		else
		{
			LONG previousCount = -1;
			BOOL released = ReleaseSemaphore(semaphore, 2, &previousCount);
			DWORD first = WaitForSingleObjectEx(semaphore, 0, FALSE);
			DWORD second = WaitForSingleObjectEx(semaphore, 0, FALSE);
			DWORD third = WaitForSingleObjectEx(semaphore, 0, FALSE);
			CloseHandle(semaphore);
			const bool passed = released && previousCount == 0 && first == WAIT_OBJECT_0 && second == WAIT_OBJECT_0 && third == WAIT_TIMEOUT;
			results.push_back(MakeResult(L"Semaphore semantics", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"Semaphore count and exhaustion behavior matched expectations." : L"Semaphore behavior differed from expectations.",
				L"PreviousCount=" + std::to_wstring(previousCount) + L"; Waits=" + std::to_wstring(first) + L"," +
				std::to_wstring(second) + L"," + std::to_wstring(third) + L"."));
		}

		HANDLE mutex = CreateMutexExW(nullptr, nullptr, 0, MUTEX_ALL_ACCESS);
		if (!mutex)
		{
			results.push_back(ErrorResult(L"Mutex semantics", L"CreateMutexExW failed.", GetLastError()));
		}
		else
		{
			DWORD acquired = WaitForSingleObjectEx(mutex, 1000, FALSE);
			BOOL released = acquired == WAIT_OBJECT_0 ? ReleaseMutex(mutex) : FALSE;
			CloseHandle(mutex);
			const bool passed = acquired == WAIT_OBJECT_0 && released;
			results.push_back(MakeResult(L"Mutex semantics", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"The mutex was acquired and released successfully." : L"Mutex acquisition or release failed.",
				L"WaitResult=" + std::to_wstring(acquired) + L"; Released=" + (released ? L"true" : L"false") + L"."));
		}

		ConditionContext conditionContext = {};
		InitializeSRWLock(&conditionContext.lock);
		InitializeConditionVariable(&conditionContext.condition);
		HANDLE conditionThread = CreateThread(nullptr, 0, ConditionWorker, &conditionContext, 0, nullptr);
		if (!conditionThread)
		{
			results.push_back(ErrorResult(L"SRW lock / condition variable", L"CreateThread failed.", GetLastError()));
		}
		else
		{
			Sleep(10);
			AcquireSRWLockExclusive(&conditionContext.lock);
			conditionContext.ready = true;
			WakeConditionVariable(&conditionContext.condition);
			ReleaseSRWLockExclusive(&conditionContext.lock);
			DWORD wait = WaitForSingleObjectEx(conditionThread, 2500, FALSE);
			DWORD workerResult = ERROR_GEN_FAILURE;
			if (wait == WAIT_OBJECT_0) GetExitCodeThread(conditionThread, &workerResult);
			else WaitForSingleObjectEx(conditionThread, 3000, FALSE); // Do not let the worker outlive its stack context.
			CloseHandle(conditionThread);
			const bool passed = wait == WAIT_OBJECT_0 && workerResult == ERROR_SUCCESS && conditionContext.observed;
			results.push_back(MakeResult(L"SRW lock / condition variable", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"A worker slept and woke through a condition variable protected by an SRW lock."
				: L"The condition-variable synchronization flow failed or timed out.",
				L"WaitResult=" + std::to_wstring(wait) + L"; WorkerResult=" + std::to_wstring(workerResult) +
				L"; Observed=" + (conditionContext.observed ? L"true" : L"false") + L"."));
		}

		HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		if (!timer)
		{
			results.push_back(ErrorResult(L"Waitable timer", L"CreateWaitableTimerExW failed.", GetLastError()));
		}
		else
		{
			LARGE_INTEGER dueTime = {};
			dueTime.QuadPart = -100000; // 10 ms in relative 100 ns units.
			LARGE_INTEGER begin = {}, end = {};
			QueryPerformanceCounter(&begin);
			BOOL armed = SetWaitableTimerEx(timer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
			DWORD wait = armed ? WaitForSingleObjectEx(timer, 1000, FALSE) : WAIT_FAILED;
			DWORD error = (armed && wait != WAIT_FAILED) ? ERROR_SUCCESS : GetLastError();
			QueryPerformanceCounter(&end);
			CloseHandle(timer);
			const uint64_t elapsedUs = frequency.QuadPart > 0 ? static_cast<uint64_t>((end.QuadPart - begin.QuadPart) * 1000000ll / frequency.QuadPart) : 0;
			const bool passed = armed && wait == WAIT_OBJECT_0;
			auto result = MakeResult(L"Waitable timer", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				passed ? L"A relative 10 ms waitable timer was armed and signaled." : L"The waitable timer failed or timed out.",
				L"ElapsedMicroseconds=" + std::to_wstring(elapsedUs) + L"; WaitResult=" + std::to_wstring(wait) +
				L"; Error=" + std::to_wstring(error) + L".");
			if (error) { result.hasWin32Error = true; result.win32Error = error; }
			results.push_back(std::move(result));
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(L"Timer/synchronization suite completion", DiagnosticStatus::Pass,
			L"Timer and synchronization probes completed.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}

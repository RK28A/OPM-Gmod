#pragma once

// Debug.h -- crash reporting and file logging for the module.
//
// Purpose is diagnostics: when the injected DLL faults, write a readable crash
// report (exception, faulting address resolved to module+offset, a register
// dump, a heuristic stack backtrace and the loaded-module map) plus a general
// timestamped log, so a crash can be understood after the fact instead of just
// taking the game down silently.
//
// It is deliberately *additive*: the unhandled-exception filter LOGS and then
// returns EXCEPTION_CONTINUE_SEARCH, so the OS still handles the crash exactly
// as it would have. It does not swallow or hide faults.

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace Debug
{
	inline constexpr const char* kLogDir   = "C:\\GMOD-SDK-Settings";
	inline constexpr const char* kLogPath  = "C:\\GMOD-SDK-Settings\\debug.log";
	inline constexpr const char* kCrashPath = "C:\\GMOD-SDK-Settings\\crash.log";

	inline std::mutex& LogMutex()
	{
		static std::mutex m;
		return m;
	}

	inline std::string Timestamp()
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		return std::string(buf);
	}

	// Appends one line to a log file. Opens/closes each call so nothing is lost
	// if the process dies right after.
	inline void AppendLine(const char* path, const char* text)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path, "a") == 0 && f)
		{
			std::fputs(text, f);
			std::fputc('\n', f);
			std::fclose(f);
		}
	}

	// General logging entry point. Thread-safe. Use the DBG_* macros below.
	inline void Log(const char* level, const char* fmt, ...)
	{
		char msg[1024];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(msg, sizeof(msg), fmt ? fmt : "", args);
		va_end(args);

		char line[1200];
		std::snprintf(line, sizeof(line), "[%s] [%-5s] %s", Timestamp().c_str(), level ? level : "?", msg);

		std::lock_guard<std::mutex> lock(LogMutex());
		AppendLine(kLogPath, line);
#ifdef _DEBUG
		std::printf("%s\n", line);
#endif
	}

	inline const char* ExceptionName(DWORD code)
	{
		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
		case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
		case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
		default:                              return "UNKNOWN";
		}
	}

	// Resolves an address to "module+0xOFFSET", or "0x...." if it is not inside
	// a loaded module. POD-only so it is safe to call from the SEH frame below.
	inline void DescribeAddress(void* addr, char* out, size_t outSize)
	{
		HMODULE mod = nullptr;
		if (GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(addr), &mod) && mod)
		{
			char path[MAX_PATH];
			const char* name = "?";
			if (GetModuleFileNameA(mod, path, MAX_PATH))
			{
				const char* slash = std::strrchr(path, '\\');
				name = slash ? slash + 1 : path;
			}
			std::snprintf(out, outSize, "%s+0x%llX", name,
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod)));
		}
		else
		{
			std::snprintf(out, outSize, "0x%p", addr);
		}
	}

	// Heuristic backtrace: scan the crashing stack and report every value that
	// points into an executable page (i.e. a plausible return address), as
	// module+offset. No symbols or dbghelp needed, and robust because it never
	// unwinds -- it just reads stack words guarded by SEH.
	//
	// No object with a destructor may live in this function: it uses __try, and
	// MSVC forbids mixing SEH with C++ unwinding in one frame (C2712).
	inline void DumpStackTrace(void* stackPtr, FILE* f)
	{
		uintptr_t* sp = static_cast<uintptr_t*>(stackPtr);
		int printed = 0;

		for (int i = 0; i < 1024 && printed < 32; ++i)
		{
			uintptr_t value = 0;
			__try
			{
				value = sp[i];
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				break; // ran off the readable stack
			}

			if (value == 0)
				continue;

			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery(reinterpret_cast<void*>(value), &mbi, sizeof(mbi)) == sizeof(mbi)
				&& mbi.State == MEM_COMMIT
				&& (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
			{
				char desc[MAX_PATH + 32];
				DescribeAddress(reinterpret_cast<void*>(value), desc, sizeof(desc));
				std::fprintf(f, "    [sp+0x%03X] %s\n",
					static_cast<unsigned>(i * sizeof(uintptr_t)), desc);
				++printed;
			}
		}

		if (printed == 0)
			std::fprintf(f, "    (no resolvable frames)\n");
	}

	inline void DumpModules(FILE* f)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
		if (snap == INVALID_HANDLE_VALUE)
			return;

		MODULEENTRY32W me;
		me.dwSize = sizeof(me);
		if (Module32FirstW(snap, &me))
		{
			do
			{
				std::fprintf(f, "    %p - %p  %ws\n",
					me.modBaseAddr, me.modBaseAddr + me.modBaseSize, me.szModule);
			} while (Module32NextW(snap, &me));
		}
		CloseHandle(snap);
	}

	// Unhandled-exception filter: write the report, then let default handling
	// proceed. Re-entrancy guarded so a fault inside the handler cannot loop.
	inline LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info)
	{
		static volatile LONG s_inHandler = 0;
		if (InterlockedExchange(&s_inHandler, 1) != 0)
			return EXCEPTION_CONTINUE_SEARCH;

		if (info && info->ExceptionRecord)
		{
			const EXCEPTION_RECORD* er = info->ExceptionRecord;
			const CONTEXT* ctx = info->ContextRecord;

			char addrDesc[MAX_PATH + 32];
			DescribeAddress(er->ExceptionAddress, addrDesc, sizeof(addrDesc));

			CreateDirectoryA(kLogDir, nullptr);

			// crash.log is written directly here, never through Log(): the
			// faulting thread may already hold the log mutex, and taking it
			// again would deadlock the crash path.
			FILE* f = nullptr;
			if (fopen_s(&f, kCrashPath, "a") == 0 && f)
			{
				std::fprintf(f, "==================== CRASH %s ====================\n", Timestamp().c_str());
				std::fprintf(f, "Exception : 0x%08lX (%s)\n", er->ExceptionCode, ExceptionName(er->ExceptionCode));
				std::fprintf(f, "Address   : %p  (%s)\n", er->ExceptionAddress, addrDesc);

				if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
				{
					const ULONG_PTR kind = er->ExceptionInformation[0];
					const char* op = (kind == 0) ? "reading" : (kind == 1) ? "writing" : "executing";
					std::fprintf(f, "Detail    : %s address 0x%p\n", op,
						reinterpret_cast<void*>(er->ExceptionInformation[1]));
				}

				if (ctx)
				{
#ifdef _WIN64
					std::fprintf(f, "RIP=%p RSP=%p RBP=%p EFL=%08lX\n",
						reinterpret_cast<void*>(ctx->Rip), reinterpret_cast<void*>(ctx->Rsp),
						reinterpret_cast<void*>(ctx->Rbp), ctx->EFlags);
					std::fprintf(f, "RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\n",
						ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx);
					std::fprintf(f, "RSI=%016llX RDI=%016llX R8 =%016llX R9 =%016llX\n",
						ctx->Rsi, ctx->Rdi, ctx->R8, ctx->R9);
					std::fprintf(f, "R10=%016llX R11=%016llX R12=%016llX R13=%016llX\n",
						ctx->R10, ctx->R11, ctx->R12, ctx->R13);
					std::fprintf(f, "R14=%016llX R15=%016llX\n", ctx->R14, ctx->R15);
					std::fprintf(f, "Backtrace (heuristic):\n");
					DumpStackTrace(reinterpret_cast<void*>(ctx->Rsp), f);
#else
					std::fprintf(f, "EIP=%p ESP=%p EBP=%p EFL=%08lX\n",
						reinterpret_cast<void*>(ctx->Eip), reinterpret_cast<void*>(ctx->Esp),
						reinterpret_cast<void*>(ctx->Ebp), ctx->EFlags);
					std::fprintf(f, "EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX\n",
						ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
					std::fprintf(f, "ESI=%08lX EDI=%08lX\n", ctx->Esi, ctx->Edi);
					std::fprintf(f, "Backtrace (heuristic):\n");
					DumpStackTrace(reinterpret_cast<void*>(ctx->Esp), f);
#endif
				}

				std::fprintf(f, "Loaded modules:\n");
				DumpModules(f);
				std::fprintf(f, "==================== END CRASH ====================\n\n");
				std::fclose(f);
			}

			// One-line breadcrumb in the general log too (direct write, no mutex).
			char breadcrumb[512];
			std::snprintf(breadcrumb, sizeof(breadcrumb),
				"[%s] [CRASH] 0x%08lX at %s -- see crash.log",
				Timestamp().c_str(), er->ExceptionCode, addrDesc);
			AppendLine(kLogPath, breadcrumb);
		}

		InterlockedExchange(&s_inHandler, 0);
		return EXCEPTION_CONTINUE_SEARCH; // additive: let the OS handle it normally
	}

	// Install once, early in Main().
	inline void Install()
	{
		CreateDirectoryA(kLogDir, nullptr);
		SetUnhandledExceptionFilter(CrashHandler);
		Log("INFO", "Debug logging initialised (built %s %s, %s)",
			__DATE__, __TIME__, sizeof(void*) == 8 ? "x64" : "x86");
	}
} // namespace Debug

#define DBG_INFO(...)  ::Debug::Log("INFO",  __VA_ARGS__)
#define DBG_WARN(...)  ::Debug::Log("WARN",  __VA_ARGS__)
#define DBG_ERROR(...) ::Debug::Log("ERROR", __VA_ARGS__)

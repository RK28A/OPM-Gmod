#pragma once

#include "../globals.hpp"
#include "../core/PathSanitize.h"

#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace ScriptDumper
{
	// Everything below is named by the *server*: the paths it passes to
	// RunStringEx become directory and file names on the local disk.

	// "Idk why, but thinking that the server can just spam runstrings to you
	// will just eventually overload your disk / lag you sucks." -- upstream,
	// which identified the problem and then did not implement a limit.  These
	// are that limit.  They reset per injection, not per server.
	inline constexpr std::uintmax_t kMaxTotalBytes = 64ull * 1024ull * 1024ull; // 64 MB
	inline constexpr int kMaxFiles = 4096;

	inline std::atomic<std::uintmax_t> bytesWritten{ 0 };
	inline std::atomic<int> filesWritten{ 0 };
	inline std::atomic<bool> quotaReported{ false };

	[[nodiscard]] inline bool ReserveQuota(std::size_t size)
	{
		if (filesWritten.load() >= kMaxFiles
			|| bytesWritten.load() + size > kMaxTotalBytes)
		{
			// Said once, not once per script.
			if (!quotaReported.exchange(true))
				ConPrint("Script dumper quota reached; further scripts are not written to disk", Color(255, 200, 0));

			return false;
		}

		bytesWritten += size;
		++filesWritten;
		return true;
	}

	// Base directory for the dumps.
	//
	// Was hard-coded to "C:\GaztoofScriptHook\", which writes to the root of the
	// system drive -- it fails outright on a machine where that is not writable,
	// and it carries the upstream author's name into every fork.  %LOCALAPPDATA%
	// is where per-user application data belongs.
	[[nodiscard]] inline fs::path BaseDirectory()
	{
		char buffer[MAX_PATH] = {};
		const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", buffer, sizeof(buffer));

		if (length > 0 && length < sizeof(buffer))
			return fs::path(buffer) / "GMod-SDK" / "ScriptHook";

		// No LOCALAPPDATA (a service account, a stripped environment): fall back
		// to the current directory rather than to a path that may not exist.
		return fs::path("GMod-SDK-ScriptHook");
	}

	// Identifies the server the scripts came from, as one safe path component.
	[[nodiscard]] inline std::string ServerFolderName()
	{
		std::string label;

		if (EngineClient && EngineClient->GetNetChannelInfo() && EngineClient->GetNetChannelInfo()->GetAddress())
		{
			if (Globals::hostName && *Globals::hostName)
			{
				label = Globals::hostName;
				label += " - ";
			}

			label += EngineClient->GetNetChannelInfo()->GetAddress();
		}

		if (label.empty())
			label = "No Server";

		return pathsafe::SanitizeComponent(label);
	}

	// Splits a server-supplied path into sanitised components.
	//
	// The old SanitizePath() replaced a handful of characters and stopped there:
	// reserved device names (CON, NUL, COM1...), trailing dots and spaces that
	// Windows silently strips, '*', and unbounded length all went through.  Its
	// own comment on the character filter read "hm that's bad".
	[[nodiscard]] inline fs::path SanitizeRelativePath(const std::string& raw)
	{
		fs::path out;
		std::string component;

		for (const char c : raw)
		{
			if (c == '/' || c == '\\')
			{
				if (!component.empty())
					out /= pathsafe::SanitizeComponent(component);

				component.clear();
				continue;
			}

			component += c;
		}

		if (!component.empty())
			out /= pathsafe::SanitizeComponent(component);

		if (out.empty())
			out = pathsafe::SanitizeComponent(std::string());

		return out;
	}
}

// Dumps a script the server asked the client to run, and returns a replacement
// body when the user has staged one in the Detour tree.
std::optional<std::string> SaveScript(std::string fileName, std::string fileContent)
{
	try
	{
		if (fileName == "RunString(Ex)" || fileName.find('.') == std::string::npos)
			fileName = "runString.lua";

		const bool isAnonymous = (fileName == "runString.lua");

		const fs::path base = ScriptDumper::BaseDirectory();
		const std::string server = ScriptDumper::ServerFolderName();
		const fs::path relative = ScriptDumper::SanitizeRelativePath(fileName);

		const fs::path originalPath = base / "Original" / server / relative;
		const fs::path detourPath = base / "Detour" / server / relative;

		// create_directories() does what the hand-rolled CreateRecurringDir()
		// was reimplementing, and reports failure.
		std::error_code ec;
		fs::create_directories(originalPath.parent_path(), ec);
		fs::create_directories(detourPath.parent_path(), ec);

		// A staged replacement wins, and costs no disk.
		std::ifstream detourFile(detourPath, std::ios::binary);
		if (detourFile.is_open() && !isAnonymous)
		{
			std::string replacement((std::istreambuf_iterator<char>(detourFile)),
				std::istreambuf_iterator<char>());

			// std::string comparison, not strcmp: a script containing a NUL byte
			// used to compare equal to any script sharing its prefix.
			if (replacement != fileContent)
			{
				ConPrint(("Successfully detoured script \"" + fileName + "\" !").c_str(), Color(255, 51, 113));
				return replacement;
			}

			return {};
		}

		if (!ScriptDumper::ReserveQuota(fileContent.size()))
			return {};

		std::ofstream outFile(originalPath, std::ios::binary | std::ios::trunc);
		if (outFile.is_open())
		{
			outFile << fileContent;

			if (!isAnonymous)
				ConPrint(("Successfully dumped script \"" + fileName + "\" !").c_str(), Color(204, 51, 255));
		}
	}
	catch (const std::exception& e)
	{
		// `catch (...) {}` swallowed everything in silence, so a dumper that
		// never wrote a byte looked exactly like one that was working.
		ConPrint((std::string("Script dumper error: ") + e.what()).c_str(), Color(255, 100, 100));
	}
	catch (...)
	{
		ConPrint("Script dumper error", Color(255, 100, 100));
	}

	return {};
}

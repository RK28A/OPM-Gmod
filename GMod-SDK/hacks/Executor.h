#pragma once

#include "../globals.hpp"
#include "../hooks/RunStringEx.h"
#include "../core/StringUtil.h"

#include <commdlg.h>
#include <fstream>
#include <mutex>
#include <string>

// The Lua executor crosses three threads: the render thread owns the ImGui text
// buffer, a detached thread runs the modal file dialog, and the Lua thread
// (PaintTraverse) is the only one allowed to run a script.  What used to
// connect them was
//
//     std::atomic<std::pair<bool, LPCSTR>> waitingToBeExecuted;
//
// which is atomic in the *pointer* only.  The pointer it carried was
// Settings::ScriptInput -- the 128 KB ImGui buffer the render thread keeps
// editing while PaintTraverse reads it -- so the script text itself was a plain
// data race.  The std::unique_lock in ExecuteScript() guarded nothing: it was
// taken, an atomic store followed, and it was released, with no other code ever
// locking that mutex.
//
// Both hand-offs are by value under a real lock now.
namespace Executor
{
	// --- script queued for the Lua thread -----------------------------------
	inline std::mutex scriptMutex;
	inline std::string pendingScript;
	inline bool hasPendingScript = false;

	// --- file contents staged by the dialog thread for the render thread -----
	inline std::mutex loadedMutex;
	inline std::string loadedFile;
	inline bool hasLoadedFile = false;

	inline void Submit(std::string script)
	{
		const std::lock_guard<std::mutex> lock(scriptMutex);
		pendingScript = std::move(script);
		hasPendingScript = true;
	}

	// Called from PaintTraverse.  Returns true once per submitted script.
	[[nodiscard]] inline bool TakePending(std::string& out)
	{
		const std::lock_guard<std::mutex> lock(scriptMutex);
		if (!hasPendingScript)
			return false;

		out = std::move(pendingScript);
		pendingScript.clear();
		hasPendingScript = false;
		return true;
	}

	// Runs on its own detached thread: GetOpenFileNameA is modal and would
	// freeze the game if it ran on the render thread.  It therefore must not
	// touch Settings::ScriptInput, which the render thread owns -- it stages the
	// text here and PumpLoadedFile() installs it.
	inline void LoadScriptFromFile()
	{
		OPENFILENAMEA openFileName = {};
		char fileName[MAX_PATH] = "";

		openFileName.lStructSize = sizeof(OPENFILENAMEA);
		openFileName.lpstrDefExt = "lua";
		openFileName.lpstrFile = fileName;
		openFileName.lpstrFilter = "LUA Files (*.lua)\0*.lua\0\0";
		openFileName.hwndOwner = nullptr;
		openFileName.nMaxFile = sizeof(fileName);
		openFileName.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

		if (!GetOpenFileNameA(&openFileName))
			return;

		if (fileName[0] == '\0')
			return;

		std::ifstream stream(fileName, std::ios::binary);

		// `stream.bad()` is not "failed to open" -- it is set for a read error on
		// an already-open stream, so a missing or unreadable file used to sail
		// past this check and produce an empty script.
		if (!stream.is_open())
			return;

		std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

		const std::lock_guard<std::mutex> lock(loadedMutex);
		loadedFile = std::move(contents);
		hasLoadedFile = true;
	}

	// Called from the render thread, which owns Settings::ScriptInput.
	inline void PumpLoadedFile()
	{
		std::string contents;
		{
			const std::lock_guard<std::mutex> lock(loadedMutex);
			if (!hasLoadedFile)
				return;

			contents = std::move(loadedFile);
			loadedFile.clear();
			hasLoadedFile = false;
		}

		// This used to be a bare strcpy into a fixed 128 KB buffer with no size
		// check at all: a larger .lua file overflowed it.
		if (!strutil::CopyBounded(Settings::ScriptInput, sizeof(Settings::ScriptInput), contents))
			ConPrint("Script was longer than the editor buffer and has been truncated", Color(255, 200, 0));
	}
}

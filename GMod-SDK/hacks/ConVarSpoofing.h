#pragma once


#include <Windows.h>
#include <cstdlib> // malloc / free
#include <cstring> // memcpy
#include <memory>
#include <string>

#include "../client/ConVar.h"
#include "../Memory.h"
#include "../core/StringUtil.h"
// i prefer doing that than including globals.hpp ...
// (and it is now load-bearing: globals.hpp includes *this* header, so the
// dependency cannot go the other way.)
extern CCvar* CVar;

// Hides a ConVar from the server by renaming the engine's own instance and
// registering a decoy under the real name.
//
// Inspired of https://gist.github.com/markhc/c975ce63c8714709e410f1be43e4048f
//
// Four things were wrong with the previous implementation:
//
//   * **It overflowed engine memory.**  It wrote "XD_" + the original name back
//     into ConVar::pszName, three bytes longer than what was there.  pszName
//     points into the module's string pool -- which is why the code has to
//     VirtualProtect it in the first place -- so those three bytes landed on
//     whatever literal sat next to it.  The rename is same-length now: one
//     byte is swapped, which is all the mechanism needs.
//   * **It leaked, then double-freed.**  The two instances were raw `new`, and
//     the Unload button called `->~SpoofedConVar()` explicitly rather than
//     `delete`: the object leaked, the pointer was left dangling, and a second
//     Unload ran the destructor again -- double free() of the decoy, double
//     UnregisterConCommand.  They are unique_ptrs owned by this namespace now.
//   * **It restored from uninitialised buffers.**  m_szOriginalName and
//     m_szOriginalValue were unbounded strcpy targets that Spoof() could leave
//     untouched on its early `if (!m_pDummyCVar) return;` path, after which the
//     destructor copied stack garbage into the engine's ConVar.
//   * **It restored the value with strcpy.**  pszValueStr is reallocated by the
//     engine whenever SetValue runs, so copying a saved string back over it is
//     a second overflow waiting to happen.  The value is restored through the
//     engine's own setter instead, and the disabled callback and modified flags
//     -- neither of which was ever put back -- are restored too.
class SpoofedConVar
{
public:
	explicit SpoofedConVar(ConVar* pCVar)
	{
		m_pOriginalCVar = pCVar;
		Spoof();
	}

	~SpoofedConVar() { Restore(); }

	SpoofedConVar(const SpoofedConVar&) = delete;
	SpoofedConVar& operator=(const SpoofedConVar&) = delete;
	SpoofedConVar(SpoofedConVar&&) = delete;
	SpoofedConVar& operator=(SpoofedConVar&&) = delete;

	[[nodiscard]] bool IsSpoofed() const { return m_bSpoofed; }

	ConVar* m_pOriginalCVar = nullptr;

private:
	// Swapped into pszName[0].  Printable so the console does not choke on it,
	// and not a character a ConVar name ever starts with.
	static constexpr char kManglePrefix = '~';

	void Spoof()
	{
		if (!m_pOriginalCVar || !CVar || !m_pOriginalCVar->pszName)
			return;

		// Everything needed to undo this, captured before anything is touched.
		if (!strutil::CopyBounded(m_szOriginalName, sizeof(m_szOriginalName), m_pOriginalCVar->pszName))
			return; // name longer than the buffer: refuse rather than truncate the engine's

		m_iOriginalFlags = m_pOriginalCVar->nflags;
		m_iOriginalIntValue = m_pOriginalCVar->intValue;
		m_cOriginalFirstChar = m_pOriginalCVar->pszName[0];

		if (m_pOriginalCVar->CALLBACKPTR)
			m_pOriginalCallback = *(PVOID*)(m_pOriginalCVar->CALLBACKPTR);

		// The decoy is a byte copy of the engine's ConVar, vtable pointer
		// included -- which is what makes the Create() call below dispatch.
		m_pDummyCVar = (ConVar*)malloc(sizeof(ConVar));
		if (!m_pDummyCVar)
			return;

		memcpy(m_pDummyCVar, m_pOriginalCVar, sizeof(ConVar));

		// Rename the real one out of the way, in place: same length, one byte.
		DWORD dwOld = 0;
		if (!VirtualProtect((LPVOID)m_pOriginalCVar->pszName, sizeof(char), PAGE_READWRITE, &dwOld))
		{
			free(m_pDummyCVar);
			m_pDummyCVar = nullptr;
			return;
		}

		m_pOriginalCVar->pszName[0] = kManglePrefix;
		VirtualProtect((LPVOID)m_pOriginalCVar->pszName, sizeof(char), dwOld, &dwOld);

		// The decoy takes the name the server will look for.
		m_pDummyCVar->Create(m_szOriginalName, m_pOriginalCVar->pszDefaultValue,
			static_cast<int>(m_pOriginalCVar->nflags), m_pOriginalCVar->pszHelpString,
			m_pOriginalCVar->bHasMin != 0, m_pOriginalCVar->fMinVal,
			m_pOriginalCVar->bHasMax != 0, m_pOriginalCVar->fMaxVal, nullptr);

		CVar->RegisterConCommand(m_pDummyCVar);

		// Paired with the save above, so the callback can actually be put back.
		// The caller used to do this itself, one frame after construction, with
		// nothing tying the two together.
		m_pOriginalCVar->DisableCallback();

		m_bSpoofed = true;
	}

	void Restore()
	{
		if (!m_bSpoofed)
		{
			// Spoof() bailed out.  Free the decoy if it got as far as
			// allocating one, and touch nothing in the engine.
			if (m_pDummyCVar)
			{
				free(m_pDummyCVar);
				m_pDummyCVar = nullptr;
			}
			return;
		}

		m_bSpoofed = false; // idempotent: a second Restore() is a no-op

		if (CVar && m_pDummyCVar)
			CVar->UnregisterConCommand(m_pDummyCVar);

		if (m_pDummyCVar)
		{
			free(m_pDummyCVar);
			m_pDummyCVar = nullptr;
		}

		if (!m_pOriginalCVar || !m_pOriginalCVar->pszName)
			return;

		// Put the name back -- one byte, so no allocation and no overflow.
		DWORD dwOld = 0;
		if (VirtualProtect((LPVOID)m_pOriginalCVar->pszName, sizeof(char), PAGE_READWRITE, &dwOld))
		{
			m_pOriginalCVar->pszName[0] = m_cOriginalFirstChar;
			VirtualProtect((LPVOID)m_pOriginalCVar->pszName, sizeof(char), dwOld, &dwOld);
		}

		// Value goes back through the engine's setter, which owns pszValueStr's
		// allocation.  Done while the callback is still disabled so restoring
		// the value does not fire it.
		m_pOriginalCVar->SetValue(m_iOriginalIntValue);

		m_pOriginalCVar->nflags = m_iOriginalFlags;

		// DisableCallback() overwrote the callback without saving it, so it was
		// never put back; every unload left the cvar's change callback stubbed
		// out for the rest of the session.
		if (m_pOriginalCVar->CALLBACKPTR && m_pOriginalCallback)
			*(PVOID*)(m_pOriginalCVar->CALLBACKPTR) = m_pOriginalCallback;
	}

	ConVar* m_pDummyCVar = nullptr;

	char m_szOriginalName[128] = {};
	char m_cOriginalFirstChar = '\0';
	uint32_t m_iOriginalFlags = 0;
	int32_t m_iOriginalIntValue = 0;
	PVOID m_pOriginalCallback = nullptr;
	bool m_bSpoofed = false;
};

// Ownership for the two cvars this module spoofs.
//
// These replace two raw globals that nothing owned.  Acquire() is idempotent,
// so the FrameStageNotify path can keep calling it every frame, and
// RestoreAll() is what the unload path needs.
namespace ConVarSpoofing
{
	inline std::unique_ptr<SpoofedConVar> allowCsLua;
	inline std::unique_ptr<SpoofedConVar> cheats;

	// Returns the live instance, spoofing it on first use.  nullptr when the
	// cvar does not exist or the spoof did not take.
	inline SpoofedConVar* Acquire(std::unique_ptr<SpoofedConVar>& slot, const char* name)
	{
		if (slot)
			return slot.get();

		if (!CVar)
			return nullptr;

		ConVar* cvar = CVar->FindVar(name);
		if (!cvar)
			return nullptr;

		auto spoofed = std::make_unique<SpoofedConVar>(cvar);
		if (!spoofed->IsSpoofed())
			return nullptr; // destroyed here; nothing was changed in the engine

		slot = std::move(spoofed);
		return slot.get();
	}

	inline void RestoreAll()
	{
		allowCsLua.reset();
		cheats.reset();
	}
}

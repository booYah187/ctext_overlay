// !!! TWO PLACEHOLDERS REQUIRE INPUT FROM YOU BEFORE THIS COMPILES !!!
//
//   1. CTR_RESOURCE_MANAGER_GET_DATA — the address constant for getData in
//      the Windows Chrono Trigger.exe. ctext has no getData hook today
//      (its resource_manager hook file only wraps createTexture), so the
//      offset must be located once in a disassembler (Ghidra: find the
//      xref to the resources.bin path strings / DetchmanResource calls,
//      or diff against the ctext community if they already have one).
//      Add it to ctext/src/ct/ct.addresses.ixx following the existing
//      naming convention (see CTR_RESOURCE_MANAGER_CREATE_TEXTURE).
//
//   2. The exact signature/type of getData on the PC binary. ct_pm proves
//      the Android signature is getData(const std::string&, int*) returning
//      the data pointer. The PC build is cocos2d-x/MSVC; verify the same
//      shape (it is very likely identical given the ports share the engine
//      generation) and adjust FN_HOOK_A below if not.
// ============================================================================

module;

#include "helpers.hpp"

export module ctext.hooks:ctr.resource_manager_getdata;

import ct.addr;
import ctext.overlay.options;
import ctext.overlay.resolver;

import std;

using namespace ct::addr;

namespace {
	// Adjust parameter list to match the verified PC signature.
	FN_HOOK_A(
		__fastcall, void*, ctr_ResourceManager_getData,
		CTR_RESOURCE_MANAGER_GET_DATA,   // <-- add this constant to ct.addresses.ixx
		const std::string*, path, int*, size
	) {
		if (!path) {
			return CALL_ORIG(ctr_ResourceManager_getData, path, size);
		}

		// Never overlay an overlay request, and skip entirely when the
		// user has no options configured. Both checks are inside
		// Resolver::ResolvePlan / Options::Get(), so the hot path below
		// only runs when there is real work to do.
		auto plan = ctext::overlay::Resolver::ResolvePlan(*path);

		if (plan.empty()) {
			return CALL_ORIG(ctr_ResourceManager_getData, path, size);
		}

		int originalSize = size ? *size : 0;

		for (auto& probe : plan) {
			// Restore the original out-size before each independent
			// lookup (ported from ct_pm: "Restore the original size
			// before each independent lookup").
			if (size)
				*size = originalSize;

			if (probe.kind == ctext::overlay::Resolution::Kind::BinPath) {
				// Retry the ORIGINAL getData with the overlay path, exactly
				// like ct_pm's ct_res_overlay_try_path: the normal
				// ResourceManager -> DetchmanResource -> resources.bin path
				// serves it. There is still only one resources.bin.
				std::string overlayPath(probe.path);
				void* data = CALL_ORIG(ctr_ResourceManager_getData, &overlayPath, size);
				if (data)
					return data;

			} else if (probe.kind == ctext::overlay::Resolution::Kind::DiskFile) {
				std::ifstream file(probe.path, std::ios::binary | std::ios::ate);
				if (!file)
					continue;

				auto fileSize = file.tellg();
				if (fileSize < 0)
					continue;

				auto bytes = static_cast<std::size_t>(fileSize);
				file.seekg(0);

				// CAVEAT (verify before release): getData's ownership
				// contract on the PC build (who frees the returned buffer,
				// and with which allocator) must match how the game treats
				// bin-served buffers. Until verified, we allocate with
				// plain new[] and intentionally LEAK the buffer (bounded:
				// one buffer per distinct overlay asset, loaded once).
				// If the game frees the pointer through its own allocator,
				// switch this to the game's allocator or a free-list cache.
				auto* buffer = new (std::nothrow) std::uint8_t[bytes];
				if (!buffer)
					continue;

				file.read(reinterpret_cast<char*>(buffer),
				          static_cast<std::streamsize>(bytes));
				if (static_cast<std::size_t>(file.gcount()) != bytes) {
					delete[] buffer;
					continue;
				}

				if (size)
					*size = static_cast<int>(bytes);

				return buffer;
			}
		}

		// No configured option contained this resource:
		// restore the original size and perform the normal lookup.
		if (size)
			*size = originalSize;

		return CALL_ORIG(ctr_ResourceManager_getData, path, size);
	}
}

namespace ctext::hooks {
	void EnableCtrResourceManagerGetDataHooks() {
		// Gate on configuration, mirroring ct_pm's
		// "Nothing configured: don't patch anything."
		if (!ctext::overlay::Options::Get().Empty())
			ENABLE_FN_HOOK(ctr_ResourceManager_getData);
	}
}


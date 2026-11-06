module;

#include "helpers.hpp"

#include <miniz/miniz.h>
#include <cstdint>

export module ctext.hooks:detchman_resource;

import ct.addr;
import ctext.config;
import ctext.io;
import ctext.overlay.options;   //[OVERLAY] add
import ctext.overlay.resolver;   //[OVERLAY] add

import std;

using namespace ct::addr;


namespace {
	std::vector<mz_zip_archive*> loadedCtpArchives;
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenFilepaths;

	//[OVERLAY] Loose-file overlay fallback table: keys are
	// "<option>/<value>/<original-path>" lowercased with forward slashes
	// (relative to mods/options/), populated by ScanLooseOptionOverlays().
	// Kept separate from overridenFilepaths so mod overrides and option
	// overrides never interact unintentionally.
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenOptionPaths;

	//[OVERLAY] Walks mods/options/<name>/<value>/... and registers every file
	// found there as a FilesystemFileEntry. No-op when the directory is
	// absent. Exception-guarded so filesystem errors during DLL load can
	// never abort the process.
	void ScanLooseOptionOverlays() {
		const std::filesystem::path optionsRoot = "mods/options";

		try {
			if (!std::filesystem::exists(optionsRoot))
				return;

			for (const auto& entry :
			     std::filesystem::recursive_directory_iterator(optionsRoot)) {
				if (!entry.is_regular_file())
					continue;

				auto rel = entry.path().lexically_relative(optionsRoot).generic_string();
				std::transform(
					rel.begin(), rel.end(), rel.begin(),
					[](unsigned char c) { return std::tolower(c); });

				overridenOptionPaths[rel] =
					new ctext::io::FilesystemFileEntry(entry.path().string());
			}
		} catch (...) {
			// Loose overlays are optional; any failure leaves the table
			// partially populated at worst.
		}
	}

	//[OVERLAY] The ported priority walk (ct_res_overlay_getdata_hook).
	// Returns the winning buffer, or nullptr when no option provides the
	// resource (caller then continues with normal handling).
	//
	// Per option, in config order:
	//   1a. Bin probe, original case (PC resources.bin entry naming).
	//   1b. Bin probe, lowercased (pm/nx-style bin naming fallback).
	//   2.  Disk probe: mods/options/<name>/<value>/<path> (lowercased key).
	// First hit wins. Bin is authoritative within an option.
	uint8_t* TryOptionOverlay(
		const std::string& originalPath,
		size_t* outLen,
		auto&& binProbe
	) {
		ctext::overlay::Options::InitOptions();

		if (ctext::overlay::Options::Empty())
			return nullptr;

		if (ctext::overlay::Options::IsOverlayPath(originalPath))
			return nullptr;

		for (const auto& opt : ctext::overlay::Options::List()) {
			// 1a. resources.bin overlay entry, original case.
			auto binPath = ctext::overlay::BuildBinPath(opt, originalPath);

			if (uint8_t* buf = binProbe(binPath))
				return buf;

			// 1b. resources.bin overlay entry, lowercased.
			auto binPathLower = ctext::overlay::BuildBinPathLower(opt, originalPath);

			if (binPathLower != binPath) {
				if (uint8_t* buf = binProbe(binPathLower))
					return buf;
			}

			// 2. Loose-file fallback: mods/options/<name>/<value>/<path>.
			auto key = ctext::overlay::BuildDiskKey(opt, originalPath);

			if (overridenOptionPaths.contains(key))
				return overridenOptionPaths[key]->GetData(outLen);
		}

		return nullptr;
	}


	//[OVERLAY] Safe config access: Config::Get() throws json
	// type_error.302 when a boolean key in ctext.json is null/absent,
	// which would otherwise abort DLL load entirely. Falls back to
	// defaults instead.
	bool GetModsEnabledSafe() {
		try {
			return ctext::Config::Get().ModsEnabled;
		} catch (...) {
			return false;
		}
	}


	void LoadCtpArchive(const std::filesystem::path& path) {
		const auto ctpFilename = path.generic_string();

		auto ctp = new mz_zip_archive{ 0 };
		mz_zip_reader_init_file(ctp, ctpFilename.c_str(), 0);

		for (unsigned int i = 0; i < mz_zip_reader_get_num_files(ctp); ++i) {
			mz_zip_archive_file_stat fileStat{ 0 };
			mz_zip_reader_file_stat(ctp, i, &fileStat);

			const std::string filename(fileStat.m_filename);
			std::string lcFilename(filename);
			std::transform(
				lcFilename.begin(),
				lcFilename.end(),
				lcFilename.begin(),
				[](unsigned char c) { return std::tolower(c); }
			);

			overridenFilepaths[lcFilename] = new ctext::io::ZipFileEntry(
				ctp,
				ctpFilename,
				filename,
				static_cast<size_t>(fileStat.m_uncomp_size)
			);
		}
	}


	FN_HOOK_A(
		__stdcall, uint8_t*, DetchmanResource_LoadFileEntry,
		DETCHMAN_RESOURCE_LOAD_FILE_ENTRY,
		const std::string&, filename, size_t*, outLen
	) {
		//[OVERLAY] Option overlay takes precedence over mod overrides and
		// default assets. binProbe lambda = "call the ORIGINAL loader with
		// a rewritten path" (the direct analogue of ct_pm's
		// ct_res_overlay_try_path calling the getData trampoline).
		if (uint8_t* buf = TryOptionOverlay(
			    filename,
			    outLen,
			    [&](const std::string& overlayPath) {
				    return CALL_ORIG(DetchmanResource_LoadFileEntry, overlayPath, outLen);
			    })) {
			return buf;
		}

		std::string lcFilename = filename;
		std::transform(
			lcFilename.begin(),
			lcFilename.end(),
			lcFilename.begin(),
			[](unsigned char c) { return std::tolower(c); }
		);

		if (!overridenFilepaths.contains(lcFilename))
			return CALL_ORIG(DetchmanResource_LoadFileEntry, filename, outLen);

		const auto* entry = overridenFilepaths[lcFilename];

		auto* buf = entry->GetData(outLen);

		return buf;
	}
}


export namespace ctext::hooks {
	void InitialiseDetchmanResourceHooks() {
		//[OVERLAY] Register loose option overlays and parse configured
		// options. Both no-ops when absent. Runs regardless of ModsEnabled
		// so the overlay system works standalone.
		ScanLooseOptionOverlays();

		ctext::overlay::Options::InitOptions();

		//[OVERLAY] Mod loading, guarded: a malformed ctext.json or broken
		// mod directory must never abort DLL load.
		try {
			const bool modsEnabled = GetModsEnabledSafe();

			if (!modsEnabled)
				return;

			if (!std::filesystem::exists("mods"))
				return;

			const auto& loadOrder = ctext::Config::Get().ModsLoadOrder;

			for (const auto& load : loadOrder) {
				auto modPath = "mods" / std::filesystem::path(load);

				if (ctext::Config::Get().ModsEnableCtpLoading) {
					std::filesystem::path ctpPath(modPath);
					ctpPath.replace_extension(".ctp");

					if (std::filesystem::exists(ctpPath)) {
						LoadCtpArchive(ctpPath);

						continue;
					}
				}

				if (!std::filesystem::exists(modPath)) {
					LOG_ERROR("Mod path not found '" << load << "'!");
					continue;
				}

				const auto iter = std::filesystem::recursive_directory_iterator(modPath);

				for (const auto& filePath : iter) {
					if (!filePath.is_regular_file())
						continue;

					const auto relPath = filePath.path().lexically_relative(modPath);

					auto pattern = std::filesystem::path(relPath);

					if (pattern.extension() == ".mp3" || pattern.extension() == ".ogg")
						pattern.replace_extension(".sab");

					auto str = pattern.generic_string();
					std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });

					if (!overridenFilepaths.contains(str))
						overridenFilepaths[str] = new io::FilesystemFileEntry(filePath.path().string());
				}
			}
		} catch (...) {
			// Mod loading is optional; overlay functionality is unaffected.
		}
	}

	void EnableDetchmanResourceHooks() {
		//[OVERLAY] The hook must also install when ONLY the overlay system
		// is in use (ModsEnabled false, options configured). When nothing
		// is configured the hook still installs if mods are enabled, but
		// TryOptionOverlay's fast-path returns nullptr immediately and
		// overridenFilepaths is empty, so behaviour remains identical to
		// the unhooked game.
		ctext::overlay::Options::InitOptions();

		const bool overlayActive = !ctext::overlay::Options::Empty();

		if (GetModsEnabledSafe() || overlayActive)
			ENABLE_FN_HOOK(DetchmanResource_LoadFileEntry);
	}

	void UninitialiseDetchmanResourceHooks() {
		if (!GetModsEnabledSafe())
			return;

		for (auto& ctp : loadedCtpArchives)
			mz_zip_reader_end(ctp);
	}
}
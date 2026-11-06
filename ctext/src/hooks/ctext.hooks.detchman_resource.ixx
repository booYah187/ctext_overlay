module;

#include "helpers.hpp"

#include <miniz/miniz.h>
#include <cstdint>

export module ctext.hooks:detchman_resource;

import ct.addr;
import ctext.config;
import ctext.io;
import ctext.overlay.options;    //[OVERLAY] add
import ctext.overlay.resolver;   //[OVERLAY] add

import std;

using namespace ct::addr;

#define LOG_RES_HOOK 1


namespace {
	std::vector<mz_zip_archive*> loadedCtpArchives;
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenFilepaths;

	//[OVERLAY] Loose-file overlay fallback table: keys are
	// "<option>/<value>/<original-path-lowercase>" (relative to mods/options/),
	// populated by ScanLooseOptionOverlays() below. Kept separate from
	// overridenFilepaths so mod overrides and option overrides never
	// interact unintentionally.
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenOptionPaths;

	//[OVERLAY] Walks mods/options/<name>/<value>/... and registers every file
	// found there as a FilesystemFileEntry. No-op when the directory or the
	// option is absent, so the common case costs one stat() call.
	void ScanLooseOptionOverlays() {
		const std::filesystem::path optionsRoot = "mods/options";

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
	}

	//[OVERLAY] The ported priority walk (ct_res_overlay_getdata_hook).
	// Returns the winning buffer, or nullptr when no option provides the
	// resource (caller then continues with normal handling).
	//
	// Per option, in config order:
	//   1. Bin probe:  CALL_ORIG with "options/<name>/<value>/<path>" —
	//      the game's own machinery serves it from the modified
	//      resources.bin. Bin is authoritative within the option.
	//   2. Disk probe: overridenOptionPaths lookup of
	//      "<name>/<value>/<path>" (mods/options/...).
	// First hit anywhere wins. No configured options, or an already-
	// overlay ("options/") path => immediate pass-through.
	uint8_t* TryOptionOverlay(
		const std::string& originalPath,
		size_t* outLen,
		auto&& binProbe
	) {
		const auto& options = ctext::overlay::Options::Get();

		if (options.Empty())
			return nullptr;

		if (ctext::overlay::Options::IsOverlayPath(originalPath))
			return nullptr;

		std::string lcOriginal = originalPath;
		std::transform(
			lcOriginal.begin(), lcOriginal.end(), lcOriginal.begin(),
			[](unsigned char c) { return std::tolower(c); });

		for (const auto& opt : options.List()) {
			// 1. resources.bin overlay entry (authoritative within this option).
			auto binPath = ctext::overlay::BuildBinPath(opt, originalPath);
			if (uint8_t* buf = binProbe(binPath))
				return buf;

			// 2. Loose-file fallback: mods/options/<name>/<value>/<path>.
			auto key = ctext::overlay::BuildDiskKey(opt, lcOriginal);
			if (overridenOptionPaths.contains(key))
				return overridenOptionPaths[key]->GetData(outLen);
		}

		return nullptr;
	}


	void LoadCtpArchive(const std::filesystem::path& path) {
		// ... UNCHANGED existing body ...
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
#if LOG_RES_HOOK
		LOG_DEBUG("RES_HOOK: " << filename);
#endif

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

#if LOG_RES_HOOK
		LOG_DEBUG("RES_HOOK: Redirecting to " << entry->GetFilePath());
#endif

		auto* buf = entry->GetData(outLen);

		return buf;
	}
}


export namespace ctext::hooks {
	void InitialiseDetchmanResourceHooks() {
		//[OVERLAY] Register loose option overlays and scan ctext.json for
		// configured options. Both are no-ops when absent. Runs regardless
		// of ModsEnabled so the overlay system works standalone.
		ScanLooseOptionOverlays();   //[OVERLAY] add

		const auto& cfg = ctext::Config::Get();

		if (!cfg.ModsEnabled)
			return;

		if (!std::filesystem::exists("mods"))
			return;

		// ... UNCHANGED existing body from here ...
		const auto& loadOrder = ctext::Config::Get().ModsLoadOrder;

		for (const auto& load : loadOrder) {
			auto modPath = "mods" / std::filesystem::path(load);

			if (cfg.ModsEnableCtpLoading) {
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
	}

	void EnableDetchmanResourceHooks() {
		//[OVERLAY] CHANGED CONDITION: hook must also install when ONLY the
		// overlay system is in use (ModsEnabled false, options configured).
		// When nothing is configured the hook still installs but the
		// TryOptionOverlay fast-path returns nullptr immediately and
		// overridenFilepaths is empty, so behaviour remains identical to
		// the unhooked game.
		const bool overlayActive = !ctext::overlay::Options::Get().Empty();

		if (ctext::Config::Get().ModsEnabled || overlayActive)
			ENABLE_FN_HOOK(DetchmanResource_LoadFileEntry);
	}

	void UninitialiseDetchmanResourceHooks() {
		if (!ctext::Config::Get().ModsEnabled)
			return;

		for (auto& ctp : loadedCtpArchives)
			mz_zip_reader_end(ctp);
	}
}
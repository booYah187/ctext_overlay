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

#define LOG_RES_HOOK 1


namespace {
	std::vector<mz_zip_archive*> loadedCtpArchives;
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenFilepaths;

	//[OVERLAY] Loose-file overlay fallback table: keys are
	// "<option>/<value>/<original-path>" normalized to lowercase with
	// forward slashes (relative to mods/options/), populated by
	// ScanLooseOptionOverlays() below. Kept separate from
	// overridenFilepaths so mod overrides and option overrides never
	// interact unintentionally.
	std::unordered_map<std::string, ctext::io::FileEntry*> overridenOptionPaths;

	//[OVERLAY] Walks mods/options/<name>/<value>/... and registers every file
	// found there as a FilesystemFileEntry. Fully exception-guarded: any
	// error is logged and swallowed instead of propagating during DLL load.
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
			ctext::overlay::Options::Log(
				"[overlay] loose option scan done (" +
				std::to_string(overridenOptionPaths.size()) + " files)");
		} catch (const std::exception& e) {
			ctext::overlay::Options::Log(
				std::string("[overlay] ERROR scanning mods/options: ") + e.what());
		} catch (...) {
			ctext::overlay::Options::Log(
				"[overlay] ERROR scanning mods/options: unknown exception");
		}
	}

	//[OVERLAY] The ported priority walk (ct_res_overlay_getdata_hook).
	// Returns the winning buffer, or nullptr when no option provides the
	// resource (caller then continues with normal handling).
	uint8_t* TryOptionOverlay(
		const std::string& originalPath,
		size_t* outLen,
		auto&& binProbe
	) {
		//[OVERLAY] ensure options are parsed
		ctext::overlay::Options::InitLogging();

		ctext::overlay::Options::Log(
			"[overlay] resolve request: " + originalPath);

		if (ctext::overlay::Options::Empty())
			return nullptr;

		if (ctext::overlay::Options::IsOverlayPath(originalPath))
			return nullptr;

		for (const auto& opt : ctext::overlay::Options::List()) {
			// 1. resources.bin overlay entry (authoritative within this option).
			auto binPath = ctext::overlay::BuildBinPath(opt, originalPath);

			ctext::overlay::Options::Log("[overlay]   bin probe: " + binPath);

			if (uint8_t* buf = binProbe(binPath))
				return buf;

			// 2. Loose-file fallback: mods/options/<name>/<value>/<path>.
			auto key = ctext::overlay::BuildDiskKey(opt, originalPath);

			ctext::overlay::Options::Log("[overlay]   disk probe: " + key);

			if (overridenOptionPaths.contains(key))
				return overridenOptionPaths[key]->GetData(outLen);
		}

		return nullptr;
	}


	void LoadCtpArchive(const std::filesystem::path& path) {
		ctext::overlay::Options::Log(
			"[overlay] LoadCtpArchive: " + path.generic_string());

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

		ctext::overlay::Options::Log(
			"[overlay] LoadCtpArchive done: " + ctpFilename);
	}


	FN_HOOK_A(
		__stdcall, uint8_t*, DetchmanResource_LoadFileEntry,
		DETCHMAN_RESOURCE_LOAD_FILE_ENTRY,
		const std::string&, filename, size_t*, outLen
	) {
		//[OVERLAY] unconditional trace: proves/disproves hook firing
		ctext::overlay::Options::Log("[overlay] HOOK BODY: " + filename);

#if LOG_RES_HOOK
		LOG_DEBUG("RES_HOOK: " << filename);
#endif

		//[OVERLAY] Option overlay takes precedence over mod overrides and
		// default assets. binProbe lambda = "call the ORIGINAL loader with
		// a rewritten path".
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
		//[OVERLAY] unconditional trace
		ctext::overlay::Options::Log("[overlay] Initialise called");

		//[OVERLAY] Register loose option overlays. Exception-guarded.
		ScanLooseOptionOverlays();   //[OVERLAY] add

		//[OVERLAY] Everything below this point is mod loading, which has
		// been determined to be where the process dies. Wrap the ENTIRE
		// section in try/catch: mod-loading failures must never abort DLL
		// load or take the overlay system down with them. Fine-grained
		// traces bisect exactly which step dies.
		try {
			ctext::overlay::Options::Log("[overlay] init: about to Config::Get()");

			const auto& cfg = ctext::Config::Get();

			ctext::overlay::Options::Log(
				std::string("[overlay] init: ModsEnabled=") +
				(cfg.ModsEnabled ? "true" : "false"));

			if (!cfg.ModsEnabled)
				return;

			if (!std::filesystem::exists("mods")) {
				ctext::overlay::Options::Log("[overlay] init: no mods dir");
				return;
			}

			ctext::overlay::Options::Log("[overlay] init: iterating mod load order");

			const auto& loadOrder = ctext::Config::Get().ModsLoadOrder;

			ctext::overlay::Options::Log(
				"[overlay] init: load order size=" +
				std::to_string(loadOrder.size()));

			for (const auto& load : loadOrder) {
				auto modPath = "mods" / std::filesystem::path(load);

				ctext::overlay::Options::Log(
					"[overlay] init: processing mod '" + load + "'");

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
					ctext::overlay::Options::Log(
						"[overlay] init: mod path not found '" + load + "'");
					continue;
				}

				ctext::overlay::Options::Log(
					"[overlay] init: walking loose mod dir '" + load + "'");

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

				ctext::overlay::Options::Log(
					"[overlay] init: mod done '" + load + "'");
			}

			ctext::overlay::Options::Log("[overlay] init: mod loading complete");
		} catch (const std::exception& e) {
			ctext::overlay::Options::Log(
				std::string("[overlay] EXCEPTION in mod loading: ") + e.what());
		} catch (...) {
			ctext::overlay::Options::Log(
				"[overlay] UNKNOWN EXCEPTION in mod loading");
		}
	}

	void EnableDetchmanResourceHooks() {
		//[OVERLAY] unconditional trace + ensure options parsed
		ctext::overlay::Options::Log("[overlay] Enable called");

		ctext::overlay::Options::InitLogging();

		ctext::overlay::Options::Log(
			std::string("[overlay] ModsEnabled=") +
			(ctext::Config::Get().ModsEnabled ? "true" : "false"));

		const bool overlayActive = !ctext::overlay::Options::Empty();

		ctext::overlay::Options::Log(
			std::string("[overlay] overlayActive=") +
			(overlayActive ? "true" : "false"));

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
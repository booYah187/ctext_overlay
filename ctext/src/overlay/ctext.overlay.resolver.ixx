module;

export module ctext.overlay.resolver;

import ctext.overlay.options;

import std;

export namespace ctext::overlay {

	//[OVERLAY] Converts backslashes to forward slashes, preserving case.
	// Used for the resources.bin probe: the PC bin stores entry names in
	// their original case (e.g. "Localize/en/msg/battle.txt"), unlike the
	// pm/nx bins which were fully lowercased.
	inline std::string BinNormalize(std::string_view path) {
		std::string out;
		out.reserve(path.size());
		for (char c : path)
			out += (c == '\\') ? '/' : c;
		return out;
	}

	//[OVERLAY] Lowercase + slash-normalize. Used for the loose-file key
	// table, whose keys are produced by ScanLooseOptionOverlays().
	inline std::string DiskNormalize(std::string_view path) {
		std::string out;
		out.reserve(path.size());
		for (char c : path) {
			char d = (c == '\\') ? '/' : c;
			out += static_cast<char>(
				std::tolower(static_cast<unsigned char>(d)));
		}
		return out;
	}

	// Builds "options/<option>/<value>/<original>" — the direct port of
	// ct_res_overlay_build_path. Served by the GAME's own
	// ResourceManager -> DetchmanResource -> resources.bin machinery.
	// Path preserves original case (PC bin entry naming), slashes
	// normalized to forward slashes.
	inline std::string BuildBinPath(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += "options/";
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += BinNormalize(original);
		return out;
	}

	// Builds the lowercased bin path variant. Probed as a fallback for
	// bins whose entry tables were generated lowercased (the pm/nx-style
	// layout, which the overlay-tree generation tooling historically
	// produced).
	inline std::string BuildBinPathLower(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += "options/";
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += DiskNormalize(original);
		return out;
	}

	// Builds the key for the loose-file fallback:
	// "<option>/<value>/<original>" relative to mods/options/.
	// Lowercased, forward slashes — matches the scan's key table.
	inline std::string BuildDiskKey(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += DiskNormalize(original);
		return out;
	}
}
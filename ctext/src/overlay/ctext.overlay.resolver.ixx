module;

export module ctext.overlay.resolver;

import ctext.overlay.options;

import std;

export namespace ctext::overlay {

	//[OVERLAY] Normalizes a resource path to the canonical form used inside
	// resources.bin and by the loose-overlay key tables: lowercased,
	// backslashes converted to forward slashes. The PM-generated overlay
	// trees use lowercase/forward-slash layout exclusively; the Steam exe
	// may hand the loader mixed-case or backslash paths.
	inline std::string NormalizePath(std::string_view path) {
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
	// ResourceManager -> DetchmanResource -> resources.bin machinery
	// (the overlay entries live inside the same, modified resources.bin).
	// Path is normalized (lowercase + forward slashes) so the probe path
	// matches the entry names actually stored in the bin.
	inline std::string BuildBinPath(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += "options/";
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += NormalizePath(original);
		return out;
	}

	// Builds the key for the loose-file fallback:
	// "<option>/<value>/<original>" relative to mods/options/.
	// Key format matches the lowercased, slash-normalized keys produced by
	// ScanLooseOptionOverlays()'s map population.
	inline std::string BuildDiskKey(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += NormalizePath(original);
		return out;
	}
}
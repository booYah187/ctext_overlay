module;

export module ctext.overlay.resolver;

import ctext.overlay.options;

import std;

export namespace ctext::overlay {

	// Builds "options/<option>/<value>/<original>" — the direct port of
	// ct_res_overlay_build_path. Served by the GAME's own
	// ResourceManager -> DetchmanResource -> resources.bin machinery
	// (the overlay entries live inside the same, modified resources.bin).
	inline std::string BuildBinPath(const Option& opt, std::string_view original) {
		std::string out;
		out.reserve(original.size() + opt.name.size() + 24);
		out += "options/";
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += original;
		return out;
	}

	// Builds the key for the loose-file fallback:
	// "<option>/<value>/<original-lowercase>" relative to mods/options/.
	// (Key format matches the lowercased keys used by the detchman-resource
	// hook's override maps.)
	inline std::string BuildDiskKey(const Option& opt, std::string_view lcOriginal) {
		std::string out;
		out.reserve(lcOriginal.size() + opt.name.size() + 24);
		out += opt.name;
		out += '/';
		out += std::to_string(opt.value);
		out += '/';
		out += lcOriginal;
		return out;
	}
}
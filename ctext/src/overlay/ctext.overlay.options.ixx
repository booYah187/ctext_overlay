module;
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <cstdio>

export module ctext.overlay.options;

import std;
export namespace ctext::overlay {

	// One configured overlay selection, in ctext.json file order.
	struct Option {
		std::string name;
		int value;
	};

	// Parses the "options" object from ctext.json.
	//
	//   "options": {
	//     "myface": 1,
	//     "face": 1,
	//     "ui": 0
	//   }
	//
	// Semantics (ported from ct_pm_mod config.c/h + resource_overlay.h):
	//   - nlohmann::ordered_json preserves FILE ORDER (plain nlohmann::json
	//     would sort keys alphabetically and silently break priority).
	//   - Order = priority: earlier options beat later ones.
	//   - 0 = disabled, identical to omitting the key.
	//   - Missing file / missing "options" key => overlay system inert and
	//     existing ctext behaviour is byte-identical.
	class Options final {
	public:
		static const Options& Get() {
			static Options instance;
			return instance;
		}

		const std::vector<Option>& List() const { return m_options; }
		bool Empty() const { return m_options.empty(); }

		static bool IsOverlayPath(std::string_view path) {
			return path.starts_with("options/");
		}

		// Debug helper: append a line to overlay_debug.log next to the
		// game executable (current working directory at time of call).
		// Debug helper: append a line to overlay_debug.log next to the game
// executable, regardless of the process working directory.
static void Log(std::string_view line) {
    static const std::string logPath = [] {
        char exe[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string p(exe);
        auto slash = p.find_last_of("\\/");
        return (slash == std::string::npos ? std::string(".") : p.substr(0, slash + 1))
            + "overlay_debug.log";
    }();

    FILE* f = nullptr;
    fopen_s(&f, logPath.c_str(), "a");
    if (f) {
        fwrite(line.data(), 1, line.size(), f);
        fputc('\n', f);
        fclose(f);
    }
}
		std::vector<Option> m_options;
	};
}
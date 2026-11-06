module;
#include <nlohmann/json.hpp>
#include <Windows.h>

// Replace the Log member with:
static void Log(std::string_view line) {
    static const std::string logPath = [] {
        char exe[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string p(exe);
        return p.substr(0, p.find_last_of("\\/") + 1) + std::string("overlay_debug.log");
    }();
    std::ofstream dbg(logPath, std::ios::app);
    if (dbg)
        dbg << line << "\n";
}

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
		static void Log(std::string_view line) {
			std::ofstream dbg("overlay_debug.log", std::ios::app);
			if (dbg)
				dbg << line << "\n";
		}

	private:
		Options() {
			Log("[overlay] Options ctor: reading ctext.json...");

			std::ifstream file("ctext.json");
			if (!file) {
				Log("[overlay] ERROR: could not open ctext.json in CWD");
				return;
			}

			Log("[overlay] ctext.json opened OK");

			try {
				auto cfg = nlohmann::ordered_json::parse(
					file, nullptr, true, true, true);

				if (!cfg.is_object()) {
					Log("[overlay] ERROR: ctext.json root is not an object");
					return;
				}

				if (!cfg.contains("options")) {
					Log("[overlay] no \"options\" key present -> overlay inert");
					return;
				}

				const auto& opts = cfg["options"];
				if (!opts.is_object()) {
					Log("[overlay] ERROR: \"options\" is not an object");
					return;
				}

				for (auto it = opts.begin(); it != opts.end(); ++it) {
					if (!it.value().is_number_integer()) {
						Log("[overlay] skip \"" + it.key() +
							"\": value is not an integer");
						continue;
					}

					const int value = it.value().get<int>();
					if (value == 0) {
						Log("[overlay] skip \"" + it.key() +
							"\": value is 0 (disabled)");
						continue;
					}

					m_options.push_back({ it.key(), value });
					Log("[overlay] option accepted: " + it.key() + " = " +
						std::to_string(value));
				}

				Log("[overlay] total active options: " +
					std::to_string(m_options.size()));
			} catch (const std::exception& e) {
				Log(std::string("[overlay] EXCEPTION parsing ctext.json: ") +
					e.what());
			}
		}

		std::vector<Option> m_options;
	};
}
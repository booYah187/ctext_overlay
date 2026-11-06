module;
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <cstdio>
#include <fstream>

export module ctext.overlay.options;

import std;

export namespace ctext::overlay {

	struct Option {
		std::string name;
		int value;
	};

	class Options final {
	public:
		// inline static members: no out-of-class definitions needed
		inline static std::vector<Option> s_options{};
		inline static FILE* s_con_out = nullptr;
		inline static FILE* s_con_err = nullptr;
		inline static bool s_initialized = false;

		static bool Empty() { return s_options.empty(); }
		static const std::vector<Option>& List() { return s_options; }

		static bool IsOverlayPath(std::string_view path) {
			return path.starts_with("options/");
		}

		// Runs on first asset load (after DllMain), parses ctext.json
		// "options" block and initializes debug logging/console.
		static void InitLogging() {
			if (s_initialized) return;
			s_initialized = true;

			// Debug console (harmless if it fails)
			AllocConsole();
			freopen_s(&s_con_out, "CONOUT$", "w", stdout);
			freopen_s(&s_con_err, "CONOUT$", "w", stderr);

			Log("[overlay] init (post-DllMain)");
			Log("[overlay] reading ctext.json...");

			std::ifstream file("ctext.json");
			if (!file) {
				Log("[overlay] ERROR: could not open ctext.json in CWD");
				return;
			}

			Log("[overlay] ctext.json opened OK");

			try {
				auto cfg = nlohmann::ordered_json::parse(file);

				if (!cfg.is_object()) {
					Log("[overlay] ERROR: root is not an object");
					return;
				}
				if (!cfg.contains("options")) {
					Log("[overlay] no \"options\" key -> overlay inert");
					return;
				}

				const auto& opts = cfg["options"];
				if (!opts.is_object()) {
					Log("[overlay] ERROR: \"options\" is not an object");
					return;
				}

				// ordered_json preserves file order = priority order
				for (auto it = opts.begin(); it != opts.end(); ++it) {
					if (!it.value().is_number_integer()) {
						Log("[overlay] skip \"" + it.key() + "\": not integer");
						continue;
					}
					const int value = it.value().get<int>();
					if (value == 0) {
						Log("[overlay] skip \"" + it.key() + "\": 0 = default");
						continue;
					}
					s_options.push_back({ it.key(), value });
					Log("[overlay] option accepted: " + it.key() + " = " +
						std::to_string(value));
				}

				Log("[overlay] total active options: " +
					std::to_string(s_options.size()));
			} catch (const std::exception& e) {
				Log(std::string("[overlay] EXCEPTION: ") + e.what());
			}
		}

		static void Log(std::string_view line) {
			static const std::string logPath = [] {
				char exe[MAX_PATH]{};
				GetModuleFileNameA(nullptr, exe, MAX_PATH);
				std::string p(exe);
				auto slash = p.find_last_of("\\/");
				return (slash == std::string::npos
					? std::string(".")
					: p.substr(0, slash + 1)) + "overlay_debug.log";
			}();

			FILE* f = nullptr;
			fopen_s(&f, logPath.c_str(), "a");
			if (f) {
				fwrite(line.data(), 1, line.size(), f);
				fputc('\n', f);
				fclose(f);
			}
		}
	};
}
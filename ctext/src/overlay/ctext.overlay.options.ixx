CText Overlay Options Lazy Init · cpp


module;
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <cstdio>

export module ctext.overlay.options;

import std;

export namespace ctext::overlay {

	struct Option {
		std::string name;
		int value;
	};

	class Options final {
	public:
		static const Options& Get() {
			// Lazy initialization: ensures we run after DllMain
			// and that the console/file operations succeed.
			static Options instance;
			return instance;
		}

		// NOTE: static storage so static functions (InitLogging, Log)
		// can access it without a `this` pointer.
		static std::vector<Option>& MutableList() { return s_options; }
		static bool Empty() { return s_options.empty(); }
		static const std::vector<Option>& List() { return s_options; }

		static bool IsOverlayPath(std::string_view path) {
			return path.starts_with("options/");
		}

		// Trigger logging on first access.
		// This runs AFTER DllMain, ensuring console/file ops work.
		static void InitLogging() {
			static bool initialized = false;
			if (initialized) return;
			initialized = true;

			// Force console allocation now (safer than DllMain)
			AllocConsole();
			freopen_s(&s_con_out, "CONOUT$", "w", stdout);
			freopen_s(&s_con_err, "CONOUT$", "w", stderr);

			Log("[overlay] Logging initialized (post-DllMain)");
			Log("[overlay] Reading ctext.json...");

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

					s_options.push_back({ it.key(), value });
					Log("[overlay] option accepted: " + it.key() + " = " +
						std::to_string(value));
				}

				Log("[overlay] total active options: " +
					std::to_string(s_options.size()));
			} catch (const std::exception& e) {
				Log(std::string("[overlay] EXCEPTION parsing ctext.json: ") +
					e.what());
			}
		}

		// Debug helper: append a line to overlay_debug.log next to the
		// game executable.
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

	private:
		Options() {
			// Constructor does NOTHING. Initialization happens in InitLogging().
			// This prevents DllMain issues.
		}

		static std::vector<Option> s_options;
		static FILE* s_con_out;
		static FILE* s_con_err;
	};

	// Static member definitions (file-local to this module unit)
	static std::vector<Option> Options::s_options{};
	static FILE* Options::s_con_out = nullptr;
	static FILE* Options::s_con_err = nullptr;
}
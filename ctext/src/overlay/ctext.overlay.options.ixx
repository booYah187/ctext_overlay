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
		inline static std::vector<Option> s_options{};
		inline static bool s_initialized = false;

		static bool Empty() { return s_options.empty(); }
		static const std::vector<Option>& List() { return s_options; }

		static bool IsOverlayPath(std::string_view path) {
			return path.starts_with("options/");
		}

		// Runs on first asset load / hook enable (post-DllMain safe).
		static void InitLogging() {
			if (s_initialized) return;
			s_initialized = true;

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

		// Appends to overlay_debug.log. Tries, in order:
		//   1. next to the game executable
		//   2. current working directory
		//   3. %TEMP%
		// The first location that accepts a write is cached. NOTE: there is
		// NO recursion here — the location probe calls WriteLine directly.
		// (A previous version called Log() inside the probe block while
		// location was still undecided, causing infinite recursion.)
		static void Log(std::string_view line) {
			// Decide routing once. std::optional-style: -1 undecided,
			// 0..2 = location index, 3 = nowhere writable.
			static int location = DecideLocation();

			switch (location) {
				case 0: {
					std::string base = ExeDir();
					WriteLine(base + "overlay_debug.log", line);
					break;
				}
				case 1:
					WriteLine("overlay_debug.log", line);
					break;
				case 2: {
					char tempBuf[MAX_PATH]{};
					GetTempPathA(MAX_PATH, tempBuf);
					WriteLine(std::string(tempBuf) + "ctext_overlay_debug.log", line);
					break;
				}
				default:
					OutputDebugStringA("[overlay] LOG WRITE FAILED: ");
					OutputDebugStringA(line.data());
					OutputDebugStringA("\n");
					break;
			}
		}

	private:
		static std::string ExeDir() {
			char exe[MAX_PATH]{};
			GetModuleFileNameA(nullptr, exe, MAX_PATH);
			std::string p(exe);
			auto slash = p.find_last_of("\\/");
			return (slash == std::string::npos)
				? std::string(".") : p.substr(0, slash + 1);
		}

		// Pure probe: decides the routing index. Never calls Log().
		static int DecideLocation() {
			// probe 1: exe dir
			FILE* f = nullptr;
			std::string p1 = ExeDir() + "overlay_probe.tmp";
			fopen_s(&f, p1.c_str(), "w");
			if (f) { fclose(f); DeleteFileA(p1.c_str()); return 0; }

			// probe 2: cwd
			fopen_s(&f, "overlay_probe.tmp", "w");
			if (f) { fclose(f); DeleteFileA("overlay_probe.tmp"); return 1; }

			// probe 3: temp
			char tempBuf[MAX_PATH]{};
			GetTempPathA(MAX_PATH, tempBuf);
			std::string p3 = std::string(tempBuf) + "overlay_probe.tmp";
			fopen_s(&f, p3.c_str(), "w");
			if (f) { fclose(f); DeleteFileA(p3.c_str()); return 2; }

			return 3;
		}

		static void WriteLine(const std::string& path, std::string_view line) {
			FILE* f = nullptr;
			fopen_s(&f, path.c_str(), "a");
			if (f) {
				fwrite(line.data(), 1, line.size(), f);
				fputc('\n', f);
				fclose(f);
			} else {
				OutputDebugStringA("[overlay] LOG WRITE FAILED: ");
				OutputDebugStringA(line.data());
				OutputDebugStringA("\n");
			}
		}
	};
}
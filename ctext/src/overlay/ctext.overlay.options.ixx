module;

#include <nlohmann/json.hpp>

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

		// Parses the "options" block from ctext.json. Runs once, guarded;
		// safe to call from any phase after DllMain. ordered_json preserves
		// file order, which IS the priority order for the overlay walk.
		static void InitOptions() {
			if (s_initialized) return;
			s_initialized = true;

			std::ifstream file("ctext.json");
			if (!file)
				return;

			try {
				auto cfg = nlohmann::ordered_json::parse(file);

				if (!cfg.is_object() || !cfg.contains("options"))
					return;

				const auto& opts = cfg["options"];
				if (!opts.is_object())
					return;

				for (auto it = opts.begin(); it != opts.end(); ++it) {
					if (!it.value().is_number_integer())
						continue;

					const int value = it.value().get<int>();
					if (value == 0)
						continue; // 0 = explicitly default/disabled

					s_options.push_back({ it.key(), value });
				}
			} catch (...) {
				// Malformed config must never abort DLL load; overlay
				// simply stays inert.
				s_options.clear();
			}
		}
	};
}
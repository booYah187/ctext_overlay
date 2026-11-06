module;

#include <nlohmann/json.hpp>

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

	private:
		Options() {
			std::ifstream file("ctext.json");
			if (!file)
				return;

			auto cfg = nlohmann::ordered_json::parse(
				file, nullptr, true, true, true);

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
					continue;

				m_options.push_back({ it.key(), value });
			}
		}

		std::vector<Option> m_options;
	};
}
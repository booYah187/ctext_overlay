export module ctext.config;

import std;

import ctext.singleton;

using json = nlohmann::json;

export namespace ctext {
	Config::Config(const std::filesystem::path& configPath) {
		if (!std::filesystem::exists(configPath))
			return;

		std::ifstream configFile(configPath);

		try {
			data = json::parse(configFile);
		}
		catch (json::exception& e) {
			printf_s("Failed to parse config: %s\n", e.what());
		}
	}

	void Config::Save() {
		std::ofstream configFile(dataFilePath);

		configFile << data.dump(4);

		configFile.close();
	}

	const Config& Config::Get() {
		return Singleton<Config>::Get(dataFilePath);
	}

	bool Config::GetBool(const std::string& key, bool defaultValue) const {
		// contains() is not enough: a key present with value null must
		// also fall back to the default instead of throwing 302 (which
		// aborted DLL load under ctext's config schema).
		if (!data.contains(key))
			return defaultValue;

		const auto& value = data.at(key);

		if (!value.is_boolean()) {
			printf_s("Config key '%s' is not a boolean (value: %s) - using default: %s\n",
			         key.c_str(),
			         value.is_null() ? "null" : value.dump().c_str(),
			         defaultValue ? "true" : "false");
			return defaultValue;
		}

		return value.get<bool>();
	}

	int Config::GetInt(const std::string& key, int defaultValue) const {
		if (!data.contains(key))
			return defaultValue;

		const auto& value = data.at(key);

		if (!value.is_number_integer()) {
			printf_s("Config key '%s' is not an integer - using default: %d\n",
			         key.c_str(), defaultValue);
			return defaultValue;
		}

		return value.get<int>();
	}

	std::string Config::GetString(const std::string& key, const std::string& defaultValue) const {
		if (!data.contains(key))
			return defaultValue;

		const auto& value = data.at(key);

		if (!value.is_string()) {
			printf_s("Config key '%s' is not a string - using default: %s\n",
			         key.c_str(), defaultValue.c_str());
			return defaultValue;
		}

		return value.get<std::string>();
	}

	bool Config::ShouldReplaceImages() const {
		return GetBool("replace_images", true);
	}

	bool Config::AreModsEnabled() const {
		return GetBool("mods_enabled", false);
	}

	bool Config::AreVoiceCommandsEnabled() const {
		return GetBool("enable_voice_acting", true);
	}

	bool Config::UnloadOnMenu() const {
		return GetBool("force_unload_ctext_on_menu_exit", false);
	}

	std::vector<std::string> Config::GetModsLoadOrder() const {
		// ...
		if (!data.contains("mods_load_order") || !data.at("mods_load_order").is_array())
			return {};

		return data.at("mods_load_order").get<std::vector<std::string>>();
	}

	// ... (remaining getters unchanged in logic, but routed through the
	//      safe GetBool/GetInt/GetString helpers where they touch scalars)
}
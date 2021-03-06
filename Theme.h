#ifndef THEME_H
#define THEME_H

#include "TowerSettings.h"
#include "json_spirit/json_spirit.h"

class Theme
{
public:
	void LoadTheme(const std::string& name);

	const sf::Font& GetMainFont() const
	{
		return mainFont;
	}

	const sf::Vector2f GetPosition(const std::string& path, int idx = -1) const
	{
		auto val = TraversePath(path, idx);
		assert(std::get<0>(val));
		json_spirit::mArray arr = std::get<1>(val).get_array();
		return sf::Vector2f(static_cast<float>(arr[0].get_real()), static_cast<float>(arr[1].get_real()));
	}

	int GetInt(const std::string& path, int idx = -1) const
	{
		auto val = TraversePath(path, idx);
		assert(std::get<0>(val));
		return std::get<1>(val).get_int();
	}

	float GetFloat(const std::string& path, int idx = -1) const
	{
		auto val = TraversePath(path, idx);
		assert(std::get<0>(val));
		return static_cast<float>(std::get<1>(val).get_real());
	}

	std::string GetString(const std::string& path, int idx = -1) const
	{
		auto val = TraversePath(path, idx);
		assert(std::get<0>(val));
		return std::get<1>(val).get_str();
	}

	std::string GetFileName(const std::string& path, int idx = -1) const;
	Color GetColor(const std::string& path, int idx = -1) const;

	size_t GetArrayLength(const std::string& path, int idx = -1) const
	{
		auto val = TraversePath(path, idx);
		assert(std::get<0>(val));
		return std::get<1>(val).get_array().size();
	}

	const TowerSettings* GetTowerSettings(size_t i) const
	{
		return &towerSettings.at(i);
	}

	bool KeyExists(const std::string& path, int idx = -1) const
	{
		return std::get<0>(TraversePath(path, idx));
	}

private:
	json_spirit::mObject rootObj;

	std::tuple<bool, json_spirit::mValue> TraversePath(const std::string& path, int idx = -1) const;

	//const json_spirit::mValue& TraversePath(const std::string& path, int idx = -1) const;

	std::string currentTheme;

	sf::Font mainFont;

	std::vector<TowerSettings> towerSettings;

	void LoadTowerSettings();
};

extern Theme gTheme;

#endif //THEME_H

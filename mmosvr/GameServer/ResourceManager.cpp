#include "pch.h"
#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "SpawnEntry.h"
#include "SkillTemplate.h"


void ResourceManager::Init()
{
	Register<MonsterTemplate>("monster_templates.csv");
	Register<SpawnEntry>("spawn_entries.csv");
	Register<SkillTemplate>("skill_templates.csv");

	LOG_INFO("ResourceManager initialized");
}


std::string ResourceManager::FindDataFile(const std::string& filename)
{
	namespace fs = std::filesystem;

	std::vector<std::string> candidates = {
		"../../../../ShareDir/data/" + filename,
		"../../../ShareDir/data/" + filename,
		"../../ShareDir/data/" + filename,
		"../ShareDir/data/" + filename,
		"ShareDir/data/" + filename,
		"data/" + filename,
	};

	for (const auto& path : candidates)
	{
		if (fs::exists(path))
			return fs::absolute(path).string();
	}

	return {};
}
#include "pch.h"
#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "MonsterSkillEntry.h"
#include "SpawnEntry.h"
#include "SkillTemplate.h"
#include "Effect.h"
#include "SkillEffect.h"
#include "SkillBehavior.h"
#include "SkillBehaviorRegistry.h"

#include <stdexcept>


void ResourceManager::Init()
{
	// Behavior 레지스트리는 SkillTable 로딩보다 먼저 등록되어야 한다.
	// SkillTable::OnLoaded 가 각 SkillTemplate.behaviorName → Behavior 를 조회하므로.
	SkillBehaviorRegistry::Instance().Register("default",
		[] { return std::make_shared<DefaultAttackBehavior>(); });

	Register<MonsterTemplate>("monster_templates.csv");
	Register<SpawnEntry>("spawn_entries.csv");
	Register<SkillTemplate>("skill_templates.csv");
	Register<Effect>("effects.csv");
	Register<SkillEffectEntry>("skill_effects.csv");
	Register<MonsterSkillEntry>("monster_skills.csv");

	// 모든 테이블 로드 후 조인 키 검증. 실패 시 throw → 서버 부팅 거부.
	ValidateReferences();

	LOG_INFO("ResourceManager initialized");
}


void ResourceManager::ValidateReferences() const
{
	// 각 테이블이 자기 FK 를 스스로 검증. RM 은 등록 순서대로 디스패치만.
	// 실제 에러 상세는 각 OnValidate 내부에서 LOG_ERROR 로 기록.
	int errors = 0;
	for (const auto* table : registrationOrder_)
		errors += table->OnValidate();

	if (errors > 0)
	{
		LOG_ERROR(std::format("ResourceManager: {} reference validation error(s) — aborting boot", errors));
		throw std::runtime_error("Resource reference validation failed");
	}
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
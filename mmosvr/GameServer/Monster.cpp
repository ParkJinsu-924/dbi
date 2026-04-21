#include "pch.h"
#include "Monster.h"
#include "Agent/BuffAgent.h"
#include "Agent/SkillCooldownAgent.h"
#include "Agent/FSMAgent.h"
#include "Zone.h"
#include "Player.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "MonsterSkillEntry.h"
#include "game.pb.h"
#include <cmath>
#include <random>


void Monster::InitAI(const Proto::Vector2& spawnPos)
{
	spawnPos_ = spawnPos;
	position_ = spawnPos;

	// FSM 상태 등록 / 시작은 FSMAgent 가 전담.
	Get<FSMAgent>().Init();
}

void Monster::Update(const float deltaTime)
{
	Unit::Update(deltaTime);
	// FSM 은 FSMAgent.Tick 에서 이미 실행됨 (CanAct 체크 포함).
}

// ---------------------------------------------------------------------------
// Public utilities (상태 클래스에서 호출)
// ---------------------------------------------------------------------------

std::shared_ptr<Player> Monster::GetTarget() const
{
	const long long guid = GetTargetGuid();
	if (guid == 0)
		return nullptr;
	return GetZone().FindAs<Player>(guid);
}

float Monster::DistanceToSpawn() const
{
	return DistanceTo(spawnPos_);   // GameObject::DistanceTo 재사용
}

float Monster::GetBasicSkillRange() const
{
	const auto* skTable       = GetResourceManager().Get<SkillTemplate>();
	const auto* monsterSkills = GetResourceManager().Get<MonsterSkillEntry>();
	if (!skTable || !monsterSkills) return 0.0f;

	const auto* basicEntry = monsterSkills->FindBasicByMonster(templateId_);
	if (!basicEntry) return 0.0f;

	const SkillTemplate* tmpl = skTable->Find(basicEntry->skillId);
	return tmpl ? tmpl->cast_range : 0.0f;
}

std::optional<Monster::SkillChoice> Monster::PickCastable(const float now, const float distance) const
{
	const auto* skTable       = GetResourceManager().Get<SkillTemplate>();
	const auto* monsterSkills = GetResourceManager().Get<MonsterSkillEntry>();
	if (!skTable || !monsterSkills) return std::nullopt;

	const auto& entries = monsterSkills->FindByMonster(templateId_);

	// CanCast 는 target 이 필요. 없으면 후보 전체 탈락.
	const auto target = GetTarget();
	if (!target) return std::nullopt;

	// 1. 시전 가능한 후보 수집 + 가중치 총합.
	struct Candidate { const MonsterSkillEntry* entry; const SkillTemplate* tmpl; float cooldown; };
	std::vector<Candidate> candidates;
	candidates.reserve(entries.size());
	int32 totalWeight = 0;

	for (const auto* entry : entries)
	{
		const SkillTemplate* tmpl = skTable->Find(entry->skillId);
		if (!tmpl) continue;
		if (distance > tmpl->cast_range) continue;

		if (!Get<SkillCooldownAgent>().IsReady(entry->skillId, now))
			continue;

		// Strategy: 스킬별 추가 조건 (HP threshold 등). Default 는 항상 true.
		if (tmpl->behavior && !tmpl->behavior->CanCast(*this, *target, now)) continue;

		const float applied = (std::max)(tmpl->cooldown, entry->minInterval);
		candidates.push_back({ entry, tmpl, applied });
		totalWeight += entry->weight;
	}

	if (candidates.empty()) return std::nullopt;

	// 2. 가중 추첨 (weight 합 기반).
	static thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<int32> dist(0, totalWeight - 1);
	int32 roll = dist(rng);
	for (const auto& c : candidates)
	{
		roll -= c.entry->weight;
		if (roll < 0)
			return SkillChoice{ c.tmpl, c.entry->skillId, c.cooldown };
	}
	// 부동소수 없이 정수로 돌리므로 unreachable. 컴파일러 경고 방지용 fallback.
	return SkillChoice{ candidates.back().tmpl, candidates.back().entry->skillId, candidates.back().cooldown };
}

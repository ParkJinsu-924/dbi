#include "pch.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "MonsterSkillEntry.h"
#include "PacketMaker.h"
#include "Utils/MathUtil.h"
#include "game.pb.h"
#include <cmath>
#include <random>


void Monster::InitAI(const Proto::Vector2& spawnPos)
{
	spawnPos_ = spawnPos;
	position_ = spawnPos;

	// GlobalState: detect player in Idle/Patrol -> Chase
	fsm_.SetGlobalState<MonsterGlobalState>();

	// 상태 등록
	fsm_.AddState<IdleState>(MonsterStateId::Idle);
	fsm_.AddState<PatrolState>(MonsterStateId::Patrol);
	fsm_.AddState<EngageState>(MonsterStateId::Engage);
	fsm_.AddState<ReturnState>(MonsterStateId::Return);

	// 상태 전환 콜백 (로그 + 브로드캐스트)
	fsm_.SetOnStateChanged([this](MonsterStateId prev, MonsterStateId next)
		{
			BroadcastState(prev, next);
		});

	// 시작 (Idle 상태로 시작)
	fsm_.Start(*this, MonsterStateId::Idle);
}

void Monster::Update(const float deltaTime)
{
	TickBuffs(deltaTime);
	// 총체적 행동 불가 상태(현재는 Stun) 에서는 FSM 자체를 스킵.
	// (TickBuffs 는 먼저 수행돼 stun duration 이 시간이 지나며 풀린다)
	if (!CanAct())
		return;
	fsm_.Update(deltaTime);
}

// ---------------------------------------------------------------------------
// Aggro (delegated to AggroTable)
// ---------------------------------------------------------------------------

void Monster::AddAggro(const long long playerGuid, const float amount)
{
	aggro_.Add(playerGuid, amount);
}

long long Monster::GetTopAggroGuid() const
{
	return aggro_.ResolveTop();
}

bool Monster::HasAggro() const
{
	return !aggro_.Empty();
}

void Monster::ClearAggro()
{
	aggro_.Clear();
}

// ---------------------------------------------------------------------------
// Public utilities (상태 클래스에서 호출)
// ---------------------------------------------------------------------------

std::shared_ptr<Player> Monster::GetTarget() const
{
	if (targetGuid_ == 0)
		return nullptr;
	return GetZone().FindAs<Player>(targetGuid_);
}

float Monster::DistanceToSpawn() const
{
	return DistanceTo(spawnPos_);   // GameObject::DistanceTo 재사용
}

void Monster::MoveToward(const Proto::Vector2& target, const float deltaTime)
{
	// 이동 불가 CC(Stun/Root) 차단. 공격 가능 여부는 DefaultAttackBehavior 에서 별도 판정.
	if (!CanMove())
		return;

	const float dx = target.x() - position_.x();
	const float dz = target.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);

	if (dist < 0.001f)
		return;

	float step = GetEffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step > dist)
		step = dist;

	position_.set_x(position_.x() + (dx / dist) * step);
	position_.set_y(position_.y() + (dz / dist) * step);
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

		const auto it = skillNextUsable_.find(entry->skillId);
		const float ready = (it == skillNextUsable_.end()) ? 0.0f : it->second;
		if (now < ready) continue;

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

// ---------------------------------------------------------------------------
// Broadcasting
// ---------------------------------------------------------------------------

void Monster::BroadcastState(MonsterStateId /*prev*/, MonsterStateId next)
{
	GetZone().Broadcast(PacketMaker::MakeMonsterState(*this, next));
}

#pragma once

#include "ResourceManager.h"
#include <vector>


// ─────────────────────────────────────────────────────────────────────
// SkillEffectEntry — skill_effects.csv 의 한 행.
// "스킬 sid 가 발동되면, trigger 시점에 effect eid 를 delay 초 뒤 적용한다."
//
// trigger 별 의미:
//   OnCast : 캐스트 순간 즉발 (버프 자기 강화, 자원 회복 등)
//   OnHit  : 투사체가 타겟에 명중했을 때 (데미지, 슬로우 부여 등)
//   OnTick : 주기적 반복 (DoT — Phase 2+)
// ─────────────────────────────────────────────────────────────────────

enum class EffectTrigger : int32
{
	OnCast = 0,
	OnHit  = 1,
	OnTick = 2,   // Phase 2+
};


class SkillEffectTable;


struct SkillEffectEntry
{
	// 합성 키 (sid, eid) → 단일 int32.
	// 현재 sid/eid 는 모두 10000 미만. 스케일업 시 키 전략 재검토.
	using KeyType = int64;
	using Table   = SkillEffectTable;

	int32         sid     = 0;
	int32         eid     = 0;
	EffectTrigger trigger = EffectTrigger::OnHit;
	float         delay   = 0.0f;

	KeyType GetKey() const
	{
		return (static_cast<int64>(sid) << 32) | static_cast<uint32>(eid);
	}

	CSV_DEFINE_TYPE(SkillEffectEntry, sid, eid, trigger, delay)
};


class SkillEffectTable : public KeyedResourceTable<SkillEffectEntry>
{
public:
	// 한 스킬에 연결된 모든 effect 엔트리 반환 (trigger 무관, 호출측 필터).
	// Phase 1: 스킬당 엔트리가 1~2 개이므로 선형 탐색 충분.
	// Phase 2+: 스킬당 다수 엔트리 누적 시 sid→entries 인덱스 캐시로 O(1) 화.
	std::vector<const SkillEffectEntry*> FindBySkill(int32 sid) const
	{
		std::vector<const SkillEffectEntry*> out;
		for (const auto& [k, e] : map_)
			if (e.sid == sid) out.push_back(&e);
		return out;
	}
};

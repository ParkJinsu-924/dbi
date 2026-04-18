#pragma once

#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "Effect.h"
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
	// OnLoaded 에서 sidIndex_ 를 구축하므로 O(1). 빈 결과도 안정적 빈 vector 반환.
	const std::vector<const SkillEffectEntry*>& FindBySkill(int32 sid) const
	{
		static const std::vector<const SkillEffectEntry*> kEmpty;
		auto it = sidIndex_.find(sid);
		return it != sidIndex_.end() ? it->second : kEmpty;
	}

	int OnValidate() const override
	{
		int errors = 0;
		const auto* skills  = GetResourceManager().Get<SkillTemplate>();
		const auto* effects = GetResourceManager().Get<Effect>();

		for (const auto& [k, se] : map_)
		{
			if (skills && !skills->Find(se.sid))
			{
				LOG_ERROR(std::format(
					"skill_effects: sid={} eid={} — referenced skill sid={} not found in skill_templates.csv",
					se.sid, se.eid, se.sid));
				++errors;
			}
			if (effects && !effects->Find(se.eid))
			{
				LOG_ERROR(std::format(
					"skill_effects: sid={} eid={} — referenced effect eid={} not found in effects.csv",
					se.sid, se.eid, se.eid));
				++errors;
			}
		}
		return errors;
	}

	const char* DebugName() const override { return "skill_effects"; }

protected:
	void OnLoaded() override
	{
		sidIndex_.clear();
		for (const auto& [k, e] : map_)
			sidIndex_[e.sid].push_back(&e);
	}

private:
	std::unordered_map<int32, std::vector<const SkillEffectEntry*>> sidIndex_;
};

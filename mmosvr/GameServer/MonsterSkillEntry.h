#pragma once

#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "SkillTemplate.h"
#include <vector>


// ─────────────────────────────────────────────────────────────────────
// MonsterSkillEntry — monster_skills.csv 의 한 행.
// "몬스터 tid 는 skillId 를 weight 가중으로, 최소 minInterval 간격으로 쓴다."
//
// SkillEffect 와 동형 패턴. 한 몬스터가 여러 스킬(평타/특수기/궁)을 가질 수 있고,
// AttackState 가 거리/쿨다운으로 후보를 필터링한 뒤 weight 로 가중 추첨.
//
// 실효 쿨다운 = max(SkillTemplate.cooldown, minInterval).
//   - SkillTemplate.cooldown : 스킬 자체의 재사용 대기 (연출/밸런스)
//   - minInterval            : AI 관점의 "이 스킬을 너무 자주 쓰지 마" 제약
// 평타 계열은 둘 다 짧게, 스페셜은 minInterval 을 크게 줘서 간헐 발동 유도.
// ─────────────────────────────────────────────────────────────────────


class MonsterSkillTable;


struct MonsterSkillEntry
{
	// 합성 키 (tid, skillId) → 단일 int64.
	using KeyType = int64;
	using Table   = MonsterSkillTable;

	int32 tid         = 0;
	int32 skillId     = 0;
	int32 weight      = 1;      // 상대 가중치 (양수)
	float minInterval = 0.0f;   // AI 관점의 최소 재사용 간격 (초)
	bool  is_basic    = false;  // true 면 이 스킬이 교전 기준 — cast_range 가 Chase→Attack 임계값.
	                            //         tid 당 정확히 1개만 true (OnValidate 강제).

	KeyType GetKey() const
	{
		return (static_cast<int64>(tid) << 32) | static_cast<uint32>(skillId);
	}

	CSV_DEFINE_TYPE(MonsterSkillEntry, tid, skillId, weight, minInterval, is_basic)
};


class MonsterSkillTable : public KeyedResourceTable<MonsterSkillEntry>
{
public:
	// 한 몬스터 tid 의 모든 스킬 엔트리. 없으면 빈 vector.
	const std::vector<const MonsterSkillEntry*>& FindByMonster(int32 tid) const
	{
		static const std::vector<const MonsterSkillEntry*> kEmpty;
		const auto it = tidIndex_.find(tid);
		return it != tidIndex_.end() ? it->second : kEmpty;
	}

	// 한 몬스터 tid 의 basic 엔트리. tid 당 1개 보장 (OnValidate). 없으면 nullptr.
	const MonsterSkillEntry* FindBasicByMonster(int32 tid) const
	{
		const auto it = basicIndex_.find(tid);
		return it != basicIndex_.end() ? it->second : nullptr;
	}

	int OnValidate() const override
	{
		int errors = 0;
		const auto* monsters = GetResourceManager().Get<MonsterTemplate>();
		const auto* skills   = GetResourceManager().Get<SkillTemplate>();

		// (1) 행별 무결성: FK + weight 양수.
		for (const auto& [k, e] : map_)
		{
			if (monsters && !monsters->Find(e.tid))
			{
				LOG_ERROR(std::format(
					"monster_skills: tid={} skillId={} — referenced monster tid not found in monster_templates.csv",
					e.tid, e.skillId));
				++errors;
			}
			if (skills && !skills->Find(e.skillId))
			{
				LOG_ERROR(std::format(
					"monster_skills: tid={} skillId={} — referenced skill sid not found in skill_templates.csv",
					e.tid, e.skillId));
				++errors;
			}
			if (e.weight <= 0)
			{
				LOG_ERROR(std::format(
					"monster_skills: tid={} skillId={} — weight must be positive (got {})",
					e.tid, e.skillId, e.weight));
				++errors;
			}
		}

		// (2) tid 당 정확히 1개의 is_basic=true 강제. monster_templates 의 모든 tid 가
		//     스킬 목록을 갖고 있다는 전제로 검사.
		if (monsters)
		{
			std::unordered_map<int32, int32> basicCount;
			for (const auto& [k, e] : map_)
			{
				if (e.is_basic) ++basicCount[e.tid];
				else            basicCount.try_emplace(e.tid, 0);
			}
			for (const auto& [tid, count] : basicCount)
			{
				if (count != 1)
				{
					LOG_ERROR(std::format(
						"monster_skills: tid={} — must have exactly 1 row with is_basic=true (got {})",
						tid, count));
					++errors;
				}
			}
		}
		return errors;
	}

	const char* DebugName() const override { return "monster_skills"; }

protected:
	void OnLoaded() override
	{
		tidIndex_.clear();
		basicIndex_.clear();
		for (const auto& [k, e] : map_)
		{
			tidIndex_[e.tid].push_back(&e);
			if (e.is_basic) basicIndex_[e.tid] = &e;
		}
	}

private:
	std::unordered_map<int32, std::vector<const MonsterSkillEntry*>> tidIndex_;
	std::unordered_map<int32, const MonsterSkillEntry*> basicIndex_;
};

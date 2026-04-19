#pragma once

#include "Utils/Types.h"
#include "AttackTypes.h"
#include <vector>

struct Effect;
class Unit;


// Unit 에 부착된 지속 Buff/Debuff 를 관리한다.
// Add : Effect 를 Buff 엔트리로 변환해 리스트에 추가하거나, 같은 eid 가 이미 있으면
//       duration 을 refresh (Phase 1 정책).
// Tick: dt 만큼 남은 시간을 감소. 만료된 엔트리는 제거하고 소유 Unit 의 Zone 으로
//       S_BuffRemoved 를 브로드캐스트한다.
// Remove: Dispel / 외부 해제. 제거되면 true 반환.
//
// BuffContainer 는 소유 Unit 과 1:1 로 묶이며, Unit::Update 에서 Tick 이 호출된다.
// Stat/CC 조회는 Unit::GetEffectiveXxx / IsStunned 등의 wrapper 를 통해 외부에 노출된다.
class BuffContainer
{
public:
	struct Entry
	{
		const Effect* effect;              // 원본 Effect (ResourceManager 가 소유)
		long long     casterGuid;
		float         remainingDuration;
	};

	explicit BuffContainer(Unit& owner) : owner_(&owner) {}

	BuffContainer(const BuffContainer&) = delete;
	BuffContainer& operator=(const BuffContainer&) = delete;

	// true = 새로 부착되었거나 기존 엔트리가 refresh 되었음 (호출측이 S_BuffApplied 방송).
	// false = duration 이 0 이하라 Buff 로 취급 안 됨.
	bool Add(const Effect& e, long long casterGuid);

	// dt 만큼 남은 시간 감소. 만료 엔트리는 제거 + S_BuffRemoved 방송.
	void Tick(float dt);

	// 외부 해제 (Dispel 등). true = 실제 제거됨.
	bool Remove(int32 eid);

	// 부착된 모든 Buff 의 cc_flag bitwise OR.
	uint32 GetCCFlags() const;

	// 특정 스탯에 대한 modifier 합산.
	// outFlat   : 평탄 보정 (양수/음수). 최종값 = base * (1 + outPercent) + outFlat
	// outPercent: 비율 보정 (0.3 = +30%, -0.3 = -30%). 중첩은 단순 합산.
	void GetStatModifier(StatType stat, float& outFlat, float& outPercent) const;

	const std::vector<Entry>& GetEntries() const { return entries_; }

private:
	Unit* owner_;
	std::vector<Entry> entries_;
};

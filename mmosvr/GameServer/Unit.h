#pragma once

#include "GameObject.h"
#include "Agent/IAgent.h"
#include "Agent/AgentRegistry.h"

#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

struct SkillTemplate;


// ===========================================================================
// Unit — Agent 컨테이너 + 기본 생존 상태(HP) + 이동 속도 소유.
// 기능별 로직(Buff / SkillCooldown / FSM / Aggro …) 은 Agent 에 위임.
// Get<T>() 는 실패하지 않는 계약: 없는 Agent 접근은 프로그래머 버그 → assert.
//
// 저장소: agents_ 는 AgentRegistry::IdOf<T>() 를 인덱스로 쓰는 sparse vector.
// Unit 타입에서 등록되지 않은 Agent 슬롯은 nullptr 로 남는다.
// ===========================================================================
class Unit : public GameObject
{
public:
	Unit(GameObjectType type, Zone& zone, std::string name = "");
	Unit(GameObjectType type, Zone& zone, long long guid, std::string name);

	// --- Agent API --------------------------------------------------------

	template<typename T, typename... Args>
	T& AddAgent(Args&&... args)
	{
		static_assert(std::is_base_of_v<IAgent, T>, "T must inherit IAgent");
		const int idx = AgentRegistry::IdOf<T>();
		if (idx >= static_cast<int>(agents_.size()))
			agents_.resize(idx + 1);
		auto p = std::make_unique<T>(*this, std::forward<Args>(args)...);
		T& ref = *p;
		tickOrder_.push_back(p.get());
		agents_[idx] = std::move(p);
		return ref;
	}

	template<typename T>
	T& Get()
	{
		const int idx = AgentRegistry::IdOf<T>();
		assert(idx < static_cast<int>(agents_.size()) && agents_[idx] && "Agent not registered on this Unit");
		return *static_cast<T*>(agents_[idx].get());
	}

	template<typename T>
	const T& Get() const
	{
		const int idx = AgentRegistry::IdOf<T>();
		assert(idx < static_cast<int>(agents_.size()) && agents_[idx] && "Agent not registered on this Unit");
		return *static_cast<const T*>(agents_[idx].get());
	}

	void Update(const float deltaTime) override;

	// --- HP / 생존 --------------------------------------------------------

	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }
	int32 GetMaxHp() const { return maxHp_; }
	void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
	bool IsAlive() const { return hp_ > 0; }

	// Invulnerable 버프면 데미지 무시. 실제 HP 변화가 있을 때만 S_UnitHp 방송.
	// attacker 가 Player 이고 자신이 Monster 면 actualDmg 만큼 aggro 자동 누적.
	void TakeDamage(int32 amount, const Unit* attacker = nullptr);
	// HP 변화가 있을 때만 S_UnitHp 방송.
	void Heal(int32 amount);

	// --- Move --------------------------------------------------------------

	float GetMoveSpeed() const { return moveSpeed_; }
	void  SetMoveSpeed(float v) { moveSpeed_ = v; }

	// target 으로 한 틱 이동. moveSpeed_ 에 Buff modifier 가 적용됨.
	// true  = 이 틱에 target 에 도달(snap 됨) 또는 이미 target 위.
	// false = 아직 이동 중 또는 CanMove() 가 false 라 이동 불가.
	bool MoveToward(const Proto::Vector2& target, float deltaTime);

	// --- Cast (wind-up + recovery) --------------------------------------------
	// SkillTemplate.cast_time>0 인 스킬 한정. 두 단계 흐름:
	//   0 ── resolveAt(=now+cast_time) ── endAt(=now+cast_time+recovery_time)
	//   │      wind-up      │       recovery       │
	//   │                   ↑                      ↑
	//   │             ResolveHit (S_SkillHit)   cooldown 시작 + pendingCast 해제
	// 두 단계 동안 IsCasting()==true 이며 EngageState 는 이동/추가 시전을 차단한다.
	// cancel(target 사망 wind-up 중 / caster 사망 / Stun) 시 즉시 cooldown MarkUsed.
	struct PendingCast
	{
		int32          skillId         = 0;
		long long      targetGuid      = 0;
		float          resolveAt       = 0.0f;   // 임팩트 시각 (now+cast_time)
		float          endAt           = 0.0f;   // 시전 완료 시각 (now+cast_time+recovery_time)
		float          appliedCooldown = 0.0f;   // endAt/cancel 시 MarkUsed(now + applied) 에 사용
		bool           resolved        = false;  // 임팩트 도달 후 true → ResolveHit 중복 방지
		Proto::Vector2 castPos;                  // 시전 시작 시점 caster 위치 스냅샷
	};

	bool IsCasting() const { return pendingCast_.has_value(); }

	// skill.cast_time>0 가정. S_SkillCastStart 브로드캐스트 + pendingCast_ 세팅.
	// appliedCooldown: cast 완료 또는 cancel 시점에 SkillCooldownAgent.MarkUsed 에 사용.
	// now 는 TimeManager.GetTotalTime() 기준 (호출부와 동일 clock).
	void BeginCast(const SkillTemplate& skill, const Unit& target, float now, float appliedCooldown);

	// pendingCast_ 가 있고 임팩트/완료 조건을 만족하면 ResolveHit / cooldown 시작.
	// Update 의 마지막 단계에서 자동 호출 — 외부에서 직접 부를 일은 없다.
	void TickCast();

	// 시전 중이면 S_SkillCastCancel 브로드캐스트 + 즉시 cooldown 시작 + pendingCast_.reset().
	// 아니면 no-op.
	void CancelCast();

protected:
	int32 hp_ = 100;
	int32 maxHp_ = 100;
	float moveSpeed_ = 3.0f;   // 파생 ctor 또는 스폰 시점(템플릿) 에서 덮어쓴다.

	std::optional<PendingCast> pendingCast_;

private:
	// AgentRegistry::IdOf<T>() 를 인덱스로 사용. 미등록 Agent 슬롯은 nullptr.
	std::vector<std::unique_ptr<IAgent>> agents_;
	// Tick 순서 보존용 raw pointer 벡터 (소유권은 agents_ 에).
	std::vector<IAgent*> tickOrder_;
};

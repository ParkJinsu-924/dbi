#include "pch.h"
#include "Unit.h"

#include "Agent/BuffAgent.h"
#include "Agent/SkillCooldownAgent.h"
#include "Agent/AggroAgent.h"
#include "Agent/ForcedMoveAgent.h"
#include "SkillExecution.h"
#include "SkillTemplate.h"
#include "Utils/MathUtil.h"
#include "Utils/TimeManager.h"
#include "Zone.h"
#include "PacketMaker.h"


Unit::Unit(GameObjectType type, Zone& zone, std::string name)
	: Unit(type, zone, GetObjectGuidGenerator().Generate(), std::move(name))
{
}

Unit::Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
	: GameObject(type, zone, guid, std::move(name))
{
	AddAgent<BuffAgent>();
	AddAgent<SkillCooldownAgent>();
	AddAgent<ForcedMoveAgent>();   // 강제 이동(돌진/넉백 등) 수신 채널. 첫 사용처 도입 전엔 inactive.
}

void Unit::Update(const float deltaTime)
{
	for (auto* a : tickOrder_)
		a->Tick(deltaTime);

	// Cast 진행은 Agent Tick 후에 평가 — BuffAgent 가 이번 틱에 부착한 stun 의 캔슬 결과가
	// 즉시 반영되도록(부착 시점에 CancelCast 호출이 따로 있긴 하지만 안전망 역할).
	TickCast();
}

void Unit::TakeDamage(int32 amount, const Unit* attacker)
{
	if (Get<BuffAgent>().CanIgnoreDamage()) return;

	const int32 hpBefore = hp_;
	hp_ = (std::max)(0, hp_ - amount);
	const int32 actualDmg = hpBefore - hp_;
	if (actualDmg <= 0) return;

	GetZone().Broadcast(PacketMaker::MakeUnitHp(*this));

	// Player 가 Monster 공격 시 실제 적용 피해량만큼 aggro 자동 누적.
	if (attacker &&
		attacker->GetType() == GameObjectType::Player &&
		GetType() == GameObjectType::Monster)
	{
		Get<AggroAgent>().Add(attacker->GetGuid(), static_cast<float>(actualDmg));
	}

	// 사망 시 진행 중 시전 캔슬 — Update 순회 안이라 다음 TickCast 가 돌기 전에 정리해 둔다.
	if (hp_ == 0)
		CancelCast();
}

void Unit::Heal(int32 amount)
{
	const int32 hpBefore = hp_;
	hp_ = (std::min)(maxHp_, hp_ + amount);
	if (hp_ != hpBefore)
		GetZone().Broadcast(PacketMaker::MakeUnitHp(*this));
}

bool Unit::MoveToward(const Proto::Vector2& target, float deltaTime)
{
	if (!Get<BuffAgent>().CanMove()) return false;

	const float dx = target.x() - position_.x();
	const float dz = target.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);
	if (dist < 0.001f) return true;

	const float step = Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step >= dist)
	{
		position_.set_x(target.x());
		position_.set_y(target.y());
		return true;
	}
	position_.set_x(position_.x() + (dx / dist) * step);
	position_.set_y(position_.y() + (dz / dist) * step);
	return false;
}

void Unit::BeginCast(const SkillTemplate& skill, const Unit& target, const float now, const float appliedCooldown)
{
	const float castEndTime = skill.cast_time + skill.recovery_time;
	pendingCast_ = PendingCast{
		skill.sid,
		target.GetGuid(),
		now + skill.cast_time,
		now + castEndTime,
		appliedCooldown,
		false,
		position_,
	};
	GetZone().Broadcast(PacketMaker::MakeSkillCastStart(
		guid_, target.GetGuid(), skill.sid, skill.cast_time, castEndTime, position_));
}

void Unit::TickCast()
{
	if (!pendingCast_) return;

	const float now = GetTimeManager().GetTotalTime();

	// 1) wind-up 단계 — 아직 임팩트 전이면 target 살아있는지 확인.
	//    target 사망/소실 시 wind-up 캔슬 (데미지 미적용).
	//    recovery 단계 (resolved=true) 진입 후엔 데미지가 이미 적용됐으므로
	//    target 가 죽어도 follow-through 는 그대로 진행한다.
	if (!pendingCast_->resolved)
	{
		auto target = std::dynamic_pointer_cast<Unit>(GetZone().Find(pendingCast_->targetGuid));
		if (!target || !target->IsAlive())
		{
			CancelCast();
			return;
		}

		if (now >= pendingCast_->resolveAt)
		{
			// 임팩트 — ResolveHit 가 부수효과로 caster/target 상태를 건드릴 수 있으니
			// 먼저 resolved=true 로 마킹해 재진입 가드.
			pendingCast_->resolved = true;
			SkillExecution::ResolveHit(this, *target, pendingCast_->skillId,
			                           pendingCast_->castPos, target->GetPosition(), GetZone());
			// ResolveHit 안에서 caster 사망 등으로 pendingCast_.reset() 이 일어났을 수 있음.
			if (!pendingCast_) return;
		}
	}

	// 2) recovery 종료 — cooldown 시작 + pendingCast 해제.
	if (now >= pendingCast_->endAt)
	{
		Get<SkillCooldownAgent>().MarkUsed(pendingCast_->skillId, now + pendingCast_->appliedCooldown);
		pendingCast_.reset();
	}
}

void Unit::CancelCast()
{
	if (!pendingCast_) return;
	const int32 skillId = pendingCast_->skillId;
	const float appliedCooldown = pendingCast_->appliedCooldown;
	pendingCast_.reset();
	// 캔슬도 cooldown 패널티 — 무한 캔슬-재시전 방지. wind-up/recovery 어느 단계에서 끊기든 동일.
	Get<SkillCooldownAgent>().MarkUsed(skillId, GetTimeManager().GetTotalTime() + appliedCooldown);
	GetZone().Broadcast(PacketMaker::MakeSkillCastCancel(guid_, skillId));
}

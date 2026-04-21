#include "pch.h"
#include "Agent/FSMAgent.h"
#include "Agent/BuffAgent.h"
#include "Unit.h"
#include <cassert>


FSMAgent::FSMAgent(Unit& owner)
	: IAgent(owner)
{
	// 계약 assert: FSMAgent 는 Monster 에만 등록된다.
	assert(owner.GetType() == GameObjectType::Monster);
}

void FSMAgent::Tick(const float deltaTime)
{
	// 총체적 행동 불가(Stun) 면 스킵. BuffAgent 는 이 Agent 보다 먼저 Tick 됨이 보장된다
	// (Unit ctor 에서 AddAgent<BuffAgent>() 먼저 호출됨).
	if (!owner_.Get<BuffAgent>().CanAct())
		return;
	fsm_.Update(deltaTime);
}

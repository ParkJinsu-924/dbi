#include "pch.h"
#include "Agent/FSMAgent.h"
#include "Agent/BuffAgent.h"
#include "Unit.h"
#include "Monster.h"
#include "Zone.h"
#include "PacketMaker.h"
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

void FSMAgent::Init()
{
	// FSMAgent ctor assert 가 Monster 소유를 보장 — static_cast 안전.
	Monster& monster = static_cast<Monster&>(owner_);

	// GlobalState: Idle/Patrol 에서 매 틱 플레이어 감지.
	fsm_.SetGlobalState<MonsterGlobalState>();

	fsm_.AddState<IdleState>(MonsterStateId::Idle);
	fsm_.AddState<PatrolState>(MonsterStateId::Patrol);
	fsm_.AddState<EngageState>(MonsterStateId::Engage);
	fsm_.AddState<ReturnState>(MonsterStateId::Return);

	// 상태 전환 시 클라에 브로드캐스트.
	fsm_.SetOnStateChanged([&monster](MonsterStateId /*prev*/, MonsterStateId next)
		{
			monster.GetZone().Broadcast(PacketMaker::MakeMonsterState(monster, next));
		});

	fsm_.Start(monster, MonsterStateId::Idle);
}

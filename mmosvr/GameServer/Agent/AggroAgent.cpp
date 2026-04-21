#include "pch.h"
#include "Agent/AggroAgent.h"
#include "Unit.h"
#include <cassert>


AggroAgent::AggroAgent(Unit& owner)
	: IAgent(owner)
{
	assert(owner.GetType() == GameObjectType::Monster);
}

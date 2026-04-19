#pragma once

#include "Unit.h"


class Npc : public Unit
{
public:
	Npc(std::string name, Zone& zone)
		: Unit(GameObjectType::Npc, zone, std::move(name))
	{
	}

protected:
	Npc(GameObjectType type, Zone& zone, std::string name)
		: Unit(type, zone, std::move(name))
	{
	}
};

#pragma once

#include "Unit.h"


class Npc : public Unit
{
public:
	explicit Npc(std::string name)
		: Unit(GameObjectType::Npc, std::move(name))
	{
	}

protected:
	Npc(GameObjectType type, std::string name)
		: Unit(type, std::move(name))
	{
	}
};

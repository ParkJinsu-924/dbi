#pragma once

#include "Unit.h"


class Npc : public Unit
{
protected:
	Npc(GameObjectType type, Zone& zone)
		: Unit(type, zone)
	{
	}
};

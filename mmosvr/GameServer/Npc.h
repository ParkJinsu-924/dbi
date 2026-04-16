#pragma once

#include "GameObject.h"


class Npc : public GameObject
{
public:
	explicit Npc(std::string name)
		: GameObject(GameObjectType::Npc, std::move(name))
	{
	}
};

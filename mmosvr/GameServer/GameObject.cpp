#include "pch.h"
#include "GameObject.h"
#include "Zone.h"


int32 GameObject::GetZoneId() const
{
	return zone_ ? zone_->GetId() : 0;
}

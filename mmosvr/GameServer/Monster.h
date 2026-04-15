#pragma once

#include "common.pb.h"
#include "Utils/Types.h"
#include "Utils/ObjectGuidGenerator.h"


class Monster
{
public:
	Monster()
		: guid_(GetObjectGuidGenerator().Generate())
	{
	}

	long long GetGuid() const { return guid_; }
	const Proto::Vector3& GetPosition() const { return position_; }
	void SetPosition(const Proto::Vector3& p) { position_ = p; }
	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }

private:
	const long long guid_;
	Proto::Vector3 position_;
	int32 hp_ = 100;
};

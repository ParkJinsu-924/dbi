#pragma once

#include "common.pb.h"
#include "Utils/Types.h"
#include "Utils/ObjectGuidGenerator.h"
#include <string>


class Npc
{
public:
	explicit Npc(std::string name)
		: guid_(GetObjectGuidGenerator().Generate())
		, name_(std::move(name))
	{
	}

	long long GetGuid() const { return guid_; }
	const std::string& GetName() const { return name_; }
	const Proto::Vector3& GetPosition() const { return position_; }
	void SetPosition(const Proto::Vector3& p) { position_ = p; }

private:
	const long long guid_;
	const std::string name_;
	Proto::Vector3 position_;
};

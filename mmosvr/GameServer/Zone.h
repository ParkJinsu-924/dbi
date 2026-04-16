#pragma once

#include "Utils/Synchronized.h"
#include "Utils/Types.h"
#include "Network/SendBuffer.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>

class GameObject;


class Zone
{
public:
	explicit Zone(int32 id) : id_(id) {}

	int32 GetId() const { return id_; }

	// Unified API for all GameObject types
	void Add(std::shared_ptr<GameObject> obj);
	void Remove(long long guid);
	std::shared_ptr<GameObject> Find(long long guid) const;

	template<typename T>
	std::shared_ptr<T> FindAs(long long guid) const
	{
		return std::dynamic_pointer_cast<T>(Find(guid));
	}

	template<typename T>
	std::vector<std::shared_ptr<T>> GetObjectsByType() const
	{
		return objects_.Read([](const auto& m)
			{
				std::vector<std::shared_ptr<T>> result;
				for (const auto& [guid, obj] : m)
					if (auto casted = std::dynamic_pointer_cast<T>(obj))
						result.push_back(std::move(casted));
				return result;
			});
	}

	// Tick all objects + broadcast monster positions periodically
	void Update(float deltaTime);

	// Broadcast to Players currently in this zone
	void Broadcast(SendBufferChunkPtr chunk);

private:
	void BroadcastMonsterPositions();

	const int32 id_;
	Synchronized<std::unordered_map<long long, std::shared_ptr<GameObject>>, std::shared_mutex> objects_;
	float monsterBroadcastAccum_ = 0.0f;
};

#pragma once

#include "Utils/Synchronized.h"
#include "Utils/Types.h"
#include "Network/SendBuffer.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>

class Player;
class Monster;
class Npc;


class Zone
{
public:
	explicit Zone(int32 id) : id_(id) {}

	int32 GetId() const { return id_; }

	// Players
	void AddPlayer(std::shared_ptr<Player> player);
	void RemovePlayer(int32 playerId);
	std::shared_ptr<Player> FindPlayer(int32 playerId) const;

	// Monsters
	void AddMonster(std::shared_ptr<Monster> monster);
	void RemoveMonster(long long guid);

	// Npcs
	void AddNpc(std::shared_ptr<Npc> npc);
	void RemoveNpc(long long guid);

	// Broadcast to all players currently in this zone
	void Broadcast(SendBufferChunkPtr chunk);

private:
	const int32 id_;
	Synchronized<std::unordered_map<int32, std::shared_ptr<Player>>, std::shared_mutex> players_;
	Synchronized<std::unordered_map<long long, std::shared_ptr<Monster>>, std::shared_mutex> monsters_;
	Synchronized<std::unordered_map<long long, std::shared_ptr<Npc>>, std::shared_mutex> npcs_;
};

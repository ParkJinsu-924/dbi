#include "pch.h"
#include "Zone.h"
#include "Player.h"
#include "Monster.h"
#include "Npc.h"
#include "Network/Session.h"


void Zone::AddPlayer(std::shared_ptr<Player> player)
{
	int32 id = player->GetPlayerId();
	players_.Write([&](auto& m)
		{
			m[id] = std::move(player);
		});
}

void Zone::RemovePlayer(int32 playerId)
{
	players_.Write([&](auto& m)
		{
			m.erase(playerId);
		});
}

std::shared_ptr<Player> Zone::FindPlayer(int32 playerId) const
{
	return players_.Read([&](const auto& m) -> std::shared_ptr<Player>
		{
			auto it = m.find(playerId);
			if (it == m.end())
				return nullptr;
			return it->second;
		});
}

void Zone::AddMonster(std::shared_ptr<Monster> monster)
{
	long long guid = monster->GetGuid();
	monsters_.Write([&](auto& m)
		{
			m[guid] = std::move(monster);
		});
}

void Zone::RemoveMonster(long long guid)
{
	monsters_.Write([&](auto& m)
		{
			m.erase(guid);
		});
}

void Zone::AddNpc(std::shared_ptr<Npc> npc)
{
	long long guid = npc->GetGuid();
	npcs_.Write([&](auto& m)
		{
			m[guid] = std::move(npc);
		});
}

void Zone::RemoveNpc(long long guid)
{
	npcs_.Write([&](auto& m)
		{
			m.erase(guid);
		});
}

void Zone::Broadcast(SendBufferChunkPtr chunk)
{
	players_.Read([&](const auto& m)
		{
			for (const auto& [id, player] : m)
			{
				if (auto session = player->GetSession())
				{
					if (session->IsConnected())
						std::static_pointer_cast<Session>(session)->Send(chunk);
				}
			}
		});
}

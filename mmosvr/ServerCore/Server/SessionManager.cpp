#include "pch.h"
#include "Server/SessionManager.h"
#include "Network/ServerSession.h"


void SessionManager::AddGameSession(SessionPtr session)
{
	sessions_.Write([&](auto& set)
		{
			set.insert(std::move(session));
		});
}

void SessionManager::RemoveGameSession(SessionPtr session)
{
	sessions_.Write([&](auto& set)
		{
			set.erase(session);
		});
}

void SessionManager::BroadcastToGameSessions(SendBufferChunkPtr chunk)
{
	sessions_.Read([&](const auto& set)
		{
			for (auto& session : set)
			{
				if (session->IsConnected())
				{
					session->Send(chunk);
				}
			}
		});
}

int32 SessionManager::GetGameSessionsCount() const
{
	return sessions_.Read([](const auto& set)
		{
			return static_cast<int32>(set.size());
		});
}

void SessionManager::ClearGameSessions()
{
	sessions_.Write([](auto& set)
		{
			for (auto& session : set)
			{
				session->Disconnect();
			}
			set.clear();
		});
}

void SessionManager::SetServerSession(ServerType type, std::shared_ptr<ServerSession> session)
{
	serverSession_.WithLock([&](auto& set)
		{
			set[type] = std::move(session);
		});
}

void SessionManager::ClearServerSession(ServerType type)
{
	serverSession_.WithLock([&](auto& set)
		{
			set.erase(type);
		});
}

std::shared_ptr<ServerSession> SessionManager::GetServerSession(ServerType type) const
{
	return serverSession_.WithLock([&](auto& set) -> std::shared_ptr<ServerSession>
		{
			auto it = set.find(type);
			if (it == set.end())
				return nullptr;
			return it->second.lock();
		});
}

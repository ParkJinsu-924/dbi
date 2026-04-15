#pragma once

#include "Network/Session.h"
#include "Network/SendBuffer.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"

class ServerSession;

enum class ServerType
{
	LoginServer,
	// ChatServer,
	// WorldServer,
};

template<>
struct std::hash<ServerType>
{
	size_t operator()(ServerType type) const noexcept
	{
		return std::hash<int>{}(static_cast<int>(type));
	}
};

#define GetSessionManager() SessionManager::Instance()

class SessionManager : public TSingleton<SessionManager>
{
public:
	// Client sessions
	void AddGameSession(SessionPtr session);
	void RemoveGameSession(SessionPtr session);
	void BroadcastToGameSessions(SendBufferChunkPtr chunk);
	int32 GetGameSessionsCount() const;
	void ClearGameSessions();

	// Server-to-server sessions
	void SetServerSession(ServerType type, std::shared_ptr<ServerSession> session);
	void ClearServerSession(ServerType type);
	std::shared_ptr<ServerSession> GetServerSession(ServerType type) const;

private:
	Synchronized<std::set<SessionPtr>, std::shared_mutex> sessions_;
	Synchronized<std::unordered_map<ServerType, std::weak_ptr<ServerSession>>, std::mutex> serverSession_;
};

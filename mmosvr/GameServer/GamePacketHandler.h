#pragma once

#include "GameSession.h"
#include "game.pb.h"
#include "server.pb.h"
#include "Network/ServerSession.h"
#include "Server/SessionManager.h"
#include "Services/PlayerService.h"
#include "Services/MapService.h"
#include "Utils/Synchronized.h"


class GamePacketHandler
{
public:
	static void Init(SessionManager& sessionManager, PlayerService& playerService, MapService& mapService);

	static void SetLoginServerSession(std::shared_ptr<ServerSession> session);

	static Proto::ErrorCode C_EnterGame(std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt);
	static Proto::ErrorCode C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt);
	static Proto::ErrorCode C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt);
	static Proto::ErrorCode SS_ValidateTokenResult(std::shared_ptr<ServerSession> session, const Proto::SS_ValidateTokenResult& pkt);

	static SessionManager* sSessionManager;
	static PlayerService* sPlayerService;
	static MapService* sMapService;
	static std::weak_ptr<ServerSession> sLoginServerSession;
	static Synchronized<std::unordered_map<std::string, std::weak_ptr<GameSession>>, std::mutex> sPendingValidations;
};

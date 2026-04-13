#pragma once

#include "GameSession.h"
#include "game.pb.h"
#include "server.pb.h"
#include "Network/ServerSession.h"
#include "Server/SessionManager.h"
#include "Services/PlayerService.h"
#include "Services/MapService.h"


class GamePacketHandler
{
public:
	static void Init(SessionManager& sessionManager, PlayerService& playerService, MapService& mapService);

	static void SetLoginSession(std::shared_ptr<ServerSession> session);

	static void C_EnterGame(std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt);
	static void C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt);
	static void C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt);
	static void SS_ValidateTokenResult(std::shared_ptr<ServerSession> session, const Proto::SS_ValidateTokenResult& pkt);

	static SessionManager* sSessionManager;
	static PlayerService* sPlayerService;
	static MapService* sMapService;
	static std::weak_ptr<ServerSession> sLoginSession;
	static std::mutex sPendingMutex;
	static std::unordered_map<std::string, std::weak_ptr<GameSession>> sPendingValidations;
};

#pragma once

#include "GameSession.h"
#include "game.pb.h"
#include "server.pb.h"
#include "Network/ServerSession.h"
#include "Utils/Synchronized.h"



class Player;
class GamePacketHandler
{
public:
	static Proto::ErrorCode C_EnterGame(std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt);
	static Proto::ErrorCode C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt);
	static Proto::ErrorCode C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt);
	static Proto::ErrorCode SS_ValidateToken(std::shared_ptr<ServerSession> session, const Proto::SS_ValidateTokenResult& pkt);

	static Synchronized<std::unordered_map<std::string, std::weak_ptr<GameSession>>, std::mutex> sPendingValidations;
};

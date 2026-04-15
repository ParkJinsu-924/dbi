#pragma once

#include "LoginSession.h"
#include "login.pb.h"
#include "server.pb.h"
#include "common.pb.h"


class LoginPacketHandler
{
public:
	static Proto::ErrorCode C_Login(std::shared_ptr<LoginSession> session, const Proto::C_Login& pkt);
	static Proto::ErrorCode SS_ValidateToken(std::shared_ptr<LoginSession> session, const Proto::SS_ValidateToken& pkt);
};

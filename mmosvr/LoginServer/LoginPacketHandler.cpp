#include "pch.h"
#include "LoginPacketHandler.h"


TokenStore* LoginPacketHandler::sTokenStore = nullptr;

void LoginPacketHandler::Init(TokenStore& tokenStore)
{
	sTokenStore = &tokenStore;

	auto& handler = PacketHandler::Instance();
	handler.Register<Proto::C_Login, LoginSession>(&HandleLogin);
	handler.Register<Proto::SS_ValidateToken, LoginSession>(&HandleValidateToken);
}

Proto::ErrorCode LoginPacketHandler::HandleLogin(
	std::shared_ptr<LoginSession> session, const Proto::C_Login& pkt)
{
	LOG_INFO("Login attempt: username=" + pkt.username());

	if (pkt.username().empty() || pkt.password().empty())
		return Proto::ErrorCode::INVALID_REQUEST;

	const std::string token = "token_" + pkt.username();
	sTokenStore->Store(token, pkt.username());

	Proto::S_Login response;
	response.set_token(token);
	response.set_game_server_ip("127.0.0.1");
	response.set_game_server_port(7777);
	session->Send(response);

	LOG_INFO("Token issued: " + token + " for " + pkt.username());
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode LoginPacketHandler::HandleValidateToken(
	std::shared_ptr<LoginSession> session, const Proto::SS_ValidateToken& pkt)
{
	LOG_INFO("Token validation request: token=" + pkt.token());

	Proto::SS_ValidateTokenResult result;
	result.set_token(pkt.token());

	std::string username;
	if (sTokenStore->Validate(pkt.token(), username))
	{
		result.set_valid(true);
		result.set_username(username);
		LOG_INFO("Token valid: " + pkt.token() + " -> " + username);
	}
	else
	{
		result.set_valid(false);
		LOG_INFO("Token invalid: " + pkt.token());
	}

	session->Send(result);
	return Proto::ErrorCode::OK;
}

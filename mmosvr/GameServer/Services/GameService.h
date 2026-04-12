#pragma once


class GameService
{
public:
	virtual ~GameService() = default;
	virtual void Init() = 0;
	virtual void Update(float deltaTime) = 0;
	virtual void Shutdown() = 0;
};

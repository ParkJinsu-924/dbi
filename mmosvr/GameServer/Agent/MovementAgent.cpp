#include "pch.h"
#include "Agent/MovementAgent.h"
#include "Unit.h"


void MovementAgent::Tick(const float dt)
{
	if (!active_)
		return;

	if (owner_.MoveToward(destination_, dt))
		active_ = false;
}

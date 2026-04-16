#include "pch.h"
#include "Monster.h"
#include <cmath>


void Monster::InitCircularMovement(const Proto::Vector3& center,
	float radius, float angularSpeedRad, float startAngleRad)
{
	center_ = center;
	radius_ = radius;
	angularSpeed_ = angularSpeedRad;
	angle_ = startAngleRad;

	// Seed initial position on the circle
	position_.set_x(center.x() + radius * std::cos(angle_));
	position_.set_y(center.y());
	position_.set_z(center.z() + radius * std::sin(angle_));
}

void Monster::Update(float deltaTime)
{
	if (radius_ <= 0.0f)
		return;

	angle_ += angularSpeed_ * deltaTime;

	// Keep angle in a reasonable range
	const float twoPi = 6.28318530718f;
	if (angle_ > twoPi) angle_ -= twoPi;
	if (angle_ < -twoPi) angle_ += twoPi;

	position_.set_x(center_.x() + radius_ * std::cos(angle_));
	position_.set_z(center_.z() + radius_ * std::sin(angle_));
	// y stays the same (ground plane)
}

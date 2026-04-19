#pragma once

#include "common.pb.h"
#include <cmath>


// MMO 좌표/거리 계산 공용 유틸. ServerCore 에 있어 Game/Login/향후 서버 모두 공유.
// 헤더 전용 inline — .cpp 불필요.
//
// 본 프로젝트는 전 좌표계가 2D (Proto::Vector2 { x, y }) 로 통일돼 있다.
// "Sq" 접미사 = 제곱 거리. 범위 비교/최근접 탐색은 sqrt 를 피하기 위해 항상 Sq 사용.
namespace MathUtil
{
	// 정규화 방향 벡터 반환용.
	struct Dir2D
	{
		float x;
		float y;
	};

	inline float Distance2D(const Proto::Vector2& a, const Proto::Vector2& b)
	{
		const float dx = a.x() - b.x();
		const float dy = a.y() - b.y();
		return std::sqrt(dx * dx + dy * dy);
	}

	inline float Distance2DSq(const Proto::Vector2& a, const Proto::Vector2& b)
	{
		const float dx = a.x() - b.x();
		const float dy = a.y() - b.y();
		return dx * dx + dy * dy;
	}

	// from→to 정규화 방향. 두 점이 거의 같으면 (1,0) fallback.
	inline Dir2D NormalizedDir2D(const Proto::Vector2& from, const Proto::Vector2& to)
	{
		const float dx = to.x() - from.x();
		const float dy = to.y() - from.y();
		const float len = std::sqrt(dx * dx + dy * dy);
		if (len < 1e-4f)
			return { 1.0f, 0.0f };
		return { dx / len, dy / len };
	}

	// (dx, dy) 벡터 길이. 이미 분리 계산된 차분의 크기를 얻을 때.
	inline float Length2D(float dx, float dy)
	{
		return std::sqrt(dx * dx + dy * dy);
	}

	// (dx, dy) 제곱 길이. 범위 비교용.
	inline float LengthSq2D(float dx, float dy)
	{
		return dx * dx + dy * dy;
	}
}

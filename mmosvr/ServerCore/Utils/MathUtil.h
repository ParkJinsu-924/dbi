#pragma once

#include "common.pb.h"
#include <cmath>
#include <optional>


// MMO 좌표/거리 계산 공용 유틸. ServerCore 에 있어 Game/Login/향후 서버 모두 공유.
// 헤더 전용 inline — .cpp 불필요.
//
// 본 프로젝트는 전 좌표계가 2D (Proto::Vector2 { x, y }) 로 통일돼 있다.
// "Sq" 접미사 = 제곱 거리. 범위 비교/최근접 탐색은 sqrt 를 피하기 위해 항상 Sq 사용.
namespace MathUtil
{
	// 2D 벡터 "사실상 영벡터" 판정 임계값. 정규화 0-division 방지와
	// 두 좌표가 같은 지점인지 판정하는 용도로 공용.
	constexpr float kVectorLengthEpsilon = 1e-4f;

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

	// from→to 정규화 방향. 두 점이 거의 같으면 (1,0) fallback.
	// "방향이 없으면 기본값으로 진행" 의미를 가진 곳에서 사용.
	inline Dir2D NormalizedDir2D(const Proto::Vector2& from, const Proto::Vector2& to)
	{
		const float dx = to.x() - from.x();
		const float dy = to.y() - from.y();
		const float len = Length2D(dx, dy);
		if (len < kVectorLengthEpsilon)
			return { 1.0f, 0.0f };
		return { dx / len, dy / len };
	}

	// (x, y) 벡터 정규화 시도. 길이가 kVectorLengthEpsilon 미만이면 nullopt.
	// "방향이 없으면 호출자가 거절/분기" 의미를 가진 곳에서 사용.
	inline std::optional<Dir2D> TryNormalize2D(float x, float y)
	{
		const float len = Length2D(x, y);
		if (len < kVectorLengthEpsilon)
			return std::nullopt;
		return Dir2D{ x / len, y / len };
	}
}

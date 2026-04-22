#pragma once

// ===========================================================================
// GameConstants — 게임플레이 튜닝 상수 중앙화.
//
// 각 .cpp 에 흩어져 있던 매직 넘버가 새로 생기거나 공유가 필요해질 때 이곳에 모은다.
// 향후 CSV/Config 파일로 이동할 수도 있는 후보들의 중간 기지. 순수 컴파일 타임 상수만 둔다
// (런타임에 조정할 값은 별도 Config 시스템 또는 CSV 사용).
//
// 도메인별 서브 네임스페이스(Zone, Monster, ...) 로 구획화. 도메인이 자명한 값은
// 서브 네임스페이스 밑에 두고, 도메인이 애매하거나 cross-cutting 한 값만 top-level 유지.
// ===========================================================================

namespace GameConfig
{
	// ─── Skill / Cast ─────────────────────────────────────────────────────
	// 서버측 cast_range 검증 관용 margin.
	// 클라-서버 위치 예측의 RTT 지연으로 경계부 시전이 miss 처리되는 것을 방지.
	// 이 값만큼의 여유를 허용 (실제 사거리 = CSV cast_range + 이 값).
	// 0.5 유닛 기준: RTT 100ms + 5 unit/sec 이속의 race 를 커버.
	constexpr float CAST_RANGE_TOLERANCE = 0.5f;


	namespace Zone
	{
		// ─── Broadcast Tick ─────────────────────────────────────────────
		// Zone::Update 가 축적한 delta 가 이 값을 넘을 때마다 위치 방송.
		// 10 Hz = 네트워크 대역과 클라 보간 품질의 타협점.
		constexpr float MONSTER_BROADCAST_INTERVAL_SEC = 0.1f;

		// 클릭 이동 중인 플레이어의 위치만 방송하는 주기.
		constexpr float PLAYER_BROADCAST_INTERVAL_SEC  = 0.1f;
	}


	namespace Monster
	{
		// ─── Monster Default Stats ──────────────────────────────────────
		// MonsterTemplate(CSV) 미지정 / 로드 실패 시 fallback 으로 사용.
		// CSV 로 오버라이드 되는 것이 정상 경로.
		constexpr float DEFAULT_MOVE_SPEED   = 3.0f;
		constexpr float DEFAULT_DETECT_RANGE = 10.0f;
		constexpr float DEFAULT_LEASH_RANGE  = 15.0f;
	}
}

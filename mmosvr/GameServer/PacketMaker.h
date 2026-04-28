#pragma once

#include "game.pb.h"
#include "MonsterStates.h"   // MonsterStateId
#include <memory>
#include <vector>
#include <string>

class Monster;
class Player;
class Unit;
class Projectile;
class HomingProjectile;
class SkillshotProjectile;
struct Effect;


// 서버에서 클라이언트로 보내는 모든 S_* 패킷을 생성하는 헬퍼.
// 호출부는 `zone->Broadcast(PacketMaker::MakeXxx(...))` 한 줄로 끝난다.
// 필드 추가/수정 시 여기 한 곳만 고치면 모든 호출부가 자동으로 따라온다.
//
// 규칙:
//  - 도메인 오브젝트가 모든 필드를 제공할 수 있으면 해당 오브젝트 레퍼런스 하나를 받는다.
//  - 도메인 바깥 값(채팅 메시지, 스폰 위치 등)은 추가 파라미터로 받는다.
class PacketMaker
{
public:
	// --- Player ---
	static Proto::S_EnterGame      MakeEnterGame(const Player& player, const Proto::Vector2& spawnPos);
	static Proto::S_PlayerSpawn    MakePlayerSpawn(const Player& player);
	static Proto::S_PlayerList     MakePlayerList(const std::vector<std::shared_ptr<Player>>& players);
	static Proto::S_PlayerMove     MakePlayerMove(const Player& player);
	static Proto::S_PlayerLeave    MakePlayerLeave(int32 playerId);
	static Proto::S_Chat           MakeChat(const Player& sender, const std::string& message);
	static Proto::S_MoveCorrection MakeMoveCorrection(const Proto::Vector2& position);

	// --- Monster ---
	static Proto::S_MonsterSpawn   MakeMonsterSpawn(const Monster& monster);
	static Proto::S_MonsterList    MakeMonsterList(const std::vector<std::shared_ptr<Monster>>& monsters);
	static Proto::S_MonsterMove    MakeMonsterMove(const Monster& monster);
	static Proto::S_MonsterDespawn MakeMonsterDespawn(long long guid);
	static Proto::S_MonsterState   MakeMonsterState(const Monster& monster, MonsterStateId nextState);

	// --- 통합 공격 적중 (Melee/Hitscan/Homing/Skillshot 모두) ---
	static Proto::S_SkillHit MakeSkillHit(long long casterGuid, long long targetGuid,
	                                      int32 skillId, int32 damage,
	                                      const Proto::Vector2& casterPos,
	                                      const Proto::Vector2& hitPos);

	// --- 시전 시작/캔슬 (cast_time>0 인 스킬 한정) ---
	// targetGuid=0 은 Skillshot/AoE 처럼 명시 타겟이 없는 경우.
	// castTime: 임팩트 시점 (S_SkillHit 도착 예정). castEndTime: 시전 완료 시점.
	static Proto::S_SkillCastStart  MakeSkillCastStart(long long casterGuid, long long targetGuid,
	                                                   int32 skillId, float castTime, float castEndTime,
	                                                   const Proto::Vector2& casterPos);
	static Proto::S_SkillCastCancel MakeSkillCastCancel(long long casterGuid, int32 skillId);

	// --- Unit (generic, Player/Monster/Npc 공통) ---
	static Proto::S_UnitHp MakeUnitHp(const Unit& unit);

	// --- Buff / Effect ---
	static Proto::S_BuffApplied MakeBuffApplied(long long targetGuid, const Effect& e, long long casterGuid);
	static Proto::S_BuffRemoved MakeBuffRemoved(long long targetGuid, int32 eid);

	// --- Projectile ---
	static Proto::S_ProjectileSpawn   MakeHomingProjectileSpawn(const HomingProjectile& projectile);
	static Proto::S_ProjectileSpawn   MakeSkillshotProjectileSpawn(const SkillshotProjectile& projectile);
	static Proto::S_ProjectileDestroy MakeProjectileDestroy(long long projectileGuid, Proto::S_ProjectileDestroy_Reason reason);

private:
	// 내부 DTO 빌더 — S_*Spawn / S_*List 구현에서 공용으로 사용.
	static Proto::MonsterInfo MakeMonsterInfo(const Monster& monster);
	static Proto::PlayerInfo  MakePlayerInfo(const Player& player);
};

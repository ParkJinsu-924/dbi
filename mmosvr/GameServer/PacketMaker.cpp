#include "pch.h"
#include "PacketMaker.h"
#include "Monster.h"
#include "Player.h"
#include "Unit.h"
#include "Projectile.h"
#include "HomingProjectile.h"
#include "SkillshotProjectile.h"
#include "Effect.h"


// ===========================================================================
// Player
// ===========================================================================

Proto::S_EnterGame PacketMaker::MakeEnterGame(const Player& player, const Proto::Vector2& spawnPos)
{
	Proto::S_EnterGame pkt;
	pkt.set_player_id(player.GetPlayerId());
	pkt.set_guid(player.GetGuid());
	*pkt.mutable_spawn_position() = spawnPos;
	return pkt;
}

Proto::S_PlayerSpawn PacketMaker::MakePlayerSpawn(const Player& player)
{
	Proto::S_PlayerSpawn pkt;
	*pkt.mutable_player() = MakePlayerInfo(player);
	return pkt;
}

Proto::S_PlayerList PacketMaker::MakePlayerList(const std::vector<std::shared_ptr<Player>>& players)
{
	Proto::S_PlayerList pkt;
	for (const auto& p : players)
		*pkt.add_players() = MakePlayerInfo(*p);
	return pkt;
}

Proto::S_PlayerMove PacketMaker::MakePlayerMove(const Player& player)
{
	Proto::S_PlayerMove pkt;
	pkt.set_player_id(player.GetPlayerId());
	*pkt.mutable_position() = player.GetPosition();
	pkt.set_yaw(player.GetYaw());
	return pkt;
}

Proto::S_PlayerLeave PacketMaker::MakePlayerLeave(const int32 playerId)
{
	Proto::S_PlayerLeave pkt;
	pkt.set_player_id(playerId);
	return pkt;
}

Proto::S_Chat PacketMaker::MakeChat(const Player& sender, const std::string& message)
{
	Proto::S_Chat pkt;
	pkt.set_player_id(sender.GetPlayerId());
	pkt.set_sender(sender.GetName());
	pkt.set_message(message);
	return pkt;
}

Proto::S_MoveCorrection PacketMaker::MakeMoveCorrection(const Proto::Vector2& position)
{
	Proto::S_MoveCorrection pkt;
	*pkt.mutable_position() = position;
	return pkt;
}


// ===========================================================================
// Monster
// ===========================================================================

Proto::S_MonsterSpawn PacketMaker::MakeMonsterSpawn(const Monster& monster)
{
	Proto::S_MonsterSpawn pkt;
	*pkt.mutable_monster() = MakeMonsterInfo(monster);
	return pkt;
}

Proto::S_MonsterList PacketMaker::MakeMonsterList(const std::vector<std::shared_ptr<Monster>>& monsters)
{
	Proto::S_MonsterList pkt;
	for (const auto& m : monsters)
		*pkt.add_monsters() = MakeMonsterInfo(*m);
	return pkt;
}

Proto::S_MonsterMove PacketMaker::MakeMonsterMove(const Monster& monster)
{
	Proto::S_MonsterMove pkt;
	pkt.set_guid(monster.GetGuid());
	*pkt.mutable_position() = monster.GetPosition();
	return pkt;
}

Proto::S_MonsterDespawn PacketMaker::MakeMonsterDespawn(const long long guid)
{
	Proto::S_MonsterDespawn pkt;
	pkt.set_guid(guid);
	return pkt;
}

Proto::S_MonsterState PacketMaker::MakeMonsterState(const Monster& monster, const MonsterStateId nextState)
{
	Proto::S_MonsterState pkt;
	pkt.set_guid(monster.GetGuid());
	pkt.set_state(static_cast<uint32>(nextState));
	pkt.set_target_guid(monster.GetTargetGuid());
	return pkt;
}

Proto::S_SkillHit PacketMaker::MakeSkillHit(const long long casterGuid, const long long targetGuid,
                                            const int32 skillId, const int32 damage,
                                            const Proto::Vector2& casterPos,
                                            const Proto::Vector2& hitPos)
{
	Proto::S_SkillHit pkt;
	pkt.set_caster_guid(casterGuid);
	pkt.set_target_guid(targetGuid);
	pkt.set_skill_id(skillId);
	pkt.set_damage(damage);
	*pkt.mutable_caster_pos() = casterPos;
	*pkt.mutable_hit_pos()    = hitPos;
	return pkt;
}


// ===========================================================================
// Unit
// ===========================================================================

Proto::S_UnitHp PacketMaker::MakeUnitHp(const Unit& unit)
{
	Proto::S_UnitHp pkt;
	pkt.set_guid(unit.GetGuid());
	pkt.set_hp(unit.GetHp());
	pkt.set_max_hp(unit.GetMaxHp());
	return pkt;
}


// ===========================================================================
// Buff
// ===========================================================================

Proto::S_BuffApplied PacketMaker::MakeBuffApplied(const long long targetGuid, const Effect& e, const long long casterGuid)
{
	Proto::S_BuffApplied pkt;
	pkt.set_target_guid(targetGuid);
	pkt.set_eid(e.eid);
	pkt.set_caster_guid(casterGuid);
	pkt.set_duration(e.duration);
	return pkt;
}

Proto::S_BuffRemoved PacketMaker::MakeBuffRemoved(const long long targetGuid, const int32 eid)
{
	Proto::S_BuffRemoved pkt;
	pkt.set_target_guid(targetGuid);
	pkt.set_eid(eid);
	return pkt;
}


// ===========================================================================
// Projectile
// ===========================================================================

Proto::S_ProjectileSpawn PacketMaker::MakeHomingProjectileSpawn(const HomingProjectile& projectile)
{
	Proto::S_ProjectileSpawn pkt;
	pkt.set_guid(projectile.GetGuid());
	pkt.set_owner_guid(projectile.GetOwnerGuid());
	pkt.set_kind(Proto::PROJECTILE_HOMING);
	*pkt.mutable_start_pos() = projectile.GetPosition();
	pkt.set_speed(projectile.GetSpeed());
	pkt.set_target_guid(projectile.GetTargetGuid());
	pkt.set_max_lifetime(projectile.GetLifetimeLimit());
	pkt.set_skill_id(projectile.GetSkillId());
	return pkt;
}

Proto::S_ProjectileSpawn PacketMaker::MakeSkillshotProjectileSpawn(const SkillshotProjectile& projectile)
{
	Proto::S_ProjectileSpawn pkt;
	pkt.set_guid(projectile.GetGuid());
	pkt.set_owner_guid(projectile.GetOwnerGuid());
	pkt.set_kind(Proto::PROJECTILE_SKILLSHOT);
	*pkt.mutable_start_pos() = projectile.GetPosition();
	pkt.set_speed(projectile.GetSpeed());
	auto* dir = pkt.mutable_dir();
	dir->set_x(projectile.GetDirX());
	dir->set_y(projectile.GetDirZ());
	pkt.set_radius(projectile.GetRadius());
	pkt.set_max_range(projectile.GetRangeLimit());
	pkt.set_skill_id(projectile.GetSkillId());
	return pkt;
}

Proto::S_ProjectileDestroy PacketMaker::MakeProjectileDestroy(const long long projectileGuid, const Proto::S_ProjectileDestroy_Reason reason)
{
	Proto::S_ProjectileDestroy pkt;
	pkt.set_projectile_guid(projectileGuid);
	pkt.set_reason(reason);
	return pkt;
}


// ===========================================================================
// Private DTO builders
// ===========================================================================

Proto::MonsterInfo PacketMaker::MakeMonsterInfo(const Monster& monster)
{
	Proto::MonsterInfo info;
	info.set_guid(monster.GetGuid());
	info.set_tid(monster.GetTemplateId());
	*info.mutable_position() = monster.GetPosition();
	info.set_detect_range(monster.GetDetectRange());
	info.set_hp(monster.GetHp());
	info.set_max_hp(monster.GetMaxHp());
	return info;
}

Proto::PlayerInfo PacketMaker::MakePlayerInfo(const Player& player)
{
	Proto::PlayerInfo info;
	info.set_player_id(player.GetPlayerId());
	info.set_name(player.GetName());
	*info.mutable_position() = player.GetPosition();
	info.set_guid(player.GetGuid());
	info.set_yaw(player.GetYaw());
	return info;
}

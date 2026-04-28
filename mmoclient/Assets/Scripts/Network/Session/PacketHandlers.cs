using System;
using Proto;

namespace MMO.Network
{
    // All gameplay-domain server-push packets exposed as multicast events.
    // The single instance lives on Net.Handlers (app-lifetime singleton, never null).
    // HandlerAutoRegistrar reflection-scans this class at GameSession startup and
    // binds each `Action<T>` event field to the session's PacketRouter.
    //
    // Subscribers attach normally:
    //     Net.Handlers.OnPlayerSpawn += handler;
    //
    // Subscriptions survive logout/reconnect because Net.Handlers outlives any
    // individual GameSession — the new session merely re-binds the events to its
    // new router; the subscriber list on each event is unchanged.
    //
    // Adding a new server-push packet is one line — declare another event below;
    // the auto-registrar picks it up automatically. If the file ever grows large
    // enough to warrant splitting by domain (Player/Monster/Combat/...), we can
    // shard back into per-domain classes — the auto-registrar API takes any
    // handler holder object, so the call site just becomes one `Register(...)`
    // per shard.
    //
    // Session-lifecycle packets (S_EnterGame, S_Error) are NOT here — they live
    // inline in GameSession.RegisterHandlers because they mutate session-owned
    // state (IsInGame, LocalPlayerId, ...) before fanning out.
    public sealed class PacketHandlers
    {
        // ── Player domain ──────────────────────────────────────────────────────
        public event Action<S_PlayerList>     OnPlayerList;
        public event Action<S_PlayerSpawn>    OnPlayerSpawn;
        public event Action<S_PlayerLeave>    OnPlayerLeave;
        public event Action<S_PlayerMove>     OnPlayerMove;
        public event Action<S_UnitPositions>  OnUnitPositions;
        public event Action<S_MoveCorrection> OnMoveCorrection;

        // ── Chat domain ────────────────────────────────────────────────────────
        public event Action<S_Chat> OnChat;

        // ── Monster / AI domain ────────────────────────────────────────────────
        public event Action<S_MonsterList>    OnMonsterList;
        public event Action<S_MonsterSpawn>   OnMonsterSpawn;
        public event Action<S_MonsterDespawn> OnMonsterDespawn;
        public event Action<S_MonsterMove>    OnMonsterMove;
        public event Action<S_MonsterState>   OnMonsterState;

        // ── Combat domain (skills, projectiles, hp, buffs) ─────────────────────
        public event Action<S_SkillHit>          OnSkillHit;
        public event Action<S_SkillCastStart>    OnSkillCastStart;
        public event Action<S_SkillCastCancel>   OnSkillCastCancel;
        public event Action<S_UnitHp>            OnUnitHp;
        public event Action<S_ProjectileSpawn>   OnProjectileSpawn;
        public event Action<S_ProjectileDestroy> OnProjectileDestroy;
        public event Action<S_BuffApplied>       OnBuffApplied;
        public event Action<S_BuffRemoved>       OnBuffRemoved;
    }
}

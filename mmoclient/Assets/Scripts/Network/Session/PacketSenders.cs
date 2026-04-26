using Google.Protobuf;
using Proto;
using UnityEngine;
using PVec2 = Proto.Vector2;   // disambiguate from UnityEngine.Vector2

namespace MMO.Network
{
    // All client→server typed senders, accessed as `Net.Senders.SendXxx(...)`.
    // The single instance lives on Net.Senders (app-lifetime singleton, never null) —
    // mirrors Net.Handlers on the receive side, so callers see a symmetric API:
    //
    //     Net.Senders.SendChat("hi");          // outgoing
    //     Net.Handlers.OnChat += handler;      // incoming
    //
    // Each method is a one-liner that builds a proto message and forwards to the
    // private TrySendVia helper, which:
    //   1. Looks up the target session (Login or Game) on NetworkManager.Instance.
    //   2. Returns false if there is no session — callers no longer need `?.`
    //      because the null is absorbed here.
    //   3. Delegates to Session.TrySend for the IsReady gate, back-pressure, and
    //      packet logging.
    //
    // Adding a new client→server packet for an existing session: one method here.
    // Adding a packet for a new session type (e.g., GatewaySession): create the
    // session class (subclass Session), expose it on NetworkManager + Net, then
    // add Send* methods routed via the new `NetworkManager.Instance?.<Session>`.
    public sealed class PacketSenders
    {
        // Generic null-aware send. Returns false if the target session is null
        // (pre-login / disconnected) or its TrySend rejects (back-pressure / not ready).
        private static bool TrySendVia<T>(Session s, T msg, bool gateOnReady)
            where T : IMessage<T>
            => s != null && s.TrySend(msg, gateOnReady);

        // ── Login channel ─────────────────────────────────────────────────────
        public bool SendLogin(string username, string password)
            => TrySendVia(NetworkManager.Instance?.Login,
                new C_Login { Username = username, Password = password },
                gateOnReady: false);

        // ── Game channel — handshake ──────────────────────────────────────────
        public bool SendEnterGame(string token)
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_EnterGame { Token = token },
                gateOnReady: false);

        // ── Game channel — gameplay (gated on IsInGame via Session.IsReady) ───
        public bool SendMoveCommand(Vector3 worldPos)
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_MoveCommand
                {
                    TargetPos = new PVec2 { X = worldPos.x, Y = worldPos.z }
                },
                gateOnReady: true);

        public bool SendStopMove()
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_StopMove(),
                gateOnReady: true);

        // Client-authoritative position report (legacy/compatibility path).
        public bool SendPlayerMove(Vector3 position, float yaw)
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_PlayerMove
                {
                    Position = new PVec2 { X = position.x, Y = position.z },
                    Yaw = yaw
                },
                gateOnReady: true);

        public bool SendChat(string text)
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_Chat { Message = text ?? "" },
                gateOnReady: true);

        // Skill use — supports all targeting types via the proto union of fields.
        // dir/targetPos use Proto.Vector2 (X,Y) where Y is the horizontal-second axis
        // (i.e., world Z in Unity convention).
        public bool SendUseSkill(int skillId, UnityEngine.Vector2 dirXZ,
                                  long targetGuid, UnityEngine.Vector2 targetPosXZ)
            => TrySendVia(NetworkManager.Instance?.Game,
                new C_UseSkill
                {
                    SkillId = skillId,
                    Dir = new PVec2 { X = dirXZ.x, Y = dirXZ.y },
                    TargetGuid = targetGuid,
                    TargetPos = new PVec2 { X = targetPosXZ.x, Y = targetPosXZ.y },
                },
                gateOnReady: true);
    }
}

using MMO.Unit;
using Proto;
using UnityEngine;

namespace MMO.Player
{
    // Remote player. Inherits position lerp + HP storage from UnitView; adds
    // yaw lerp and the player-specific identifiers (PlayerId, name).
    public sealed class RemotePlayerView : UnitView
    {
        public int PlayerId { get; private set; }
        public string PlayerName { get; private set; }

        private float _targetYaw;

        public void Init(PlayerInfo info)
        {
            PlayerId = info.PlayerId;
            Guid = info.Guid;
            PlayerName = info.Name ?? "";
            _targetPos = new Vector3(info.Position?.X ?? 0f, 0f, info.Position?.Y ?? 0f);
            _targetYaw = 0f;
            transform.position = _targetPos;
            transform.rotation = Quaternion.Euler(0f, _targetYaw, 0f);
        }

        public override void SetTargetYaw(float yaw) => _targetYaw = yaw;

        protected override void Update()
        {
            base.Update();
            float t = 1f - Mathf.Exp(-_smoothing * Time.deltaTime);
            float currentYaw = transform.eulerAngles.y;
            float newYaw = Mathf.LerpAngle(currentYaw, _targetYaw, t);
            transform.rotation = Quaternion.Euler(0f, newYaw, 0f);
        }
    }
}

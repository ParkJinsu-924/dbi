using MMO.Network;
using UnityEngine;
using UnityEngine.AI;
using UnityEngine.InputSystem;

namespace MMO.Player
{
    [RequireComponent(typeof(NavMeshAgent))]
    public sealed class ClickToMoveController : MonoBehaviour
    {
        [Header("Input")]
        [SerializeField] private LayerMask _groundMask = ~0;
        [SerializeField] private Camera _camera;

        [Header("Network Send")]
        [SerializeField, Range(1f, 30f)] private float _sendRate = 10f;
        [SerializeField] private float _positionEpsilon = 0.05f;
        [SerializeField] private float _yawEpsilon = 1f;

        private NavMeshAgent _agent;
        private float _nextSendAt;
        private Vector3 _lastSentPos;
        private float _lastSentYaw;

        private void Awake()
        {
            _agent = GetComponent<NavMeshAgent>();
            if (_camera == null) _camera = Camera.main;
            _lastSentPos = transform.position;
            _lastSentYaw = transform.eulerAngles.y;
        }

        private void Update()
        {
            HandleClick();
            TrySendPosition();
        }

        private void HandleClick()
        {
            var mouse = Mouse.current;
            if (mouse == null || _camera == null) return;
            if (!mouse.rightButton.wasPressedThisFrame) return;

            Vector2 screenPos = mouse.position.ReadValue();
            Ray ray = _camera.ScreenPointToRay(screenPos);
            if (Physics.Raycast(ray, out RaycastHit hit, 1000f, _groundMask))
                _agent.SetDestination(hit.point);
        }

        private void TrySendPosition()
        {
            if (Time.time < _nextSendAt) return;

            Vector3 pos = transform.position;
            float yaw = transform.eulerAngles.y;

            float posSqr = (pos - _lastSentPos).sqrMagnitude;
            float yawDiff = Mathf.Abs(Mathf.DeltaAngle(yaw, _lastSentYaw));
            if (posSqr < _positionEpsilon * _positionEpsilon && yawDiff < _yawEpsilon)
                return;

            if (Net.Senders.SendPlayerMove(pos, yaw))
            {
                _lastSentPos = pos;
                _lastSentYaw = yaw;
                _nextSendAt = Time.time + 1f / _sendRate;
            }
        }
    }
}

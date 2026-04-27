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

        [Header("Animation")]
        [SerializeField] private Animator _animator;
        
        [Header("Angular Speed")]
        [SerializeField] private float _angularSpeed = 720f;

        private static readonly int SpeedHash = Animator.StringToHash("Speed");

        private NavMeshAgent _agent;
        private float _nextSendAt;
        private Vector3 _lastSentPos;
        private float _lastSentYaw;

        // Rotation we drive manually so the character keeps turning even after
        // NavMeshAgent stops on a short path.
        private Vector3 _facingDir;
        private bool _hasFacing;

        private void Awake()
        {
            _agent = GetComponent<NavMeshAgent>();
            _agent.updateRotation = false;
            if (_camera == null) _camera = Camera.main;
            if (_animator == null) _animator = GetComponentInChildren<Animator>();
            _lastSentPos = transform.position;
            _lastSentYaw = transform.eulerAngles.y;
        }

        private void Update()
        {
            HandleClick();
            UpdateRotation();
            UpdateAnimator();
            TrySendPosition();
        }

        private void UpdateRotation()
        {
            Vector3 vel = _agent.velocity;
            vel.y = 0f;
            if (vel.sqrMagnitude > 0.01f)
            {
                _facingDir = vel.normalized;
                _hasFacing = true;
            }
            if (!_hasFacing) return;

            Quaternion target = Quaternion.LookRotation(_facingDir, Vector3.up);
            transform.rotation = Quaternion.RotateTowards(
                transform.rotation, target, _angularSpeed * Time.deltaTime);

            if (Quaternion.Angle(transform.rotation, target) < 0.5f)
                _hasFacing = false;
        }

        private void UpdateAnimator()
        {
            if (_animator == null) return;
            float speed = _agent.velocity.magnitude;
            _animator.SetFloat(SpeedHash, speed, 0.1f, Time.deltaTime);
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

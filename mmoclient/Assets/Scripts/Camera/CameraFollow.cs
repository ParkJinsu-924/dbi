using UnityEngine;

// Position-only smooth follow. Rotation is left to whatever the scene set
// on this Camera transform — point it at the world the way you want, and
// this component just keeps the target framed at that angle.
//
// Drop on Main Camera. Tag the local player "Player" or drag it into _target.
public sealed class CameraFollow : MonoBehaviour
{
    [SerializeField] private Transform _target;

    [Tooltip("If true, on Start the offset is computed as (camera.position - target.position) " +
             "so the scene-authored framing is preserved. Uncheck to drive the offset numerically.")]
    [SerializeField] private bool _autoDeriveOffsetFromScenePosition = true;

    [SerializeField] private Vector3 _offset = new(0f, 15f, -10f);

    [Tooltip("SmoothDamp time. 0 = instant snap. Higher = lazier follow.")]
    [SerializeField, Range(0f, 1f)] private float _smoothTime = 0f;

    private Vector3 _velocity;

    private void Start()
    {
        if (_target == null)
        {
            var go = GameObject.FindWithTag("Player");
            if (go != null) _target = go.transform;
        }

        if (_target == null)
        {
            Debug.LogWarning("[CameraFollow] target not set and no GameObject with tag 'Player' found — camera will not follow.");
            return;
        }

        if (_autoDeriveOffsetFromScenePosition)
            _offset = transform.position - _target.position;

        Debug.Log($"[CameraFollow] following '{_target.name}' offset={_offset}");
    }

    private void LateUpdate()
    {
        if (_target == null) return;
        Vector3 desired = _target.position + _offset;
        transform.position = _smoothTime <= 0f
            ? desired
            : Vector3.SmoothDamp(transform.position, desired, ref _velocity, _smoothTime);
    }
}

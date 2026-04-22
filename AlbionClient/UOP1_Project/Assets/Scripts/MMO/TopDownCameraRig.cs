using UnityEngine;
using UnityEngine.InputSystem;

// 알비온 스타일 탑다운 카메라.
// Cinemachine 없이 LateUpdate 기반으로 target 을 따라다니며, 마우스 휠 줌 / 키 회전 지원.
// Chop Chop 은 Input System 전용 모드라 legacy UnityEngine.Input 은 사용 금지.
public class TopDownCameraRig : MonoBehaviour
{
	[Header("Target")]
	[SerializeField] private Transform target;
	[SerializeField] private TransformAnchor _protagonistTransformAnchor;

	[Header("Rig")]
	[SerializeField, Range(30f, 80f)] private float pitch = 55f;
	[SerializeField] private float yaw = 45f;
	[SerializeField] private float distance = 14f;
	[SerializeField] private float minDistance = 6f;
	[SerializeField] private float maxDistance = 24f;
	[SerializeField] private float heightOffset = 1.2f;

	[Header("Smoothing")]
	[SerializeField] private float followLerp = 12f;
	[SerializeField] private float zoomLerp = 10f;
	[SerializeField] private float rotationLerp = 10f;

	[Header("Input")]
	[SerializeField] private float rotateSpeedDeg = 90f;
	[SerializeField] private float zoomSpeed = 4f;
	[SerializeField] private float scrollNormalization = 120f;

	private float _currentDistance;
	private float _currentYaw;
	private Vector3 _currentFocus;

	private void Awake()
	{
		_currentDistance = distance;
		_currentYaw = yaw;
	}

	private void OnEnable()
	{
		if (_protagonistTransformAnchor != null)
		{
			_protagonistTransformAnchor.OnAnchorProvided += OnAnchorProvided;
			if (_protagonistTransformAnchor.isSet)
				target = _protagonistTransformAnchor.Value;
		}
	}

	private void OnDisable()
	{
		if (_protagonistTransformAnchor != null)
			_protagonistTransformAnchor.OnAnchorProvided -= OnAnchorProvided;
	}

	private void OnAnchorProvided() => target = _protagonistTransformAnchor.Value;

	private void Update()
	{
		var kb = Keyboard.current;
		if (kb != null)
		{
			// Q/E 로 카메라 회전. 명명 프로퍼티는 Keyboard 디바이스 초기화 시 안전하게 바인딩됨.
			float rotInput = 0f;
			if (kb.qKey.isPressed) rotInput -= 1f;
			if (kb.eKey.isPressed) rotInput += 1f;
			yaw += rotInput * rotateSpeedDeg * Time.deltaTime;
		}

		var mouse = Mouse.current;
		if (mouse != null)
		{
			// Input System 의 scroll 은 OS raw delta (Windows 기준 1 notch = 120). 정규화 후 zoomSpeed 적용.
			float scroll = mouse.scroll.ReadValue().y / scrollNormalization;
			if (Mathf.Abs(scroll) > 0.01f)
				distance = Mathf.Clamp(distance - scroll * zoomSpeed, minDistance, maxDistance);
		}
	}

	private void LateUpdate()
	{
		if (target == null) return;

		_currentYaw = Mathf.LerpAngle(_currentYaw, yaw, rotationLerp * Time.deltaTime);
		_currentDistance = Mathf.Lerp(_currentDistance, distance, zoomLerp * Time.deltaTime);
		_currentFocus = Vector3.Lerp(_currentFocus, target.position + Vector3.up * heightOffset, followLerp * Time.deltaTime);

		Quaternion rot = Quaternion.Euler(pitch, _currentYaw, 0f);
		Vector3 offset = rot * new Vector3(0f, 0f, -_currentDistance);

		transform.position = _currentFocus + offset;
		transform.rotation = Quaternion.LookRotation(_currentFocus - transform.position, Vector3.up);
	}
}

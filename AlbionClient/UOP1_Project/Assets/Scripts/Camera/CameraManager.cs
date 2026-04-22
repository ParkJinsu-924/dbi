using UnityEngine;

// MMO 모드: Cinemachine 2.x API (CinemachineFreeLook) 가 Cinemachine 3 / Unity 6 에서 제거되어 본문 제거됨.
// 탑다운 카메라는 Assets/Scripts/MMO/TopDownCameraRig.cs 사용.
// 기존 씬/프리팹이 이 컴포넌트를 참조하므로 shell 만 유지 (missing-script 경고 방지).
public class CameraManager : MonoBehaviour
{
	public InputReader inputReader;
	public Camera mainCamera;

	[SerializeField] private TransformAnchor _cameraTransformAnchor = default;
	[SerializeField] private TransformAnchor _protagonistTransformAnchor = default;
	[SerializeField] private VoidEventChannelSO _camShakeEvent = default;

	private void OnEnable()
	{
		if (mainCamera != null && _cameraTransformAnchor != null)
			_cameraTransformAnchor.Provide(mainCamera.transform);
	}

	private void OnDisable()
	{
		_cameraTransformAnchor?.Unset();
	}

	public void SetupProtagonistVirtualCamera() { }
}

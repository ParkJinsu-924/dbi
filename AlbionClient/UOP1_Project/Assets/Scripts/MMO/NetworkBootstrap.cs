using UnityEngine;

// 씬에 추가하여 네트워크 레이어를 부팅.
// NetworkManager 가 없으면 자동 생성하고 DontDestroyOnLoad 처리.
// autoLogin 체크 시 시작과 동시에 지정된 계정으로 로그인 시도 (디버깅용).
public class NetworkBootstrap : MonoBehaviour
{
	[Header("Auto-login (debug)")]
	[SerializeField] private bool autoLogin = false;
	[SerializeField] private string username = "test";
	[SerializeField] private string password = "test";

	private void Awake()
	{
		if (NetworkManager.Instance == null)
		{
			var go = new GameObject("NetworkManager");
			go.AddComponent<NetworkManager>();
		}
	}

	private void Start()
	{
		if (!autoLogin) return;
		NetworkManager.Instance.Login(username, password);
	}
}

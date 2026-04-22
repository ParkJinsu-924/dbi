using System.Collections;
using UnityEngine;

// MMO 모드: Cinemachine 2.x CinemachineVirtualCamera 제거됨. 씬 전이 카메라 쇼트 기능은 비활성화.
public class LocationEntrance : MonoBehaviour
{
	[SerializeField] private PathSO _entrancePath;
	[SerializeField] private PathStorageSO _pathStorage = default;

	[Header("Lisenting on")]
	[SerializeField] private VoidEventChannelSO _onSceneReady;
	public PathSO EntrancePath => _entrancePath;

	private void Awake()
	{
		if (_pathStorage != null && _pathStorage.lastPathTaken == _entrancePath && _onSceneReady != null)
			_onSceneReady.OnEventRaised += PlanTransition;
	}

	private void OnDestroy()
	{
		if (_onSceneReady != null)
			_onSceneReady.OnEventRaised -= PlanTransition;
	}

	private void PlanTransition()
	{
		StartCoroutine(TransitionStub());
	}

	private IEnumerator TransitionStub()
	{
		yield return new WaitForSeconds(.1f);
		if (_onSceneReady != null)
			_onSceneReady.OnEventRaised -= PlanTransition;
	}
}

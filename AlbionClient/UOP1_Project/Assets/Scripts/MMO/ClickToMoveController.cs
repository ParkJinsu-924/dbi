using UnityEngine;
using UnityEngine.AI;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;

// 좌클릭으로 지면 위 한 점을 찍으면 NavMeshAgent 로 이동.
// UI 위에서 클릭했을 때는 무시 (IsPointerOverGameObject).
// 대상 Transform 의 rotation 은 진행 방향 기준으로 yaw 를 정렬.
[RequireComponent(typeof(NavMeshAgent))]
public class ClickToMoveController : MonoBehaviour
{
	[SerializeField] private Camera cameraRef;
	[SerializeField] private LayerMask groundMask = ~0;
	[SerializeField] private float rayMaxDistance = 200f;
	[SerializeField] private GameObject clickMarkerPrefab;
	[SerializeField] private float markerLifetime = 0.4f;
	[Tooltip("서버 Player.cpp 의 moveSpeed_ 와 일치시켜야 싱크 안 깨짐 (현재 5.0f).")]
	[SerializeField] private float agentSpeed = 5.0f;

	private NavMeshAgent _agent;

	private void Awake()
	{
		_agent = GetComponent<NavMeshAgent>();
		_agent.speed = agentSpeed;
		if (cameraRef == null) cameraRef = Camera.main;
	}

	private void Update()
	{
		if (Mouse.current == null) return;
		if (!Mouse.current.leftButton.wasPressedThisFrame) return;

		// UI 위 클릭은 무시
		if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
			return;

		Vector2 screenPos = Mouse.current.position.ReadValue();
		Ray ray = cameraRef.ScreenPointToRay(screenPos);
		if (!Physics.Raycast(ray, out RaycastHit hit, rayMaxDistance, groundMask, QueryTriggerInteraction.Ignore))
			return;

		if (NavMesh.SamplePosition(hit.point, out NavMeshHit navHit, 2f, NavMesh.AllAreas))
		{
			// Client-authoritative: 로컬 NavMeshAgent 가 이동을 수행하고,
			// NetworkPlayerSync 가 매 프레임 transform.position 을 10Hz 로 서버에 송신.
			// C_MoveCommand 는 서버가 no-op 처리하므로 보내지 않는다.
			_agent.SetDestination(navHit.position);

			if (clickMarkerPrefab != null)
			{
				GameObject marker = Instantiate(clickMarkerPrefab, navHit.position + Vector3.up * 0.05f, Quaternion.identity);
				Destroy(marker, markerLifetime);
			}
		}
	}
}

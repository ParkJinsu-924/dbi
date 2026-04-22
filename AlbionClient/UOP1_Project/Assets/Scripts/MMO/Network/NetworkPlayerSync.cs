using UnityEngine;

// Client-authoritative 이동: 자기 transform.position 을 10Hz 로 서버에 C_PlayerMove 로 송신.
// 서버는 받은 위치를 권위로 채택하고 S_PlayerMove 로 다른 클라에 브로드캐스트.
// 서버 sanity check (NavMesh 이탈 등) 실패 시 S_MoveCorrection 으로 보정 패킷 수신.
public class NetworkPlayerSync : MonoBehaviour
{
    [SerializeField] private float sendInterval = 0.1f; // 100ms = 10 Hz
    [SerializeField] private float positionThreshold = 0.01f;
    [SerializeField] private float yawThreshold = 1f;

    private float sendTimer;
    private Vector3 lastSentPosition;
    private float lastSentYaw;

    private void OnEnable()
    {
        if (NetworkManager.Instance != null)
            NetworkManager.Instance.OnMoveCorrection += HandleMoveCorrection;
    }

    private void OnDisable()
    {
        if (NetworkManager.Instance != null)
            NetworkManager.Instance.OnMoveCorrection -= HandleMoveCorrection;
    }

    private void Update()
    {
        var nm = NetworkManager.Instance;
        // IsInGame: S_EnterGame 수신 후에만 true. TCP 연결만으로는 부족 (Player 미생성 상태).
        if (nm == null || !nm.IsInGame) return;

        sendTimer -= Time.deltaTime;
        if (sendTimer > 0f) return;
        sendTimer = sendInterval;

        Vector3 pos = transform.position;
        float yaw = transform.eulerAngles.y;

        bool moved = Vector3.Distance(pos, lastSentPosition) > positionThreshold
                     || Mathf.Abs(Mathf.DeltaAngle(yaw, lastSentYaw)) > yawThreshold;
        if (!moved) return;

        nm.SendMove(pos, yaw);
        lastSentPosition = pos;
        lastSentYaw = yaw;
    }

    private void HandleMoveCorrection(Proto.S_MoveCorrection pkt)
    {
        var correctedPos = new Vector3(pkt.Position.X, 0f, pkt.Position.Y);
        var cc = GetComponent<CharacterController>();

        if (cc != null)
        {
            cc.enabled = false;
            transform.position = correctedPos;
            cc.enabled = true;
        }
        else
        {
            transform.position = correctedPos;
        }

        // NavMeshAgent 가 있으면 destination 도 재지정 (무효 위치로 가지 않도록)
        var agent = GetComponent<UnityEngine.AI.NavMeshAgent>();
        if (agent != null && agent.isOnNavMesh)
            agent.Warp(correctedPos);

        lastSentPosition = correctedPos;
        Debug.Log($"[NetworkPlayerSync] Position corrected to {correctedPos}");
    }
}

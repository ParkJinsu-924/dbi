using UnityEngine;

public class NetworkPlayerSync : MonoBehaviour
{
    [SerializeField] private float sendInterval = 0.1f; // 100ms = 10 updates/sec

    private float sendTimer;
    private Vector3 lastSentPosition;
    private float lastSentYaw;
    private const float POSITION_THRESHOLD = 0.01f;

    private void Update()
    {
        if (NetworkManager.Instance == null || !NetworkManager.Instance.IsConnectedToGame)
            return;

        sendTimer -= Time.deltaTime;
        if (sendTimer > 0f) return;
        sendTimer = sendInterval;

        Vector3 currentPos = transform.position;
        float currentYaw = transform.eulerAngles.y;

        // Only send if position actually changed
        if (Vector3.Distance(currentPos, lastSentPosition) > POSITION_THRESHOLD ||
            Mathf.Abs(currentYaw - lastSentYaw) > 1f)
        {
            NetworkManager.Instance.SendMove(currentPos, currentYaw);
            lastSentPosition = currentPos;
            lastSentYaw = currentYaw;
        }
    }
}

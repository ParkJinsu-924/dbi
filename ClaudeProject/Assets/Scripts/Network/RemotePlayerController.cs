using UnityEngine;

public class RemotePlayerController : MonoBehaviour
{
    [SerializeField] private float interpolationSpeed = 15f;
    [SerializeField] private float teleportThreshold = 10f;
    [SerializeField] private float rotationSpeed = 12f;

    private Vector3 targetPosition;
    private float targetYaw;
    private bool initialized;

    public int PlayerId { get; set; }
    public string PlayerName { get; set; }

    public void SetTarget(Vector3 position, float yaw)
    {
        if (!initialized)
        {
            transform.position = position;
            transform.rotation = Quaternion.Euler(0f, yaw, 0f);
            initialized = true;
        }

        targetPosition = position;
        targetYaw = yaw;
    }

    private void Update()
    {
        if (!initialized) return;

        float dist = Vector3.Distance(transform.position, targetPosition);

        if (dist > teleportThreshold)
        {
            transform.position = targetPosition;
        }
        else
        {
            transform.position = Vector3.Lerp(transform.position, targetPosition, interpolationSpeed * Time.deltaTime);
        }

        Quaternion targetRotation = Quaternion.Euler(0f, targetYaw, 0f);
        transform.rotation = Quaternion.Slerp(transform.rotation, targetRotation, rotationSpeed * Time.deltaTime);
    }
}

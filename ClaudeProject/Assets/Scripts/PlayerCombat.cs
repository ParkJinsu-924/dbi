using UnityEngine;
using UnityEngine.InputSystem;
using System.Collections;

public class PlayerCombat : MonoBehaviour
{
    [Header("Combo Settings")]
    [SerializeField] private int maxCombo = 3;
    [SerializeField] private float comboWindow = 0.8f;
    [SerializeField] private float[] attackDurations = { 0.4f, 0.35f, 0.5f };
    [SerializeField] private float[] attackDamages = { 10f, 15f, 25f };

    [Header("Hit Detection")]
    [SerializeField] private float attackRange = 2f;
    [SerializeField] private float attackAngle = 90f;
    [SerializeField] private Transform attackPoint;
    [SerializeField] private LayerMask enemyLayer;

    [Header("Visual Feedback")]
    [SerializeField] private Color attackColor = Color.yellow;

    private int currentCombo;
    private float comboTimer;
    private bool isAttacking;
    private bool attackQueued;
    private Renderer playerRenderer;
    private Color originalColor;

    public bool IsAttacking => isAttacking;

    private void Awake()
    {
        playerRenderer = GetComponentInChildren<Renderer>();
        if (playerRenderer != null)
            originalColor = playerRenderer.material.color;

        if (attackPoint == null)
            attackPoint = transform;
    }

    // PlayerInput SendMessages callback
    public void OnAttack(InputValue value)
    {
        if (value.isPressed)
            TryAttack();
    }

    private void Update()
    {
        if (comboTimer > 0f)
        {
            comboTimer -= Time.deltaTime;
            if (comboTimer <= 0f)
                ResetCombo();
        }
    }

    private void TryAttack()
    {
        var playerController = GetComponent<PlayerController>();
        if (playerController != null && playerController.IsDodging) return;

        if (isAttacking)
        {
            attackQueued = true;
            return;
        }

        StartCoroutine(ExecuteAttack());
    }

    private IEnumerator ExecuteAttack()
    {
        isAttacking = true;
        int comboIndex = currentCombo;
        float duration = attackDurations[Mathf.Min(comboIndex, attackDurations.Length - 1)];
        float damage = attackDamages[Mathf.Min(comboIndex, attackDamages.Length - 1)];

        // Visual feedback - flash color
        if (playerRenderer != null)
            playerRenderer.material.color = attackColor;

        // Wait for windup (first 30% of duration)
        yield return new WaitForSeconds(duration * 0.3f);

        // Active hit frame - detect enemies
        DetectHit(damage);

        // Visual: brighter at hit moment
        if (playerRenderer != null)
            playerRenderer.material.color = Color.white;

        // Recovery phase
        yield return new WaitForSeconds(duration * 0.7f);

        // Reset visual
        if (playerRenderer != null)
            playerRenderer.material.color = originalColor;

        currentCombo++;
        isAttacking = false;

        if (currentCombo >= maxCombo)
        {
            ResetCombo();
        }
        else
        {
            comboTimer = comboWindow;

            if (attackQueued)
            {
                attackQueued = false;
                StartCoroutine(ExecuteAttack());
            }
        }
    }

    private void DetectHit(float damage)
    {
        Collider[] hits = Physics.OverlapSphere(attackPoint.position + transform.forward * (attackRange * 0.5f), attackRange, enemyLayer);

        foreach (var hit in hits)
        {
            // Check if within attack angle
            Vector3 dirToTarget = (hit.transform.position - transform.position).normalized;
            float angle = Vector3.Angle(transform.forward, dirToTarget);

            if (angle <= attackAngle * 0.5f)
            {
                var health = hit.GetComponent<HealthSystem>();
                if (health != null)
                {
                    health.TakeDamage(damage);

                    // Knockback
                    var rb = hit.GetComponent<Rigidbody>();
                    if (rb != null)
                        rb.AddForce(dirToTarget * 5f, ForceMode.Impulse);
                }
            }
        }
    }

    private void ResetCombo()
    {
        currentCombo = 0;
        comboTimer = 0f;
        attackQueued = false;
    }

    private void OnDrawGizmosSelected()
    {
        Vector3 center = (attackPoint != null ? attackPoint.position : transform.position) + transform.forward * (attackRange * 0.5f);
        Gizmos.color = Color.red;
        Gizmos.DrawWireSphere(center, attackRange);
    }
}

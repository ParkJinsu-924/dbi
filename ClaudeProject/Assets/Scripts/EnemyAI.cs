using UnityEngine;
using UnityEngine.AI;
using System.Collections;

[RequireComponent(typeof(NavMeshAgent))]
[RequireComponent(typeof(HealthSystem))]
public class EnemyAI : MonoBehaviour
{
    private enum State { Idle, Chase, Attack }

    [Header("Detection")]
    [SerializeField] private float detectionRange = 12f;
    [SerializeField] private float loseRange = 18f;

    [Header("Attack")]
    [SerializeField] private float attackRange = 2.2f;
    [SerializeField] private float attackDamage = 10f;
    [SerializeField] private float attackCooldown = 1.5f;
    [SerializeField] private float attackDuration = 0.6f;

    [Header("Movement")]
    [SerializeField] private float chaseSpeed = 5f;
    [SerializeField] private float patrolSpeed = 2f;
    [SerializeField] private float patrolRadius = 5f;
    [SerializeField] private float patrolWaitTime = 2f;

    [Header("Visual")]
    [SerializeField] private Color aggroColor = new Color(1f, 0.3f, 0.3f);

    private State currentState = State.Idle;
    private NavMeshAgent agent;
    private HealthSystem health;
    private Transform player;
    private Renderer enemyRenderer;
    private Color originalColor;

    private float attackTimer;
    private float patrolTimer;
    private Vector3 spawnPosition;
    private bool isAttacking;

    private void Awake()
    {
        agent = GetComponent<NavMeshAgent>();
        health = GetComponent<HealthSystem>();
        enemyRenderer = GetComponentInChildren<Renderer>();

        if (enemyRenderer != null)
            originalColor = enemyRenderer.material.color;

        spawnPosition = transform.position;
    }

    private void Start()
    {
        GameObject playerObj = GameObject.FindGameObjectWithTag("Player");
        if (playerObj != null)
            player = playerObj.transform;

        health.OnDeath += OnDeath;
        health.OnDamaged += OnDamaged;
    }

    private void Update()
    {
        if (health.IsDead || player == null) return;

        float distToPlayer = Vector3.Distance(transform.position, player.position);
        attackTimer -= Time.deltaTime;

        switch (currentState)
        {
            case State.Idle:
                UpdateIdle(distToPlayer);
                break;
            case State.Chase:
                UpdateChase(distToPlayer);
                break;
            case State.Attack:
                UpdateAttack(distToPlayer);
                break;
        }
    }

    private void UpdateIdle(float distToPlayer)
    {
        agent.speed = patrolSpeed;

        // Detect player
        if (distToPlayer <= detectionRange)
        {
            SetState(State.Chase);
            return;
        }

        // Simple patrol: wait, then move to random nearby point
        patrolTimer -= Time.deltaTime;
        if (patrolTimer <= 0f && (!agent.hasPath || agent.remainingDistance < 0.5f))
        {
            Vector3 randomDir = Random.insideUnitSphere * patrolRadius;
            randomDir += spawnPosition;
            randomDir.y = spawnPosition.y;

            if (NavMesh.SamplePosition(randomDir, out NavMeshHit hit, patrolRadius, NavMesh.AllAreas))
                agent.SetDestination(hit.position);

            patrolTimer = patrolWaitTime + Random.Range(0f, 2f);
        }
    }

    private void UpdateChase(float distToPlayer)
    {
        agent.speed = chaseSpeed;

        // Lose aggro
        if (distToPlayer > loseRange)
        {
            SetState(State.Idle);
            return;
        }

        // In attack range
        if (distToPlayer <= attackRange)
        {
            SetState(State.Attack);
            return;
        }

        agent.SetDestination(player.position);
    }

    private void UpdateAttack(float distToPlayer)
    {
        agent.ResetPath();

        // Face player
        Vector3 lookDir = (player.position - transform.position).normalized;
        lookDir.y = 0f;
        if (lookDir.sqrMagnitude > 0.01f)
            transform.rotation = Quaternion.Slerp(transform.rotation, Quaternion.LookRotation(lookDir), 10f * Time.deltaTime);

        // Player moved out of range
        if (distToPlayer > attackRange * 1.5f)
        {
            SetState(State.Chase);
            return;
        }

        // Attack
        if (attackTimer <= 0f && !isAttacking)
        {
            StartCoroutine(ExecuteAttack());
        }
    }

    private IEnumerator ExecuteAttack()
    {
        isAttacking = true;

        // Windup - visual cue
        if (enemyRenderer != null)
            enemyRenderer.material.color = Color.white;

        yield return new WaitForSeconds(attackDuration * 0.4f);

        // Hit check
        if (player != null)
        {
            float dist = Vector3.Distance(transform.position, player.position);
            if (dist <= attackRange * 1.5f)
            {
                var playerHealth = player.GetComponent<HealthSystem>();
                if (playerHealth != null)
                    playerHealth.TakeDamage(attackDamage);
            }
        }

        // Recovery
        if (enemyRenderer != null)
            enemyRenderer.material.color = currentState == State.Idle ? originalColor : aggroColor;

        yield return new WaitForSeconds(attackDuration * 0.6f);

        isAttacking = false;
        attackTimer = attackCooldown;
    }

    private void SetState(State newState)
    {
        currentState = newState;

        if (enemyRenderer != null)
        {
            enemyRenderer.material.color = newState == State.Idle ? originalColor : aggroColor;
        }
    }

    private void OnDamaged(float damage)
    {
        // Getting hit always triggers aggro
        if (currentState == State.Idle)
            SetState(State.Chase);
    }

    private void OnDeath()
    {
        agent.enabled = false;
        StopAllCoroutines();

        // Notify GameManager
        var gm = FindFirstObjectByType<GameManager>();
        if (gm != null)
            gm.OnEnemyKilled();
    }

    private void OnDrawGizmosSelected()
    {
        Gizmos.color = Color.yellow;
        Gizmos.DrawWireSphere(transform.position, detectionRange);
        Gizmos.color = Color.red;
        Gizmos.DrawWireSphere(transform.position, attackRange);
    }
}

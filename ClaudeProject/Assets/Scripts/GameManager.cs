using UnityEngine;
using UnityEngine.UI;

public class GameManager : MonoBehaviour
{
    [Header("Enemy Spawning")]
    [SerializeField] private GameObject enemyPrefab;
    [SerializeField] private Transform[] spawnPoints;
    [SerializeField] private float spawnInterval = 5f;
    [SerializeField] private int maxEnemies = 8;

    [Header("UI References")]
    [SerializeField] private Slider playerHealthBar;
    [SerializeField] private Text killCountText;
    [SerializeField] private Text waveText;

    private int currentEnemyCount;
    private int killCount;
    private int waveNumber = 1;
    private float spawnTimer;
    private HealthSystem playerHealth;

    private void Start()
    {
        GameObject player = GameObject.FindGameObjectWithTag("Player");
        if (player != null)
        {
            playerHealth = player.GetComponent<HealthSystem>();
            if (playerHealth != null)
            {
                playerHealth.OnDamaged += _ => UpdateUI();
                playerHealth.OnHealed += _ => UpdateUI();
                playerHealth.OnDeath += OnPlayerDeath;
            }
        }

        UpdateUI();
    }

    private void Update()
    {
        if (playerHealth != null && playerHealth.IsDead) return;

        spawnTimer -= Time.deltaTime;
        if (spawnTimer <= 0f && currentEnemyCount < maxEnemies)
        {
            SpawnEnemy();
            spawnTimer = spawnInterval;
        }
    }

    private void SpawnEnemy()
    {
        if (enemyPrefab == null || spawnPoints == null || spawnPoints.Length == 0) return;

        Transform spawnPoint = spawnPoints[Random.Range(0, spawnPoints.Length)];
        GameObject enemy = Instantiate(enemyPrefab, spawnPoint.position, Quaternion.identity);
        currentEnemyCount++;
    }

    public void OnEnemyKilled()
    {
        currentEnemyCount--;
        killCount++;

        // Wave progression: every 5 kills = new wave
        int newWave = (killCount / 5) + 1;
        if (newWave > waveNumber)
        {
            waveNumber = newWave;
            spawnInterval = Mathf.Max(1.5f, spawnInterval - 0.5f);
            maxEnemies = Mathf.Min(15, maxEnemies + 1);
        }

        UpdateUI();
    }

    private void OnPlayerDeath()
    {
        if (waveText != null)
            waveText.text = "GAME OVER";
        Time.timeScale = 0f;
    }

    private void UpdateUI()
    {
        if (playerHealthBar != null && playerHealth != null)
            playerHealthBar.value = playerHealth.HealthPercent;

        if (killCountText != null)
            killCountText.text = "Kills: " + killCount;

        if (waveText != null)
            waveText.text = "Wave " + waveNumber;
    }
}

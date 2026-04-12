using UnityEngine;
using System;
using System.Collections;

public class HealthSystem : MonoBehaviour
{
    [Header("Health")]
    [SerializeField] private float maxHealth = 100f;
    [SerializeField] private float invincibilityDuration = 0.3f;

    [Header("Visual Feedback")]
    [SerializeField] private Color hitColor = Color.red;
    [SerializeField] private float hitFlashDuration = 0.15f;

    private float currentHealth;
    private bool isInvincible;
    private Renderer objectRenderer;
    private Color originalColor;

    public float CurrentHealth => currentHealth;
    public float MaxHealth => maxHealth;
    public float HealthPercent => currentHealth / maxHealth;
    public bool IsDead => currentHealth <= 0f;

    public event Action<float> OnDamaged;       // damage amount
    public event Action<float> OnHealed;        // heal amount
    public event Action OnDeath;

    private void Awake()
    {
        currentHealth = maxHealth;
        objectRenderer = GetComponentInChildren<Renderer>();
        if (objectRenderer != null)
            originalColor = objectRenderer.material.color;
    }

    public void TakeDamage(float damage)
    {
        if (IsDead || isInvincible) return;

        currentHealth = Mathf.Max(0f, currentHealth - damage);
        OnDamaged?.Invoke(damage);

        StartCoroutine(HitFlash());

        if (currentHealth <= 0f)
        {
            Die();
        }
        else
        {
            StartCoroutine(InvincibilityFrames());
        }
    }

    public void Heal(float amount)
    {
        if (IsDead) return;
        float before = currentHealth;
        currentHealth = Mathf.Min(maxHealth, currentHealth + amount);
        OnHealed?.Invoke(currentHealth - before);
    }

    public void SetInvincible(bool value)
    {
        isInvincible = value;
    }

    private IEnumerator InvincibilityFrames()
    {
        isInvincible = true;
        yield return new WaitForSeconds(invincibilityDuration);
        isInvincible = false;
    }

    private IEnumerator HitFlash()
    {
        if (objectRenderer == null) yield break;

        objectRenderer.material.color = hitColor;
        yield return new WaitForSeconds(hitFlashDuration);

        if (!IsDead && objectRenderer != null)
            objectRenderer.material.color = originalColor;
    }

    private void Die()
    {
        OnDeath?.Invoke();

        // Default death behavior: enemies get destroyed, player stays
        if (!CompareTag("Player"))
        {
            // Flash and destroy
            if (objectRenderer != null)
                objectRenderer.material.color = Color.black;
            Destroy(gameObject, 0.3f);
        }
    }
}

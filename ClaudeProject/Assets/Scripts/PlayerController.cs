using UnityEngine;
using UnityEngine.InputSystem;

[RequireComponent(typeof(CharacterController))]
public class PlayerController : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float moveSpeed = 7f;
    [SerializeField] private float sprintSpeed = 12f;
    [SerializeField] private float rotationSpeed = 15f;
    [SerializeField] private float gravity = -20f;

    [Header("Dodge")]
    [SerializeField] private float dodgeSpeed = 18f;
    [SerializeField] private float dodgeDuration = 0.25f;
    [SerializeField] private float dodgeCooldown = 0.8f;

    private CharacterController controller;
    private Camera mainCamera;
    private Vector2 moveInput;
    private bool isSprinting;
    private float verticalVelocity;

    private bool isDodging;
    private float dodgeTimer;
    private float dodgeCooldownTimer;
    private Vector3 dodgeDirection;

    public bool IsDodging => isDodging;
    public bool IsMoving => moveInput.sqrMagnitude > 0.01f;

    private void Awake()
    {
        controller = GetComponent<CharacterController>();
        mainCamera = Camera.main;
    }

    // PlayerInput SendMessages callbacks
    public void OnMove(InputValue value)
    {
        moveInput = value.Get<Vector2>();
    }

    public void OnSprint(InputValue value)
    {
        isSprinting = value.isPressed;
    }

    public void OnJump(InputValue value)
    {
        // Jump input used as dodge in hack-and-slash
        if (value.isPressed)
            TryDodge();
    }

    private void Update()
    {
        HandleDodge();

        if (!isDodging)
            HandleMovement();

        ApplyGravity();
    }

    private void HandleMovement()
    {
        Vector3 direction = GetCameraRelativeDirection(moveInput);

        if (direction.sqrMagnitude > 0.01f)
        {
            // Rotate towards movement direction
            Quaternion targetRotation = Quaternion.LookRotation(direction);
            transform.rotation = Quaternion.Slerp(transform.rotation, targetRotation, rotationSpeed * Time.deltaTime);

            float speed = isSprinting ? sprintSpeed : moveSpeed;
            Vector3 move = direction * speed + Vector3.up * verticalVelocity;
            controller.Move(move * Time.deltaTime);
        }
        else
        {
            controller.Move(Vector3.up * verticalVelocity * Time.deltaTime);
        }
    }

    private void TryDodge()
    {
        if (isDodging || dodgeCooldownTimer > 0f) return;

        isDodging = true;
        dodgeTimer = dodgeDuration;
        dodgeCooldownTimer = dodgeCooldown;

        // Dodge in movement direction, or forward if standing still
        dodgeDirection = moveInput.sqrMagnitude > 0.01f
            ? GetCameraRelativeDirection(moveInput)
            : transform.forward;

        // Grant invincibility during dodge
        var health = GetComponent<HealthSystem>();
        if (health != null) health.SetInvincible(true);
    }

    private void HandleDodge()
    {
        if (dodgeCooldownTimer > 0f)
            dodgeCooldownTimer -= Time.deltaTime;

        if (!isDodging) return;

        dodgeTimer -= Time.deltaTime;
        Vector3 move = dodgeDirection * dodgeSpeed + Vector3.up * verticalVelocity;
        controller.Move(move * Time.deltaTime);

        if (dodgeTimer <= 0f)
        {
            isDodging = false;
            var health = GetComponent<HealthSystem>();
            if (health != null) health.SetInvincible(false);
        }
    }

    private void ApplyGravity()
    {
        if (controller.isGrounded && verticalVelocity < 0f)
            verticalVelocity = -2f;
        else
            verticalVelocity += gravity * Time.deltaTime;
    }

    private Vector3 GetCameraRelativeDirection(Vector2 input)
    {
        if (mainCamera == null) mainCamera = Camera.main;

        Vector3 camForward = mainCamera.transform.forward;
        Vector3 camRight = mainCamera.transform.right;
        camForward.y = 0f;
        camRight.y = 0f;
        camForward.Normalize();
        camRight.Normalize();

        return (camForward * input.y + camRight * input.x).normalized;
    }
}

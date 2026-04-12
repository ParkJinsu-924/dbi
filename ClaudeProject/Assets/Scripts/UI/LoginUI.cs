using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;

public class LoginUI : MonoBehaviour
{
    [SerializeField] private InputField usernameField;
    [SerializeField] private InputField passwordField;
    [SerializeField] private Button loginButton;
    [SerializeField] private Text statusText;
    [SerializeField] private GameObject loginPanel;
    [SerializeField] private GameObject gameUI;

    private void Start()
    {
        EnsureEventSystem();

        if (loginButton != null)
            loginButton.onClick.AddListener(OnLoginClicked);

        if (gameUI != null)
            gameUI.SetActive(false);

        SetPlayerInputEnabled(false);

        var nm = NetworkManager.Instance;
        if (nm != null)
        {
            nm.OnLoginSuccess += OnLoginSuccess;
            nm.OnLoginFail += OnLoginFail;
            nm.OnEnterGameSuccess += OnEnterGame;
            nm.OnDisconnectedFromGame += OnDisconnected;
        }
    }

    private void OnLoginClicked()
    {
        string username = usernameField != null ? usernameField.text : "Player";
        string password = passwordField != null ? passwordField.text : "pass";

        if (string.IsNullOrEmpty(username))
        {
            SetStatus("Username is required");
            return;
        }

        SetStatus("Connecting...");
        loginButton.interactable = false;

        NetworkManager.Instance.Login(username, password);
    }

    private void OnLoginSuccess()
    {
        SetStatus("Login success! Entering game...");
    }

    private void OnLoginFail(string error)
    {
        SetStatus("Login failed: " + error);
        if (loginButton != null)
            loginButton.interactable = true;
    }

    private void OnEnterGame(int playerId, Vector3 spawnPos)
    {
        SetStatus("Entered game! ID: " + playerId);

        if (loginPanel != null)
            loginPanel.SetActive(false);

        if (gameUI != null)
            gameUI.SetActive(true);

        SetPlayerInputEnabled(true);
    }

    private void OnDisconnected()
    {
        SetStatus("Disconnected from server");

        if (loginPanel != null)
            loginPanel.SetActive(true);

        if (gameUI != null)
            gameUI.SetActive(false);

        if (loginButton != null)
            loginButton.interactable = true;

        SetPlayerInputEnabled(false);
    }

    private void SetPlayerInputEnabled(bool enabled)
    {
        var player = GameObject.FindGameObjectWithTag("Player");
        if (player == null) return;

        var playerInput = player.GetComponent<PlayerInput>();
        if (playerInput != null)
            playerInput.enabled = enabled;
    }

    private void EnsureEventSystem()
    {
        if (EventSystem.current != null) return;

        var go = new GameObject("EventSystem");
        go.AddComponent<EventSystem>();
        go.AddComponent<InputSystemUIInputModule>();
        Debug.Log("[LoginUI] EventSystem was missing — created at runtime.");
    }

    private void SetStatus(string msg)
    {
        if (statusText != null)
            statusText.text = msg;
        Debug.Log("[LoginUI] " + msg);
    }

    private void OnDestroy()
    {
        var nm = NetworkManager.Instance;
        if (nm != null)
        {
            nm.OnLoginSuccess -= OnLoginSuccess;
            nm.OnLoginFail -= OnLoginFail;
            nm.OnEnterGameSuccess -= OnEnterGame;
            nm.OnDisconnectedFromGame -= OnDisconnected;
        }
    }
}

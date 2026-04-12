using UnityEngine;
using UnityEngine.AI;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;
using UnityEditor;
using UnityEditor.AI;

public class HackAndSlashSceneSetup : EditorWindow
{
    [MenuItem("Hack And Slash/Setup Scene")]
    public static void SetupScene()
    {
        if (!EditorUtility.DisplayDialog("Hack & Slash Setup",
            "This will set up the prototype scene.\nExisting objects won't be deleted.\n\nProceed?",
            "Yes", "Cancel"))
            return;

        CreateGround();
        GameObject player = CreatePlayer();
        CreateCamera(player);
        GameObject enemyPrefab = CreateEnemyPrefab();
        CreateGameManager(enemyPrefab);
        CreateUI();
        CreateNetworkObjects();
        CreateEventSystem();
        BakeNavMesh();

        Debug.Log("[Hack & Slash] Scene setup complete! Press Play to test.");
    }

    private static void CreateGround()
    {
        // Main ground plane
        GameObject ground = GameObject.CreatePrimitive(PrimitiveType.Cube);
        ground.name = "Ground";
        ground.transform.position = Vector3.zero;
        ground.transform.localScale = new Vector3(40f, 0.5f, 40f);
        ground.isStatic = true;

        // Ground material
        var renderer = ground.GetComponent<Renderer>();
        Material groundMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        groundMat.color = new Color(0.35f, 0.45f, 0.35f);
        renderer.material = groundMat;

        // Add NavMeshSurface if available, otherwise mark as Navigation Static
        ground.isStatic = true;

        // Add some obstacles for variety
        CreateObstacle(new Vector3(5f, 0.75f, 5f), new Vector3(2f, 1.5f, 2f));
        CreateObstacle(new Vector3(-7f, 0.75f, 3f), new Vector3(3f, 1.5f, 1.5f));
        CreateObstacle(new Vector3(3f, 0.75f, -8f), new Vector3(1.5f, 1.5f, 3f));
        CreateObstacle(new Vector3(-5f, 0.75f, -6f), new Vector3(2f, 1.5f, 2f));
    }

    private static void CreateObstacle(Vector3 position, Vector3 scale)
    {
        GameObject obstacle = GameObject.CreatePrimitive(PrimitiveType.Cube);
        obstacle.name = "Obstacle";
        obstacle.transform.position = position;
        obstacle.transform.localScale = scale;
        obstacle.isStatic = true;

        var renderer = obstacle.GetComponent<Renderer>();
        Material mat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        mat.color = new Color(0.5f, 0.4f, 0.3f);
        renderer.material = mat;
    }

    private static GameObject CreatePlayer()
    {
        // Player root
        GameObject player = new GameObject("Player");
        player.tag = "Player";
        player.layer = LayerMask.NameToLayer("Default");
        player.transform.position = new Vector3(0f, 0.5f, 0f);

        // Body (Capsule)
        GameObject body = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        body.name = "Body";
        body.transform.SetParent(player.transform);
        body.transform.localPosition = new Vector3(0f, 1f, 0f);
        body.transform.localScale = Vector3.one;

        // Remove capsule's own collider (CharacterController handles collision)
        Object.DestroyImmediate(body.GetComponent<CapsuleCollider>());

        // Player material - blue
        var renderer = body.GetComponent<Renderer>();
        Material playerMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        playerMat.color = new Color(0.2f, 0.4f, 0.9f);
        renderer.material = playerMat;

        // Direction indicator (small cube as "nose")
        GameObject dirIndicator = GameObject.CreatePrimitive(PrimitiveType.Cube);
        dirIndicator.name = "DirectionIndicator";
        dirIndicator.transform.SetParent(player.transform);
        dirIndicator.transform.localPosition = new Vector3(0f, 1f, 0.6f);
        dirIndicator.transform.localScale = new Vector3(0.3f, 0.3f, 0.3f);
        Object.DestroyImmediate(dirIndicator.GetComponent<BoxCollider>());

        Material dirMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        dirMat.color = Color.white;
        dirIndicator.GetComponent<Renderer>().material = dirMat;

        // Attack point (empty for hit detection origin)
        GameObject attackPoint = new GameObject("AttackPoint");
        attackPoint.transform.SetParent(player.transform);
        attackPoint.transform.localPosition = new Vector3(0f, 1f, 1f);

        // CharacterController
        var cc = player.AddComponent<CharacterController>();
        cc.center = new Vector3(0f, 1f, 0f);
        cc.height = 2f;
        cc.radius = 0.5f;

        // Scripts
        player.AddComponent<PlayerController>();

        var combat = player.AddComponent<PlayerCombat>();
        // Set attack point via serialized field
        var so = new SerializedObject(combat);
        so.FindProperty("attackPoint").objectReferenceValue = attackPoint.transform;
        so.FindProperty("enemyLayer").intValue = LayerMask.GetMask("Default");
        so.ApplyModifiedProperties();

        player.AddComponent<HealthSystem>();

        // PlayerInput component
        var playerInput = player.AddComponent<PlayerInput>();
        // Try to find the InputActions asset
        string[] guids = AssetDatabase.FindAssets("InputSystem_Actions t:InputActionAsset");
        if (guids.Length > 0)
        {
            string path = AssetDatabase.GUIDToAssetPath(guids[0]);
            var inputActions = AssetDatabase.LoadAssetAtPath<InputActionAsset>(path);
            playerInput.actions = inputActions;
        }
        playerInput.defaultActionMap = "Player";

        return player;
    }

    private static void CreateCamera(GameObject player)
    {
        // Use existing main camera or create one
        Camera mainCam = Camera.main;
        if (mainCam == null)
        {
            GameObject camObj = new GameObject("Main Camera");
            camObj.tag = "MainCamera";
            mainCam = camObj.AddComponent<Camera>();
            camObj.AddComponent<AudioListener>();
        }

        var follow = mainCam.gameObject.GetComponent<CameraFollow>();
        if (follow == null)
            follow = mainCam.gameObject.AddComponent<CameraFollow>();

        // Position camera
        mainCam.transform.position = player.transform.position + new Vector3(0f, 12f, -8f);
        mainCam.transform.rotation = Quaternion.Euler(50f, 0f, 0f);
    }

    private static GameObject CreateEnemyPrefab()
    {
        // Create enemy template in scene first
        GameObject enemy = new GameObject("Enemy");
        enemy.layer = LayerMask.NameToLayer("Default");

        // Body
        GameObject body = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        body.name = "Body";
        body.transform.SetParent(enemy.transform);
        body.transform.localPosition = new Vector3(0f, 1f, 0f);
        body.transform.localScale = new Vector3(0.9f, 0.9f, 0.9f);

        // Keep the CapsuleCollider on enemy for hit detection

        // Enemy material - red
        var renderer = body.GetComponent<Renderer>();
        Material enemyMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        enemyMat.color = new Color(0.8f, 0.2f, 0.2f);
        renderer.material = enemyMat;

        // Direction indicator
        GameObject dirIndicator = GameObject.CreatePrimitive(PrimitiveType.Cube);
        dirIndicator.name = "DirectionIndicator";
        dirIndicator.transform.SetParent(enemy.transform);
        dirIndicator.transform.localPosition = new Vector3(0f, 1f, 0.5f);
        dirIndicator.transform.localScale = new Vector3(0.25f, 0.25f, 0.25f);
        Object.DestroyImmediate(dirIndicator.GetComponent<BoxCollider>());

        Material dirMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        dirMat.color = Color.black;
        dirIndicator.GetComponent<Renderer>().material = dirMat;

        // Collider on root for hit detection
        var rootCollider = enemy.AddComponent<CapsuleCollider>();
        rootCollider.center = new Vector3(0f, 1f, 0f);
        rootCollider.height = 2f;
        rootCollider.radius = 0.45f;

        // NavMeshAgent
        var agent = enemy.AddComponent<NavMeshAgent>();
        agent.speed = 5f;
        agent.angularSpeed = 180f;
        agent.stoppingDistance = 1.5f;
        agent.radius = 0.5f;
        agent.height = 2f;

        // Scripts
        enemy.AddComponent<HealthSystem>();
        enemy.AddComponent<EnemyAI>();

        // Save as prefab
        string prefabDir = "Assets/Prefabs";
        if (!AssetDatabase.IsValidFolder(prefabDir))
            AssetDatabase.CreateFolder("Assets", "Prefabs");

        string prefabPath = prefabDir + "/Enemy.prefab";
        GameObject prefab = PrefabUtility.SaveAsPrefabAsset(enemy, prefabPath);

        // Destroy scene instance
        Object.DestroyImmediate(enemy);

        Debug.Log("[Hack & Slash] Enemy prefab saved to " + prefabPath);
        return prefab;
    }

    private static void CreateGameManager(GameObject enemyPrefab)
    {
        GameObject gmObj = new GameObject("GameManager");
        var gm = gmObj.AddComponent<GameManager>();

        // Create spawn points
        Transform[] spawnPoints = new Transform[4];
        Vector3[] positions = {
            new Vector3(15f, 0.5f, 15f),
            new Vector3(-15f, 0.5f, 15f),
            new Vector3(15f, 0.5f, -15f),
            new Vector3(-15f, 0.5f, -15f)
        };

        for (int i = 0; i < 4; i++)
        {
            GameObject sp = new GameObject("SpawnPoint_" + i);
            sp.transform.position = positions[i];
            sp.transform.SetParent(gmObj.transform);
            spawnPoints[i] = sp.transform;
        }

        // Assign via SerializedObject
        var so = new SerializedObject(gm);
        so.FindProperty("enemyPrefab").objectReferenceValue = enemyPrefab;

        var spArray = so.FindProperty("spawnPoints");
        spArray.arraySize = spawnPoints.Length;
        for (int i = 0; i < spawnPoints.Length; i++)
            spArray.GetArrayElementAtIndex(i).objectReferenceValue = spawnPoints[i];

        so.ApplyModifiedProperties();
    }

    private static void CreateUI()
    {
        // Create Canvas
        GameObject canvasObj = new GameObject("UI_Canvas");
        var canvas = canvasObj.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvasObj.AddComponent<CanvasScaler>();
        canvasObj.AddComponent<GraphicRaycaster>();

        // Health Bar background
        GameObject healthBg = new GameObject("HealthBar_BG");
        healthBg.transform.SetParent(canvasObj.transform, false);
        var bgImage = healthBg.AddComponent<Image>();
        bgImage.color = new Color(0.2f, 0.2f, 0.2f, 0.8f);
        var bgRect = healthBg.GetComponent<RectTransform>();
        bgRect.anchorMin = new Vector2(0f, 1f);
        bgRect.anchorMax = new Vector2(0f, 1f);
        bgRect.pivot = new Vector2(0f, 1f);
        bgRect.anchoredPosition = new Vector2(20f, -20f);
        bgRect.sizeDelta = new Vector2(300f, 30f);

        // Health Bar slider
        GameObject healthBarObj = new GameObject("HealthBar");
        healthBarObj.transform.SetParent(healthBg.transform, false);
        var slider = healthBarObj.AddComponent<Slider>();
        slider.minValue = 0f;
        slider.maxValue = 1f;
        slider.value = 1f;
        slider.interactable = false;
        var sliderRect = healthBarObj.GetComponent<RectTransform>();
        sliderRect.anchorMin = Vector2.zero;
        sliderRect.anchorMax = Vector2.one;
        sliderRect.sizeDelta = Vector2.zero;
        sliderRect.anchoredPosition = Vector2.zero;

        // Fill area
        GameObject fillArea = new GameObject("Fill Area");
        fillArea.transform.SetParent(healthBarObj.transform, false);
        var fillAreaRect = fillArea.AddComponent<RectTransform>();
        fillAreaRect.anchorMin = Vector2.zero;
        fillAreaRect.anchorMax = Vector2.one;
        fillAreaRect.sizeDelta = Vector2.zero;

        GameObject fill = new GameObject("Fill");
        fill.transform.SetParent(fillArea.transform, false);
        var fillImage = fill.AddComponent<Image>();
        fillImage.color = new Color(0.1f, 0.85f, 0.2f);
        var fillRect = fill.GetComponent<RectTransform>();
        fillRect.anchorMin = Vector2.zero;
        fillRect.anchorMax = Vector2.one;
        fillRect.sizeDelta = Vector2.zero;

        slider.fillRect = fillRect;

        // Kill counter text
        GameObject killTextObj = new GameObject("KillCount");
        killTextObj.transform.SetParent(canvasObj.transform, false);
        var killText = killTextObj.AddComponent<Text>();
        killText.text = "Kills: 0";
        killText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        killText.fontSize = 24;
        killText.color = Color.white;
        killText.alignment = TextAnchor.UpperRight;
        var killRect = killTextObj.GetComponent<RectTransform>();
        killRect.anchorMin = new Vector2(1f, 1f);
        killRect.anchorMax = new Vector2(1f, 1f);
        killRect.pivot = new Vector2(1f, 1f);
        killRect.anchoredPosition = new Vector2(-20f, -20f);
        killRect.sizeDelta = new Vector2(200f, 40f);

        // Wave text
        GameObject waveTextObj = new GameObject("WaveText");
        waveTextObj.transform.SetParent(canvasObj.transform, false);
        var waveText = waveTextObj.AddComponent<Text>();
        waveText.text = "Wave 1";
        waveText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        waveText.fontSize = 28;
        waveText.color = Color.white;
        waveText.alignment = TextAnchor.UpperCenter;
        var waveRect = waveTextObj.GetComponent<RectTransform>();
        waveRect.anchorMin = new Vector2(0.5f, 1f);
        waveRect.anchorMax = new Vector2(0.5f, 1f);
        waveRect.pivot = new Vector2(0.5f, 1f);
        waveRect.anchoredPosition = new Vector2(0f, -20f);
        waveRect.sizeDelta = new Vector2(200f, 40f);

        // Link UI to GameManager
        var gm = Object.FindFirstObjectByType<GameManager>();
        if (gm != null)
        {
            var so = new SerializedObject(gm);
            so.FindProperty("playerHealthBar").objectReferenceValue = slider;
            so.FindProperty("killCountText").objectReferenceValue = killText;
            so.FindProperty("waveText").objectReferenceValue = waveText;
            so.ApplyModifiedProperties();
        }
    }

    private static void CreateNetworkObjects()
    {
        // NetworkManager
        GameObject nmObj = new GameObject("NetworkManager");
        nmObj.AddComponent<NetworkManager>();

        // PlayerManager
        GameObject pmObj = new GameObject("PlayerManager");
        pmObj.AddComponent<PlayerManager>();

        // Login UI Canvas
        GameObject loginCanvas = new GameObject("Login_Canvas");
        var canvas = loginCanvas.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 10;
        loginCanvas.AddComponent<CanvasScaler>();
        loginCanvas.AddComponent<GraphicRaycaster>();

        // Login Panel (background)
        GameObject loginPanel = new GameObject("LoginPanel");
        loginPanel.transform.SetParent(loginCanvas.transform, false);
        var panelImage = loginPanel.AddComponent<Image>();
        panelImage.color = new Color(0f, 0f, 0f, 0.7f);
        var panelRect = loginPanel.GetComponent<RectTransform>();
        panelRect.anchorMin = Vector2.zero;
        panelRect.anchorMax = Vector2.one;
        panelRect.sizeDelta = Vector2.zero;

        // Login Form Container (centered)
        GameObject formContainer = new GameObject("FormContainer");
        formContainer.transform.SetParent(loginPanel.transform, false);
        var formRect = formContainer.AddComponent<RectTransform>();
        formRect.anchorMin = new Vector2(0.5f, 0.5f);
        formRect.anchorMax = new Vector2(0.5f, 0.5f);
        formRect.sizeDelta = new Vector2(320f, 250f);

        var formBg = formContainer.AddComponent<Image>();
        formBg.color = new Color(0.15f, 0.15f, 0.15f, 0.95f);

        // Title
        GameObject titleObj = new GameObject("Title");
        titleObj.transform.SetParent(formContainer.transform, false);
        var titleText = titleObj.AddComponent<Text>();
        titleText.text = "HACK & SLASH";
        titleText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        titleText.fontSize = 26;
        titleText.color = Color.white;
        titleText.alignment = TextAnchor.MiddleCenter;
        var titleRect = titleObj.GetComponent<RectTransform>();
        titleRect.anchorMin = new Vector2(0f, 1f);
        titleRect.anchorMax = new Vector2(1f, 1f);
        titleRect.pivot = new Vector2(0.5f, 1f);
        titleRect.anchoredPosition = new Vector2(0f, -10f);
        titleRect.sizeDelta = new Vector2(0f, 40f);

        // Username field
        GameObject usernameObj = CreateInputField(formContainer.transform, "UsernameField",
            "Username", new Vector2(0f, -60f));
        var usernameInput = usernameObj.GetComponent<InputField>();

        // Password field
        GameObject passwordObj = CreateInputField(formContainer.transform, "PasswordField",
            "Password", new Vector2(0f, -110f));
        var passwordInput = passwordObj.GetComponent<InputField>();

        // Login button
        GameObject btnObj = new GameObject("LoginButton");
        btnObj.transform.SetParent(formContainer.transform, false);
        var btnImage = btnObj.AddComponent<Image>();
        btnImage.color = new Color(0.2f, 0.5f, 0.9f);
        var btnRect = btnObj.GetComponent<RectTransform>();
        btnRect.anchorMin = new Vector2(0.5f, 1f);
        btnRect.anchorMax = new Vector2(0.5f, 1f);
        btnRect.pivot = new Vector2(0.5f, 1f);
        btnRect.anchoredPosition = new Vector2(0f, -165f);
        btnRect.sizeDelta = new Vector2(200f, 40f);
        var btn = btnObj.AddComponent<Button>();

        GameObject btnTextObj = new GameObject("Text");
        btnTextObj.transform.SetParent(btnObj.transform, false);
        var btnText = btnTextObj.AddComponent<Text>();
        btnText.text = "LOGIN";
        btnText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        btnText.fontSize = 20;
        btnText.color = Color.white;
        btnText.alignment = TextAnchor.MiddleCenter;
        var btnTextRect = btnTextObj.GetComponent<RectTransform>();
        btnTextRect.anchorMin = Vector2.zero;
        btnTextRect.anchorMax = Vector2.one;
        btnTextRect.sizeDelta = Vector2.zero;

        // Status text
        GameObject statusObj = new GameObject("StatusText");
        statusObj.transform.SetParent(formContainer.transform, false);
        var statusText = statusObj.AddComponent<Text>();
        statusText.text = "";
        statusText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        statusText.fontSize = 14;
        statusText.color = new Color(1f, 0.8f, 0.3f);
        statusText.alignment = TextAnchor.MiddleCenter;
        var statusRect = statusObj.GetComponent<RectTransform>();
        statusRect.anchorMin = new Vector2(0f, 0f);
        statusRect.anchorMax = new Vector2(1f, 0f);
        statusRect.pivot = new Vector2(0.5f, 0f);
        statusRect.anchoredPosition = new Vector2(0f, 10f);
        statusRect.sizeDelta = new Vector2(0f, 30f);

        // Game UI placeholder (existing UI_Canvas acts as game UI)
        var existingGameCanvas = Object.FindFirstObjectByType<GameManager>()?.transform.root;
        GameObject gameUIObj = GameObject.Find("UI_Canvas");

        // LoginUI component
        var loginUI = loginCanvas.AddComponent<LoginUI>();
        var loginSO = new SerializedObject(loginUI);
        loginSO.FindProperty("usernameField").objectReferenceValue = usernameInput;
        loginSO.FindProperty("passwordField").objectReferenceValue = passwordInput;
        loginSO.FindProperty("loginButton").objectReferenceValue = btn;
        loginSO.FindProperty("statusText").objectReferenceValue = statusText;
        loginSO.FindProperty("loginPanel").objectReferenceValue = loginPanel;
        loginSO.FindProperty("gameUI").objectReferenceValue = gameUIObj;
        loginSO.ApplyModifiedProperties();

        Debug.Log("[Hack & Slash] Network objects created.");
    }

    private static GameObject CreateInputField(Transform parent, string name, string placeholder, Vector2 position)
    {
        GameObject obj = new GameObject(name);
        obj.transform.SetParent(parent, false);
        var bgImage = obj.AddComponent<Image>();
        bgImage.color = new Color(0.25f, 0.25f, 0.25f);
        var rect = obj.GetComponent<RectTransform>();
        rect.anchorMin = new Vector2(0.5f, 1f);
        rect.anchorMax = new Vector2(0.5f, 1f);
        rect.pivot = new Vector2(0.5f, 1f);
        rect.anchoredPosition = position;
        rect.sizeDelta = new Vector2(260f, 35f);

        // Text child
        GameObject textObj = new GameObject("Text");
        textObj.transform.SetParent(obj.transform, false);
        var text = textObj.AddComponent<Text>();
        text.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        text.fontSize = 16;
        text.color = Color.white;
        text.supportRichText = false;
        var textRect = textObj.GetComponent<RectTransform>();
        textRect.anchorMin = Vector2.zero;
        textRect.anchorMax = Vector2.one;
        textRect.offsetMin = new Vector2(10f, 2f);
        textRect.offsetMax = new Vector2(-10f, -2f);

        // Placeholder child
        GameObject phObj = new GameObject("Placeholder");
        phObj.transform.SetParent(obj.transform, false);
        var phText = phObj.AddComponent<Text>();
        phText.text = placeholder;
        phText.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        phText.fontSize = 16;
        phText.fontStyle = FontStyle.Italic;
        phText.color = new Color(0.6f, 0.6f, 0.6f);
        var phRect = phObj.GetComponent<RectTransform>();
        phRect.anchorMin = Vector2.zero;
        phRect.anchorMax = Vector2.one;
        phRect.offsetMin = new Vector2(10f, 2f);
        phRect.offsetMax = new Vector2(-10f, -2f);

        var inputField = obj.AddComponent<InputField>();
        inputField.textComponent = text;
        inputField.placeholder = phText;

        return obj;
    }

    private static void CreateEventSystem()
    {
        // EventSystem with new Input System module (required for UI input)
        if (Object.FindFirstObjectByType<EventSystem>() == null)
        {
            GameObject esObj = new GameObject("EventSystem");
            esObj.AddComponent<EventSystem>();
            esObj.AddComponent<InputSystemUIInputModule>();
            Debug.Log("[Hack & Slash] EventSystem created with InputSystemUIInputModule.");
        }
    }

    private static void BakeNavMesh()
    {
        // Mark ground and obstacles as Navigation Static
        foreach (var go in Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
        {
            if (go.gameObject.isStatic)
            {
                GameObjectUtility.SetStaticEditorFlags(go.gameObject,
                    StaticEditorFlags.NavigationStatic | StaticEditorFlags.BatchingStatic);
            }
        }

        // Bake NavMesh
        UnityEditor.AI.NavMeshBuilder.BuildNavMesh();
        Debug.Log("[Hack & Slash] NavMesh baked.");
    }
}

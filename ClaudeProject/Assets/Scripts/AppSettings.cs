using UnityEngine;

public class AppSettings : MonoBehaviour
{
    private static AppSettings instance;
    private int targetFrameRate = 60;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    private static void Initialize()
    {
        if (instance != null) return;

        var go = new GameObject("AppSettings");
        instance = go.AddComponent<AppSettings>();
        DontDestroyOnLoad(go);
    }

    private void Awake()
    {
        if (instance != null && instance != this)
        {
            Destroy(gameObject);
            return;
        }
        instance = this;

        QualitySettings.vSyncCount = 0;
        Application.targetFrameRate = targetFrameRate;
    }

    private void OnApplicationFocus(bool hasFocus)
    {
        if (hasFocus)
            Application.targetFrameRate = targetFrameRate;
    }
}

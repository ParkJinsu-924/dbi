using UnityEngine;

// Sets process-wide defaults that apply for the entire app lifetime. Drop this
// on a GameObject in the very first (boot) scene. It survives scene transitions
// via DontDestroyOnLoad and uses a low DefaultExecutionOrder so its Awake runs
// before any other component (NetworkManager, gameplay managers, ...).
//
// Currently sets: background execution + frame-rate cap. Other process-wide
// concerns that belong here as the project grows: logging init, analytics,
// locale/localization, audio bus, crash reporter.
[DefaultExecutionOrder(-10000)]
public sealed class ApplicationBootstrap : MonoBehaviour
{
    [Header("Frame rate")]
    [Tooltip("Target FPS cap. -1 = no explicit cap (platform default).")]
    [SerializeField] private int _targetFrameRate = 60;

    [Tooltip("vSync count. 0 = off (targetFrameRate is honored). " +
             "1+ = sync to display refresh (targetFrameRate is ignored).")]
    [SerializeField] private int _vSyncCount = 0;

    private static ApplicationBootstrap _instance;

    private void Awake()
    {
        // Single-instance — duplicates from scene reload are silently destroyed.
        if (_instance != null && _instance != this)
        {
            Destroy(gameObject);
            return;
        }
        _instance = this;
        DontDestroyOnLoad(gameObject);

        // 1. Game loop must keep running when the window is not focused — TCP
        //    recv/send threads + MainThreadDispatcher.Update depend on it.
        Application.runInBackground = true;

        // 2. Cap framerate explicitly so OS background-throttling can't drop us.
        Application.targetFrameRate = _targetFrameRate;

        // 3. Disable vSync so targetFrameRate is honored. With vSync > 0 the
        //    framerate is locked to display refresh, which the OS may throttle
        //    when the window loses focus.
        QualitySettings.vSyncCount = _vSyncCount;
    }
}

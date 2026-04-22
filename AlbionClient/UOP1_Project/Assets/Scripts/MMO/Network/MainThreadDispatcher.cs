using UnityEngine;
using System;
using System.Collections.Concurrent;

public class MainThreadDispatcher : MonoBehaviour
{
    private static MainThreadDispatcher instance;
    private readonly ConcurrentQueue<Action> pendingActions = new ConcurrentQueue<Action>();

    public static MainThreadDispatcher Instance
    {
        get
        {
            if (instance == null)
            {
                var go = new GameObject("MainThreadDispatcher");
                instance = go.AddComponent<MainThreadDispatcher>();
                DontDestroyOnLoad(go);
            }
            return instance;
        }
    }

    public void Enqueue(Action action)
    {
        pendingActions.Enqueue(action);
    }

    // Background 스레드에서도 안전. Instance 가 없으면(플레이 종료 / 언로드 후) 조용히 무시.
    // Instance getter 는 GameObject 를 만들 수 있는데 그건 메인 스레드 전용이라 직접 호출하면 크래시.
    public static void TryEnqueue(Action action)
    {
        var inst = instance;
        if (inst == null) return;
        inst.pendingActions.Enqueue(action);
    }

    private void Update()
    {
        while (pendingActions.TryDequeue(out var action))
        {
            action?.Invoke();
        }
    }

    private void Awake()
    {
        if (instance != null && instance != this)
        {
            Destroy(gameObject);
            return;
        }
        instance = this;
        DontDestroyOnLoad(gameObject);
    }

    private void OnDestroy()
    {
        if (instance == this)
            instance = null;
    }
}

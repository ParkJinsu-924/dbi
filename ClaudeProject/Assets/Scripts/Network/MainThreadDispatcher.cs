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

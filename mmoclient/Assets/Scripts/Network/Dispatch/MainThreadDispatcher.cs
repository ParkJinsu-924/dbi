using System;
using System.Collections.Concurrent;
using UnityEngine;

namespace MMO.Network
{
    // Marshals work from the network background threads onto Unity's main thread (Update).
    // Background callbacks must NEVER touch GameObjects/UnityEngine API directly — they
    // enqueue an Action here and the next Update tick runs it on the main thread.
    //
    // The dispatcher is created lazily; if the scene has none, Instance creates one and
    // marks it DontDestroyOnLoad so it survives scene transitions. TryEnqueue is safe to
    // call after the dispatcher has been destroyed (e.g., on application quit) — it
    // becomes a silent no-op so trailing recv-thread events don't crash the editor.
    [DefaultExecutionOrder(-10000)]
    public sealed class MainThreadDispatcher : MonoBehaviour
    {
        private static MainThreadDispatcher _instance;
        private static volatile bool _quitting;

        // Unbounded queue. Network throughput is bounded by recv/send threads which throttle
        // naturally on socket capacity, so the queue cannot grow without bound under normal play.
        private readonly ConcurrentQueue<Action> _queue = new();

        // Cap items processed per Update so a flood of packets cannot stall a frame indefinitely.
        // Server normal traffic is well under this (10Hz position broadcast + occasional events).
        private const int MaxActionsPerFrame = 256;

        public static MainThreadDispatcher Instance
        {
            get
            {
                if (_quitting) return null;
                if (_instance != null) return _instance;
                var existing = FindAnyObjectByType<MainThreadDispatcher>();
                if (existing != null)
                {
                    _instance = existing;
                    return _instance;
                }
                var go = new GameObject("MainThreadDispatcher");
                _instance = go.AddComponent<MainThreadDispatcher>();
                DontDestroyOnLoad(go);
                return _instance;
            }
        }

        // Background-safe enqueue. Returns false if the dispatcher is gone (quit/domain reload).
        public static bool TryEnqueue(Action action)
        {
            if (action == null) return false;
            var inst = _instance;
            if (inst == null) return false;
            inst._queue.Enqueue(action);
            return true;
        }

        // Main-thread enqueue. Cheaper because it skips the volatile read; only use from
        // code that knows the dispatcher exists.
        public void Enqueue(Action action)
        {
            if (action == null) return;
            _queue.Enqueue(action);
        }

        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }
            _instance = this;
            DontDestroyOnLoad(gameObject);
            Application.quitting += OnApplicationQuitting;
        }

        private void OnDestroy()
        {
            Application.quitting -= OnApplicationQuitting;
            if (_instance == this) _instance = null;
        }

        private void OnApplicationQuitting()
        {
            _quitting = true;
        }

        private void Update()
        {
            int budget = MaxActionsPerFrame;
            while (budget-- > 0 && _queue.TryDequeue(out var action))
            {
                try { action(); }
                catch (Exception ex)
                {
                    // A handler that throws must not break the dispatch loop.
                    Debug.LogException(ex);
                }
            }
        }
    }
}

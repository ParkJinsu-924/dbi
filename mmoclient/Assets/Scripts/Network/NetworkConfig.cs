using UnityEngine;

namespace MMO.Network
{
    // Per-environment endpoint and tuning knobs. Exposed via a ScriptableObject so QA can
    // ship dev/staging/prod variants without code changes; NetworkManager falls back to a
    // runtime default if no asset is wired up.
    [CreateAssetMenu(fileName = "NetworkConfig", menuName = "MMO/Network Config", order = 100)]
    public sealed class NetworkConfig : ScriptableObject
    {
        [Header("Login Server")]
        public string LoginHost = "127.0.0.1";
        public int LoginPort = 9999;

        [Header("Game Server (fallback when S_Login carries no address)")]
        public string GameHostFallback = "127.0.0.1";
        public int GamePortFallback = 7777;

        [Header("Buffers (per session)")]
        [Tooltip("Receive ring buffer size in bytes. Must be larger than the biggest packet the server can send.")]
        public int RecvBufferSize = 65536;

        [Tooltip("Maximum payload size the client will accept. Hard guard against framing corruption.")]
        public int MaxPayloadSize = 65535 - 6;

        [Tooltip("Outgoing packets that may queue before Send() returns false (back-pressure).")]
        public int SendQueueCapacity = 1024;

        [Header("Timeouts (seconds)")]
        public float ConnectTimeout = 5f;
        public float LoginResponseTimeout = 10f;

        [Header("Diagnostics")]
        [Tooltip("Log every packet send/recv with id+size. Verbose — leave off in production builds.")]
        public bool LogPackets = false;

        [Tooltip("Log connect / disconnect / login transitions.")]
        public bool LogLifecycle = true;

        public static NetworkConfig CreateDefault()
        {
            var inst = CreateInstance<NetworkConfig>();
            inst.name = "NetworkConfig (Runtime Default)";
            return inst;
        }
    }
}

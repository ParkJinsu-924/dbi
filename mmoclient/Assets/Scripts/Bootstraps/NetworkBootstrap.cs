using UnityEngine;

namespace MMO.Network
{
    // Drop this on a single GameObject in the boot scene next to NetworkManager.
    // It auto-issues a login at Start so the dev/QA build can verify the pipe end-to-end
    // without a UI. Disable autoLogin and call NetworkManager.Instance.DoLogin from your
    // login UI when wiring up real flows.
    [RequireComponent(typeof(NetworkManager))]
    public sealed class NetworkBootstrap : MonoBehaviour
    {
        [Header("Auto Login (debug)")]
        public bool autoLogin = true;
        public string username = "test";
        public string password = "test";

        private void Start()
        {
            if (!autoLogin) return;
            var net = NetworkManager.Instance;
            if (net == null)
            {
                Debug.LogError("[NetworkBootstrap] NetworkManager.Instance is null");
                return;
            }
            net.OnLoginConnectionFailed += msg => Debug.LogError($"[Boot] login conn failed: {msg}");
            net.OnLoginFailed += err => Debug.LogError($"[Boot] login rejected: code={err.Code} {err.Detail}");
            net.OnLoginSucceeded += () => Debug.Log("[Boot] login OK — connecting to GameServer");
            net.OnEnteredGame += s => Debug.Log($"[Boot] entered game playerId={s.PlayerId}");
            net.OnGameDisconnected += r => Debug.Log($"[Boot] game disconnected: {r}");

            net.DoLogin(username, password);
        }
    }
}

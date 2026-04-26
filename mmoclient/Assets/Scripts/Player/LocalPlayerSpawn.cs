using MMO.Network;
using Proto;
using UnityEngine;
using UnityEngine.AI;

namespace MMO.Player
{
    [RequireComponent(typeof(NavMeshAgent))]
    public sealed class LocalPlayerSpawn : MonoBehaviour
    {
        private NavMeshAgent _agent;

        private void Awake() => _agent = GetComponent<NavMeshAgent>();

        private void OnEnable()
        {
            var nm = NetworkManager.Instance;
            if (nm == null) return;

            nm.OnEnteredGame += OnEnteredGame;

            if (nm.IsInGame && nm.Game != null)
                Apply(nm.Game.LocalSpawnPosition);
        }

        private void OnDisable()
        {
            var nm = NetworkManager.Instance;
            if (nm == null) return;
            nm.OnEnteredGame -= OnEnteredGame;
        }

        private void OnEnteredGame(S_EnterGame msg)
        {
            var p = msg.SpawnPosition;
            Apply(new Vector3(p?.X ?? 0f, 0f, p?.Y ?? 0f));
        }

        private void Apply(Vector3 pos)
        {
            // NavMeshAgent.Warp resets path/velocity and snaps to NavMesh.
            // Setting transform.position directly leaves the agent confused.
            _agent.Warp(pos);
        }
    }
}

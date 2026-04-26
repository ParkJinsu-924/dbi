using MMO.Network;

// Static façade for the network layer — one entry point for the four things
// gameplay code touches every day:
//
//   Net.Handlers.OnUnitHp += handler;          // subscribe to a server-push event
//   Net.Senders.SendMoveCommand(target);       // send a packet (null-safe internally)
//   Net.Game?.IsInGame                         // game session state introspection
//   Net.Login?.State                           // login session state introspection
//
// `Handlers` and `Senders` are process-lifetime singletons that NEVER return null.
// Subscribers and senders work at any moment — pre-login, in-game, or during a
// brief disconnect — without `?.` ceremony.
//   • Handlers : GameSession reuses this same instance and binds its events to
//                its private PacketRouter via HandlerAutoRegistrar. Logout/reconnect
//                does NOT invalidate existing subscribers.
//   • Senders  : looks up the current target session internally on each call. If
//                no session exists, sends are a soft drop returning false rather
//                than throwing.
//
// `Game` and `Login` are genuinely nullable — the corresponding Session does not
// exist yet (pre-login) or after disposal. They exist for callers that need
// session-state introspection (IsInGame, LocalPlayerId, State, Tcp.Stats, ...).
// For sending, prefer `Net.Senders` — there is no need to write `Net.Game?.SendXxx`.
public static class Net
{
    // App-lifetime event hub. Never null. Safe to subscribe pre-login or post-disconnect.
    public static PacketHandlers Handlers { get; } = new();

    // App-lifetime sender façade. Never null. Each method internally resolves the
    // target session and returns false if none exists.
    public static PacketSenders  Senders  { get; } = new();

    // Null until login flow produces the corresponding Session. Use for state
    // introspection only — for sending, prefer `Net.Senders`.
    public static GameSession    Game     => NetworkManager.Instance?.Game;
    public static LoginSession   Login    => NetworkManager.Instance?.Login;
}

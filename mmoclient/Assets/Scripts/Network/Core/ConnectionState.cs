namespace MMO.Network
{
    public enum ConnectionState
    {
        Disconnected = 0,
        Connecting   = 1,
        Connected    = 2,
        Closing      = 3,
    }

    // Reason surfaced via OnDisconnected so UI can react differently to user logout vs. error.
    public enum DisconnectReason
    {
        Local             = 0,   // local Close() — clean shutdown
        RemoteClosed      = 1,   // server shut the connection (FIN)
        ConnectFailed     = 2,   // could not establish TCP
        SendError         = 3,   // I/O failure during send
        RecvError         = 4,   // I/O failure during recv
        ProtocolError     = 5,   // malformed packet (size/id) — stream is unrecoverable
        Timeout           = 6,
    }
}

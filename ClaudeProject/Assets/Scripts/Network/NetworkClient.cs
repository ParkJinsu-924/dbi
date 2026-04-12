using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using Google.Protobuf;

public class NetworkClient : IDisposable
{
    private const int HEADER_SIZE = 6; // uint16 size + uint32 id
    private const int RECV_BUFFER_SIZE = 65536;

    private TcpClient tcpClient;
    private NetworkStream stream;
    private Thread recvThread;
    private volatile bool isRunning;

    private readonly byte[] recvBuffer = new byte[RECV_BUFFER_SIZE];
    private int recvBufferOffset;

    private readonly object sendLock = new object();

    public event Action<uint, byte[]> OnPacketReceived;
    public event Action OnDisconnected;

    public bool IsConnected => tcpClient != null && tcpClient.Connected && isRunning;

    public void Connect(string ip, int port)
    {
        try
        {
            tcpClient = new TcpClient();
            tcpClient.NoDelay = true;
            tcpClient.Connect(ip, port);
            stream = tcpClient.GetStream();
            isRunning = true;
            recvBufferOffset = 0;

            recvThread = new Thread(RecvLoop)
            {
                IsBackground = true,
                Name = "NetworkRecv"
            };
            recvThread.Start();
        }
        catch (Exception e)
        {
            UnityEngine.Debug.LogError("[NetworkClient] Connect failed: " + e.Message);
            Cleanup();
            throw;
        }
    }

    public void Send<T>(T message) where T : IMessage
    {
        if (!IsConnected) return;

        try
        {
            var field = typeof(T).GetField("PacketId");
            if (field == null)
            {
                UnityEngine.Debug.LogError("[NetworkClient] No PacketId on " + typeof(T).Name);
                return;
            }
            uint packetId = (uint)field.GetValue(null);

            byte[] payload = message.ToByteArray();
            int totalSize = HEADER_SIZE + payload.Length;

            byte[] packet = new byte[totalSize];

            // Header: uint16 size (LE) + uint32 id (LE)
            packet[0] = (byte)(totalSize & 0xFF);
            packet[1] = (byte)((totalSize >> 8) & 0xFF);

            packet[2] = (byte)(packetId & 0xFF);
            packet[3] = (byte)((packetId >> 8) & 0xFF);
            packet[4] = (byte)((packetId >> 16) & 0xFF);
            packet[5] = (byte)((packetId >> 24) & 0xFF);

            Buffer.BlockCopy(payload, 0, packet, HEADER_SIZE, payload.Length);

            lock (sendLock)
            {
                stream.Write(packet, 0, packet.Length);
            }
        }
        catch (Exception e)
        {
            UnityEngine.Debug.LogError("[NetworkClient] Send failed: " + e.Message);
            HandleDisconnect();
        }
    }

    private void RecvLoop()
    {
        try
        {
            while (isRunning)
            {
                int bytesRead = stream.Read(recvBuffer, recvBufferOffset, RECV_BUFFER_SIZE - recvBufferOffset);
                if (bytesRead <= 0)
                {
                    HandleDisconnect();
                    return;
                }

                recvBufferOffset += bytesRead;
                ProcessRecvBuffer();
            }
        }
        catch (Exception)
        {
            if (isRunning)
                HandleDisconnect();
        }
    }

    private void ProcessRecvBuffer()
    {
        while (recvBufferOffset >= HEADER_SIZE)
        {
            ushort packetSize = (ushort)(recvBuffer[0] | (recvBuffer[1] << 8));

            if (packetSize < HEADER_SIZE || packetSize > RECV_BUFFER_SIZE)
            {
                UnityEngine.Debug.LogError("[NetworkClient] Invalid packet size: " + packetSize);
                HandleDisconnect();
                return;
            }

            if (recvBufferOffset < packetSize)
                return;

            uint packetId = (uint)(recvBuffer[2] | (recvBuffer[3] << 8) |
                                   (recvBuffer[4] << 16) | (recvBuffer[5] << 24));

            int payloadSize = packetSize - HEADER_SIZE;
            byte[] payload = new byte[payloadSize];
            Buffer.BlockCopy(recvBuffer, HEADER_SIZE, payload, 0, payloadSize);

            int remaining = recvBufferOffset - packetSize;
            if (remaining > 0)
                Buffer.BlockCopy(recvBuffer, packetSize, recvBuffer, 0, remaining);
            recvBufferOffset = remaining;

            OnPacketReceived?.Invoke(packetId, payload);
        }
    }

    private void HandleDisconnect()
    {
        if (!isRunning) return;
        isRunning = false;

        MainThreadDispatcher.Instance.Enqueue(() =>
        {
            OnDisconnected?.Invoke();
        });

        Cleanup();
    }

    public void Disconnect()
    {
        isRunning = false;
        Cleanup();
    }

    private void Cleanup()
    {
        try { stream?.Close(); } catch { }
        try { tcpClient?.Close(); } catch { }
        stream = null;
        tcpClient = null;
    }

    public void Dispose()
    {
        Disconnect();
    }
}

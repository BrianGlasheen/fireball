#pragma once

#include "networking.h"
#include "network_protocol.h"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <string>
#include <functional>

class Client {
public:
    std::function<void()> on_connected;
    std::function<void()> on_disconnected;
    std::function<void(const uint8_t*, size_t)> on_snapshot;

    bool connect(const char* ip, uint16_t port, const std::string& name) {
        // TODO make sure only called once
        // and or deinit on loss of connection
        InitSteamDatagramConnectionSockets();

        m_name = name;
        m_sockets = SteamNetworkingSockets();

        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.ParseString(ip);
        addr.m_port = port;

        SteamNetworkingConfigValue_t opt;
        opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)connection_status_changed_static);

        m_conn = m_sockets->ConnectByIPAddress(addr, 1, &opt);
        if (m_conn == k_HSteamNetConnection_Invalid) {
            printf("[CLIENT] Failed to connect to %s:%d\n", ip, port);
            return false;
        }

        s_instance = this;
        return true;
    }

    void disconnect() {
        send_packet({ Net_Msg::ClientLeaving, {} });
        m_sockets->CloseConnection(m_conn, 0, "Leaving", true);
        // TODO disable steam datagram sockets
    }

    void tick() {
        if (m_conn != k_HSteamNetConnection_Invalid) {
            poll_messages();
            m_sockets->RunCallbacks();
        }
    }

private:
    ISteamNetworkingSockets* m_sockets = nullptr;
    HSteamNetConnection m_conn    = k_HSteamNetConnection_Invalid;
    std::string m_name;
    static Client* s_instance;

    void poll_messages() {
        while (true) {
            ISteamNetworkingMessage* msg = nullptr;
            int count = m_sockets->ReceiveMessagesOnConnection(m_conn, &msg, 1);
            if (count <= 0) break;

            handle_message(static_cast<const uint8_t*>(msg->m_pData), msg->m_cbSize);
            msg->Release();
        }
    }

    void handle_message(const uint8_t* data, size_t size) {
        NetPacket pkt;
        if (!NetPacket::deserialize(data, size, pkt)) return;

        switch (pkt.type) {
            case Net_Msg::ClientAccepted: {

                Client_Accepted msg;
                bool fail = NetPacket::to(pkt, msg);
                msg.server_name[sizeof(msg.server_name) - 1] = '\0';

                printf("Connected to %s\nID %d, tickrate %d/s\n",msg.server_name, msg.your_id, msg.tick_rate);

                break;
            }

            case Net_Msg::FullSnapshot:
                printf("[CLIENT] Got snapshot (%zu bytes)\n", pkt.payload.size());
                if (on_snapshot)
                    on_snapshot(pkt.payload.data(), pkt.payload.size());

                break;

            case Net_Msg::DeltaUpdate:
                // TODO: apply delta
                break;

            case Net_Msg::ClientLeaving:
                printf("[CLIENT] Server is shutting down\n");
                if (on_disconnected) on_disconnected();
                break;            
                
            case Net_Msg::ServerShutdown: {
                Server_Shutdown s;
                int fail = NetPacket::to(pkt, s);
                s.text[sizeof(s.text) - 1] = '\0';
                
                if (fail)
                    printf("Server is shutting down\nUnknown reason\n");
                else
                    printf("Server is shutting down\n%s", s.text);

                if (on_disconnected) on_disconnected();
                break;
            }

            default:
                printf("[CLIENT] Unknown msg type %d\n", (int)pkt.type);
                break;
        }
    }

    void on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
        switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connected: {
                printf("[CLIENT] Connected, sending hello\n");

                Client_Hello msg = { "fireball-client01" } ;
                NetPacket hello = NetPacket::from(Net_Msg::ClientHello, msg);

                send_packet(hello);

                if (on_connected)
                    on_connected();
                break;
            }

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                printf("[CLIENT] Disconnected: %s\n", info->m_info.m_szEndDebug);
                if (on_disconnected) on_disconnected();
                break;

            default:
                break;
        }
    }

    void send_packet(const NetPacket& pkt) {
        auto buf = pkt.serialize();
        m_sockets->SendMessageToConnection(m_conn, buf.data(), (uint32_t)buf.size(), k_nSteamNetworkingSend_Reliable, nullptr);
    }

    static void connection_status_changed_static(SteamNetConnectionStatusChangedCallback_t* info) {
        s_instance->on_connection_status_changed(info);
    }
};

inline Client* Client::s_instance = nullptr;

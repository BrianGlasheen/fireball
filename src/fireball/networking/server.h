#pragma once

#include "networking.h"
#include "network_protocol.h"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>

struct ClientState {
    HSteamNetConnection conn;
    bool fully_loaded = false; // received snapshot and acked
    std::string name;
};

class Server {
public:
    std::function<std::vector<uint8_t>()> on_full_snapshot;
    std::function<std::vector<uint8_t>()> on_delta_update; 
    std::function<void(HSteamNetConnection, const std::string&)> on_client_joined;
    std::function<void(HSteamNetConnection)> on_client_left;
    std::function<void(HSteamNetConnection, const uint8_t*, size_t)> on_client_input;

    bool start(uint16_t port) {
        InitSteamDatagramConnectionSockets();

        m_sockets = SteamNetworkingSockets();

        SteamNetworkingIPAddr addr;
        addr.Clear();
        addr.m_port = port;

        SteamNetworkingConfigValue_t opt;
        opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                   (void*)connection_status_changed_static);

        m_listen_socket = m_sockets->CreateListenSocketIP(addr, 1, &opt);
        if (m_listen_socket == k_HSteamListenSocket_Invalid) {
            Printf("Failed to create listen socket on port %d", port);
            return false;
        }

        m_poll_group = m_sockets->CreatePollGroup();
        Printf("Server listening on port %d", port);
        s_instance = this;
        return true;
    }

    void stop() {
        for (auto& [conn, state] : m_clients) {
            send_packet(conn, { NetMsg::ClientLeaving, {} });
            m_sockets->CloseConnection(conn, 0, "Server shutdown, reason", true);
        }

        m_clients.clear();
        m_sockets->CloseListenSocket(m_listen_socket);
        m_sockets->DestroyPollGroup(m_poll_group);
    }

    void tick() {
        poll_messages();
        poll_connection_state_changes();
    }

    void broadcast_delta() {
        if (!on_delta_update) return;
        auto delta = on_delta_update();
        if (delta.empty()) return;

        NetPacket pkt { NetMsg::DeltaUpdate, delta };
        for (auto& [conn, state] : m_clients) {
            if (state.fully_loaded)
                send_packet(conn, pkt);
        }
    }

    void send_to(HSteamNetConnection conn, const NetPacket& pkt) {
        send_packet(conn, pkt);
    }

    void kick(HSteamNetConnection conn, const char* reason = "Kicked") {
        m_sockets->CloseConnection(conn, 0, reason, false);
    }

    size_t client_count() const { return m_clients.size(); }

private:
    ISteamNetworkingSockets* m_sockets     = nullptr;
    HSteamListenSocket m_listen_socket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup m_poll_group  = k_HSteamNetPollGroup_Invalid;
    std::unordered_map<HSteamNetConnection, ClientState> m_clients;

    static Server* s_instance;

    void poll_messages() {
        while (true) {
            ISteamNetworkingMessage* msg = nullptr;
            int count = m_sockets->ReceiveMessagesOnPollGroup(m_poll_group, &msg, 1);
            if (count <= 0) break;

            handle_message(msg->m_conn, static_cast<const uint8_t*>(msg->m_pData), msg->m_cbSize);
            msg->Release();
        }
    }

    void poll_connection_state_changes() {
        m_sockets->RunCallbacks();
    }

    void handle_message(HSteamNetConnection conn, const uint8_t* data, size_t size) {
        NetPacket pkt;
        if (!NetPacket::deserialize(data, size, pkt)) {
            Printf("Malformed packet from connection %u", conn);
            return;
        }

        switch (pkt.type) {
            case NetMsg::ClientHello: {
                Client_Hello msg;
                bool fail = NetPacket::to(pkt, msg);
                msg.name[sizeof(msg.name) - 1] = '\0';

                printf("Client %s joined\n", msg.name);

                auto& state = m_clients[conn];
                state.name = std::string(msg.name);

                Client_Accepted sinfo { .server_name = "fireball-server", .your_id = conn, .tick_rate = 128 };
                NetPacket packet = NetPacket::from(NetMsg::ClientAccepted, sinfo);
                send_packet(conn, packet, k_nSteamNetworkingSend_Reliable);

                if (on_full_snapshot) {
                    auto snapshot = on_full_snapshot();
                    NetPacket packet { NetMsg::FullSnapshot, snapshot };
                    printf("sending scene snapshot %zu bytes", snapshot.size());
                    send_packet(conn, packet, k_nSteamNetworkingSend_Reliable);
                }

                state.fully_loaded = true;
                if (on_client_joined) on_client_joined(conn, state.name);
                break;
            }

            case NetMsg::ClientInput: {
                if (on_client_input)
                    on_client_input(conn, pkt.payload.data(), pkt.payload.size());
                break;
            }

            case NetMsg::ClientLeaving: {
                Printf("Client %u sent graceful leave", conn);
                remove_client(conn);
                m_sockets->CloseConnection(conn, 0, "Client left", false);
                break;
            }

            default:
                Printf("Unknown message type %d from %u", static_cast<int>(pkt.type), conn);
                break;
        }
    }

    void on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
        switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting: {
                Printf("Incoming connection from %s",info->m_info.m_szConnectionDescription);
                if (m_sockets->AcceptConnection(info->m_hConn) != k_EResultOK) {
                    m_sockets->CloseConnection(info->m_hConn, 0, "Accept failed", false);
                    break;
                }
                m_sockets->SetConnectionPollGroup(info->m_hConn, m_poll_group);

                m_clients[info->m_hConn] = { info->m_hConn };
                break;
            }

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
                if (m_clients.count(info->m_hConn)) {
                    Printf("Connection %u lost: %s", info->m_hConn, info->m_info.m_szEndDebug);
                    remove_client(info->m_hConn);
                    m_sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
                }
                break;
            }

            default:
                break;
        }
    }

    void remove_client(HSteamNetConnection conn) {
        if (on_client_left)
            on_client_left(conn);
        m_clients.erase(conn);
    }

    void send_packet(HSteamNetConnection conn, const NetPacket& pkt, int send_flags = k_nSteamNetworkingSend_Reliable) {
        auto buf = pkt.serialize();
        m_sockets->SendMessageToConnection(conn, buf.data(), static_cast<uint32_t>(buf.size()), send_flags, nullptr);
    }

    static void connection_status_changed_static(
        SteamNetConnectionStatusChangedCallback_t* info) {
        s_instance->on_connection_status_changed(info);
    }
};

inline Server* Server::s_instance = nullptr;

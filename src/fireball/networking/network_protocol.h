#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// TODO network structures and settings
struct Client_Accepted {
    char server_name[32];
    uint32_t your_id;
    uint32_t tick_rate = 128;
};

struct Client_Hello {
    char name[32];
};

struct Server_Shutdown {
    char text[1024];
};

enum class Net_Msg : uint8_t {
    // Server -> Client
    FullSnapshot    = 1,
    DeltaUpdate     = 2,
    EntityDestroyed = 3,
    ClientAccepted  = 4,
    ServerShutdown = 5,

    // Client -> Server
    ClientHello     = 10,
    ClientInput     = 11,
    ClientLeaving   = 12,
};

struct NetPacket {
    Net_Msg type;
    std::vector<uint8_t> payload;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> out; // make sizeof payload maybe or stack buffer
        out.push_back(static_cast<uint8_t>(type));
        uint32_t len = static_cast<uint32_t>(payload.size());
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&len),
                              reinterpret_cast<uint8_t*>(&len) + 4);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    static bool deserialize(const uint8_t* data, size_t size, NetPacket& out) {
        if (size < 5) return false;
        out.type = static_cast<Net_Msg>(data[0]);
        uint32_t len;
        memcpy(&len, data + 1, 4);
        if (size < 5 + len) return false;
        out.payload.assign(data + 5, data + 5 + len);
        return true;
    }

    // simple serialization of single, constant sized,
    // structs based on msg type
    template<typename T>
    static NetPacket from(Net_Msg type, const T& s) {
        std::vector<uint8_t> payload(sizeof(T));
        memcpy(payload.data(), &s, sizeof(T));
        return { type, payload };
    }

    template<typename T>
    static bool to(const NetPacket& pkt, T& out) {
        if (pkt.payload.size() < sizeof(T)) return false;
        memcpy(&out, pkt.payload.data(), sizeof(T));
        return true;
    }
};

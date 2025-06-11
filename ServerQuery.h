#pragma once

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <functional>


#define A2S_INFO            0x54
#define A2S_PLAYER          0x55
#define A2S_RULES           0x56
#define A2S_PING            0x69


#define MASTER_SERVER_IP    "208.64.200.52"
#define MASTER_SERVER_PORT  27011


#define DAYZ_APP_ID         221100
#define DAYZ_QUERY_PORT     2302

struct ServerQuery {
    uint8_t header;
    const char* payload;
    size_t payloadSize;
};

struct A2SInfoResponse {
    uint8_t protocol;
    std::string name;
    std::string map;
    std::string folder;
    std::string game;
    uint16_t id;
    uint8_t players;
    uint8_t maxPlayers;
    uint8_t bots;
    uint8_t serverType;
    uint8_t environment;
    uint8_t visibility;
    uint8_t vac;
    std::string version;
    uint8_t edf;
    uint16_t port;
    uint64_t steamId;
    std::string keywords;
    std::string gameId;
};

struct A2SPlayerResponse {
    struct Player {
        uint8_t index;
        std::string name;
        int32_t score;
        float duration;
    };

    uint8_t playerCount;
    std::vector<Player> players;
};

struct A2SRulesResponse {
    struct Rule {
        std::string name;
        std::string value;
    };

    uint16_t ruleCount;
    std::vector<Rule> rules;
};


struct ServerInfo {
    std::string name;
    std::string map;
    std::string ip;
    int port;
    int players;
    int maxPlayers;
    int ping;
    bool isOfficial;
    bool isFavorite;
    bool isPassworded;
    bool hasVAC;
    bool hasAntiCheat;
    std::string gameMode;
    std::string version;
    std::string folder;
    std::string timeOfDay;
    std::string weather;
    time_t lastUpdated;
    std::vector<std::string> tags;
    std::vector<std::string> mods;


    float tickRate;
    int uptime;


    std::string country;
    std::string region;

 
    ServerInfo() : port(0), players(0), maxPlayers(0), ping(-1),
        isOfficial(false), isFavorite(false), isPassworded(false),
        hasVAC(false), hasAntiCheat(false), lastUpdated(0),
        tickRate(0.0f), uptime(0) {
    }


    bool operator<(const ServerInfo& other) const {
        return name < other.name;
    }

    bool operator==(const ServerInfo& other) const {
        return ip == other.ip && port == other.port;
    }


    std::string toJson() const;
    static ServerInfo fromJson(const std::string& json);


    std::string getAddressString() const {
        return ip + ":" + std::to_string(port);
    }

    bool isOnline() const {
        return ping != -1;
    }

    bool isFull() const {
        return players >= maxPlayers && maxPlayers > 0;
    }

    float getPlayerRatio() const {
        return maxPlayers > 0 ? static_cast<float>(players) / maxPlayers : 0.0f;
    }
};

class ServerQueryManager {
private:
    SOCKET udpSocket;
    bool initialized;
    std::chrono::milliseconds defaultTimeout{ 5000 };


public:
    ServerQueryManager();
    ~ServerQueryManager();
    bool QueryAllRegions(std::vector<std::pair<std::string, int>>& servers);
    bool QueryAlternativeMasterServers(std::vector<std::pair<std::string, int>>& servers);
    bool QuerySteamMasterServerDirect(std::vector<std::pair<std::string, int>>& servers);
    bool QueryMasterServerByRegion(uint8_t region, std::vector<std::pair<std::string, int>>& servers);
    bool QuerySpecificMasterServer(const std::string& masterIP, int masterPort, std::vector<std::pair<std::string, int>>& servers);
    bool IsLANAddress(const std::string& ip);
    bool QueryMultipleMasterServers(std::vector<std::pair<std::string, int>>& servers);
    bool QueryMasterServerFromStart(const std::string& startAddr, std::vector<std::pair<std::string, int>>& servers);
    std::vector<std::pair<std::string, int>> GetLANServers();
    bool Initialize();
    void Cleanup();

    bool QueryServerInfo(const std::string& ip, int port, A2SInfoResponse& response);
    bool QueryPlayerList(const std::string& ip, int port, A2SPlayerResponse& response);
    bool QueryServerRules(const std::string& ip, int port, A2SRulesResponse& response);
    int PingServer(const std::string& ip, int port);
    bool QuerySingleBatch(const std::string& startAddr, std::vector<std::pair<std::string, int>>& servers);

 
    bool QuerySteamMasterServer(std::vector<std::pair<std::string, int>>& servers);

    bool QueryMasterServerRegion(const std::string& region, std::vector<std::pair<std::string, int>>& servers);

 
    bool GetCompleteServerInfo(const std::string& ip, int port, ServerInfo& info);
    bool GetBasicServerInfo(const std::string& ip, int port, ServerInfo& info);

  
    void QueryMultipleServers(const std::vector<std::pair<std::string, int>>& addresses,
        std::vector<ServerInfo>& results);


    void SetTimeout(std::chrono::milliseconds timeout) { defaultTimeout = timeout; }
    std::chrono::milliseconds GetTimeout() const { return defaultTimeout; }


    std::vector<std::pair<std::string, int>> DiscoverLANServers();

private:

    bool SendQuery(const std::string& ip, int port, const ServerQuery& query,
        std::vector<uint8_t>& response, int timeoutMs = 5000);
    bool SendQueryWithChallenge(const std::string& ip, int port, const ServerQuery& query,
        std::vector<uint8_t>& response, int timeoutMs = 5000);


    void ParseA2SInfo(const std::vector<uint8_t>& data, A2SInfoResponse& response);
    void ParseA2SPlayer(const std::vector<uint8_t>& data, A2SPlayerResponse& response);
    void ParseA2SRules(const std::vector<uint8_t>& data, A2SRulesResponse& response);
    void ParseMasterServerResponse(const std::vector<uint8_t>& data,
        std::vector<std::pair<std::string, int>>& servers);


    std::string ReadNullTerminatedString(const std::vector<uint8_t>& data, size_t& offset);
    uint8_t ReadUint8(const std::vector<uint8_t>& data, size_t& offset);
    uint16_t ReadUint16(const std::vector<uint8_t>& data, size_t& offset);
    uint32_t ReadUint32(const std::vector<uint8_t>& data, size_t& offset);
    uint64_t ReadUint64(const std::vector<uint8_t>& data, size_t& offset);
    float ReadFloat(const std::vector<uint8_t>& data, size_t& offset);


    void LogError(const std::string& message);
    bool IsValidResponse(const std::vector<uint8_t>& data, uint8_t expectedType);


    bool SetSocketTimeout(SOCKET sock, int timeoutMs);
    bool SetSocketNonBlocking(SOCKET sock, bool nonBlocking);
    std::string GetLastSocketError();

  
    bool GetChallenge(const std::string& ip, int port, uint32_t& challenge);
    uint32_t lastChallenge = 0;
};


namespace ServerUtils {
    bool IsOfficialServer(const ServerInfo& server);
    bool IsModdedServer(const ServerInfo& server);
    std::string GetPingCategory(int ping);
    std::string GetPlayerCountCategory(const ServerInfo& server);
    std::string FormatUptime(int seconds);
    std::string FormatLastSeen(time_t timestamp);
    std::vector<std::string> ParseServerTags(const std::string& tags);
    std::string GetCountryFromIP(const std::string& ip);
}
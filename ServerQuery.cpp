#include "ServerQuery.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <algorithm> 

ServerQueryManager::ServerQueryManager() : udpSocket(INVALID_SOCKET), initialized(false) {}

ServerQueryManager::~ServerQueryManager() {
    Cleanup();
}

bool ServerQueryManager::Initialize() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }


    DWORD timeout = 5000; // 5 seconds
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(udpSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    initialized = true;
    return true;
}

void ServerQueryManager::Cleanup() {
    if (udpSocket != INVALID_SOCKET) {
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
    }
    if (initialized) {
        WSACleanup();
        initialized = false;
    }
}

bool ServerQueryManager::QueryServerInfo(const std::string& ip, int port, A2SInfoResponse& response) {
    if (!initialized) return false;

    response = A2SInfoResponse{};

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
        LogError("Invalid IP address: " + ip);
        return false;
    }

    std::vector<uint8_t> initialQuery = {
        0xFF, 0xFF, 0xFF, 0xFF,  // Header
        A2S_INFO,                // Query type (0x54)
        'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00
    };


    if (sendto(udpSocket, (char*)initialQuery.data(), static_cast<int>(initialQuery.size()), 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogError("Failed to send initial query to " + ip + ":" + std::to_string(port));
        return false;
    }


    std::vector<uint8_t> buffer(1400);
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);

    DWORD timeout = 5000;
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
        (sockaddr*)&fromAddr, &fromLen);

    if (bytesReceived <= 0) {
        LogError("No response from " + ip + ":" + std::to_string(port));
        return false;
    }

    buffer.resize(bytesReceived);


    if (bytesReceived == 9 && buffer.size() >= 9 &&
        buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF && buffer[3] == 0xFF &&
        buffer[4] == 0x41) { // 0x41 = A2S_INFO_CHALLENGE

        LogError("Received challenge response from " + ip + ":" + std::to_string(port));


        uint32_t challenge = *reinterpret_cast<const uint32_t*>(&buffer[5]);


        std::vector<uint8_t> challengeQuery = {
            0xFF, 0xFF, 0xFF, 0xFF,  // Header
            A2S_INFO,                // Query type (0x54)
            'S', 'o', 'u', 'r', 'c', 'e', ' ', 'E', 'n', 'g', 'i', 'n', 'e', ' ', 'Q', 'u', 'e', 'r', 'y', 0x00
        };


        challengeQuery.insert(challengeQuery.end(),
            reinterpret_cast<const uint8_t*>(&challenge),
            reinterpret_cast<const uint8_t*>(&challenge) + sizeof(challenge));


        if (sendto(udpSocket, (char*)challengeQuery.data(), static_cast<int>(challengeQuery.size()), 0,
            (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            LogError("Failed to send challenge query to " + ip + ":" + std::to_string(port));
            return false;
        }


        bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived <= 0) {
            LogError("No response to challenge query from " + ip + ":" + std::to_string(port));
            return false;
        }

        buffer.resize(bytesReceived);
    }


    if (fromAddr.sin_addr.s_addr == serverAddr.sin_addr.s_addr &&
        buffer.size() >= 6 &&
        buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF && buffer[3] == 0xFF &&
        buffer[4] == 0x49) {

        ParseA2SInfo(buffer, response);

        if (!response.name.empty() && response.name != "Parse Error") {
            LogError("Successfully queried " + ip + ":" + std::to_string(port) + " - " + response.name);
            return true;
        }
    }

    LogError("Invalid or corrupted response from " + ip + ":" + std::to_string(port));
    return false;
}



void ServerQueryManager::ParseA2SInfo(const std::vector<uint8_t>& data, A2SInfoResponse& response) {
    response = A2SInfoResponse{};

    if (data.size() < 10) {
        LogError("A2S response too short: " + std::to_string(data.size()) + " bytes");
        return;
    }

    size_t offset = 0;


    if (data.size() < 4 ||
        data[0] != 0xFF || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF) {
        LogError("Invalid A2S header");
        return;
    }
    offset = 4;


    if (offset >= data.size()) return;
    uint8_t responseType = data[offset++];
    if (responseType != 0x49) {
        LogError("Invalid A2S response type: " + std::to_string(responseType) + " (expected 0x49)");
        return;
    }

    try {

        if (offset >= data.size()) return;
        response.protocol = ReadUint8(data, offset);


        response.name = ReadNullTerminatedString(data, offset);
        if (response.name.empty() && offset < data.size()) {
            LogError("Failed to read server name at offset " + std::to_string(offset));
            return;
        }


        std::string cleanName = response.name;
        for (char& c : cleanName) {
            if (c < 32 || c > 126) c = ' ';
        }


        cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n"));
        cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);

        if (cleanName.empty() || cleanName.length() > 200) {
            LogError("Invalid server name: '" + response.name + "'");
            response.name = "Invalid Server Name";
        }
        else {
            response.name = cleanName;
        }

        response.map = ReadNullTerminatedString(data, offset);
        if (response.map.empty()) {
            response.map = "Unknown";
        }


        response.folder = ReadNullTerminatedString(data, offset);
        if (response.folder.empty()) {
            response.folder = "dayz";
        }


        response.game = ReadNullTerminatedString(data, offset);


        if (offset + 8 > data.size()) {
            LogError("Not enough data for remaining fields. Offset: " + std::to_string(offset) +
                ", Size: " + std::to_string(data.size()));
            return;
        }


        response.id = ReadUint16(data, offset);

        response.players = ReadUint8(data, offset);
        response.maxPlayers = ReadUint8(data, offset);
        response.bots = ReadUint8(data, offset);


        response.serverType = ReadUint8(data, offset);
        response.environment = ReadUint8(data, offset);
        response.visibility = ReadUint8(data, offset);
        response.vac = ReadUint8(data, offset);


        response.version = ReadNullTerminatedString(data, offset);
        if (response.version.empty()) {
            response.version = "1.28";
        }
            if (offset < data.size()) {
                response.edf = ReadUint8(data, offset);


                if (response.edf & 0x80 && offset + 2 <= data.size()) {
                    response.port = ReadUint16(data, offset);
                }
                if (response.edf & 0x10 && offset + 8 <= data.size()) {
                    response.steamId = ReadUint64(data, offset);
                }
                if (response.edf & 0x40) {
                    response.keywords = ReadNullTerminatedString(data, offset);
                }
                if (response.edf & 0x01) {
                    response.gameId = ReadNullTerminatedString(data, offset);
                }
            }


            if (response.maxPlayers > 200 || response.maxPlayers < 1) {
                LogError("Invalid maxPlayers: " + std::to_string(response.maxPlayers));
                response.maxPlayers = 60;
            }

            if (response.players > response.maxPlayers) {
                response.players = response.maxPlayers;
            }

            LogError("Successfully parsed server: " + response.name + " (" +
                std::to_string(response.players) + "/" + std::to_string(response.maxPlayers) + ")");

        }
        catch (const std::exception& e) {
            LogError("Exception in ParseA2SInfo: " + std::string(e.what()));
            response = A2SInfoResponse{};
            response.name = "Parse Error";
            response.map = "Unknown";
        }
        catch (...) {
            LogError("Unknown exception in ParseA2SInfo");
            response = A2SInfoResponse{};
            response.name = "Parse Error";
            response.map = "Unknown";
        }
    }




    int ServerQueryManager::PingServer(const std::string & ip, int port) {
        auto start = std::chrono::high_resolution_clock::now();

        A2SInfoResponse response;
        bool success = QueryServerInfo(ip, port, response);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        return success ? static_cast<int>(duration.count()) : -1;
    }


    bool ServerQueryManager::QuerySteamMasterServer(std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();

        const char* masterIP = "hl2master.steampowered.com";
        const int masterPort = 27011;

        struct hostent* host = gethostbyname(masterIP);
        if (!host) {
            LogError("Failed to resolve master server hostname");
            return false;
        }

        sockaddr_in masterAddr;
        masterAddr.sin_family = AF_INET;
        masterAddr.sin_port = htons(masterPort);
        memcpy(&masterAddr.sin_addr, host->h_addr_list[0], host->h_length);


        std::string currentStartIP = "0.0.0.0";
        int currentStartPort = 0;
        int batchCount = 0;
        int totalServers = 0;

        while (batchCount < 10) {
            batchCount++;

            std::string startAddr = currentStartIP + ":" + std::to_string(currentStartPort);
            LogError("=== BATCH " + std::to_string(batchCount) + " starting from: " + startAddr + " ===");


            std::vector<uint8_t> query;
            query.push_back(0x31);
            query.push_back(0xFF);

            for (char c : startAddr) {
                query.push_back(static_cast<uint8_t>(c));
            }
            query.push_back(0x00);

            std::string filter = "\\appid\\221100";
            for (char c : filter) {
                query.push_back(static_cast<uint8_t>(c));
            }
            query.push_back(0x00);


            if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
                (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
                LogError("Failed to send batch " + std::to_string(batchCount));
                break;
            }


            std::vector<uint8_t> buffer(1400);
            sockaddr_in fromAddr;
            int fromLen = sizeof(fromAddr);

            DWORD timeout = 15000;
            setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

            int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
                (sockaddr*)&fromAddr, &fromLen);

            if (bytesReceived <= 0) {
                LogError("No response for batch " + std::to_string(batchCount));
                break;
            }

            buffer.resize(bytesReceived);


            if (buffer.size() < 6 ||
                buffer[0] != 0xFF || buffer[1] != 0xFF || buffer[2] != 0xFF ||
                buffer[3] != 0xFF || buffer[4] != 0x66 || buffer[5] != 0x0A) {
                LogError("Invalid header for batch " + std::to_string(batchCount));
                break;
            }


            size_t offset = 6;
            int batchServerCount = 0;
            std::string lastServerIP;
            int lastServerPort = 0;

            while (offset + 6 <= buffer.size()) {
                uint32_t ip = (static_cast<uint32_t>(buffer[offset]) << 24) |
                    (static_cast<uint32_t>(buffer[offset + 1]) << 16) |
                    (static_cast<uint32_t>(buffer[offset + 2]) << 8) |
                    static_cast<uint32_t>(buffer[offset + 3]);
                offset += 4;

                uint16_t port = (static_cast<uint16_t>(buffer[offset]) << 8) |
                    static_cast<uint16_t>(buffer[offset + 1]);
                offset += 2;


                if (ip == 0 && port == 0) {
                    LogError("Batch " + std::to_string(batchCount) + " - Found end marker, no more servers!");
                    goto pagination_complete;
                }

                char ipStr[INET_ADDRSTRLEN];
                struct in_addr addr;
                addr.s_addr = htonl(ip);
                if (inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN)) {
                    servers.emplace_back(std::string(ipStr), port);
                    batchServerCount++;
                    totalServers++;

                    lastServerIP = std::string(ipStr);
                    lastServerPort = port;
                }
            }

            LogError("Batch " + std::to_string(batchCount) + " got " + std::to_string(batchServerCount) + " servers (Total: " + std::to_string(totalServers) + ")");


            if (batchServerCount == 0) {
                LogError("Empty batch - pagination complete");
                break;
            }


            currentStartIP = lastServerIP;
            currentStartPort = lastServerPort + 1;

            Sleep(1000);
        }

    pagination_complete:

        DWORD timeout = 5000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        LogError("=== PAGINATION COMPLETE: " + std::to_string(totalServers) + " servers in " + std::to_string(batchCount) + " batches ===");
        return !servers.empty();
    }

    bool ServerQueryManager::QuerySingleBatch(const std::string & startAddr, std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();

        const char* masterIP = "hl2master.steampowered.com";
        const int masterPort = 27011;

        struct hostent* host = gethostbyname(masterIP);
        if (!host) {
            return false;
        }

        sockaddr_in masterAddr;
        masterAddr.sin_family = AF_INET;
        masterAddr.sin_port = htons(masterPort);
        memcpy(&masterAddr.sin_addr, host->h_addr_list[0], host->h_length);


        std::vector<uint8_t> query;
        query.push_back(0x31);
        query.push_back(0xFF);

        for (char c : startAddr) {
            query.push_back(static_cast<uint8_t>(c));
        }
        query.push_back(0x00);

        std::string filter = "\\appid\\221100";
        for (char c : filter) {
            query.push_back(static_cast<uint8_t>(c));
        }
        query.push_back(0x00);

        if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
            (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
            return false;
        }

        std::vector<uint8_t> buffer(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        DWORD timeout = 10000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        timeout = 5000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        if (bytesReceived <= 0) return false;

        buffer.resize(bytesReceived);

        if (buffer.size() < 6 ||
            buffer[0] != 0xFF || buffer[1] != 0xFF || buffer[2] != 0xFF ||
            buffer[3] != 0xFF || buffer[4] != 0x66 || buffer[5] != 0x0A) {
            return false;
        }

        size_t offset = 6;
        int serverCount = 0;

        while (offset + 6 <= buffer.size()) {
            uint32_t ip = (static_cast<uint32_t>(buffer[offset]) << 24) |
                (static_cast<uint32_t>(buffer[offset + 1]) << 16) |
                (static_cast<uint32_t>(buffer[offset + 2]) << 8) |
                static_cast<uint32_t>(buffer[offset + 3]);
            offset += 4;

            uint16_t port = (static_cast<uint16_t>(buffer[offset]) << 8) |
                static_cast<uint16_t>(buffer[offset + 1]);
            offset += 2;

            if (ip == 0 && port == 0) break;

            char ipStr[INET_ADDRSTRLEN];
            struct in_addr addr;
            addr.s_addr = htonl(ip);
            if (inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN)) {
                servers.emplace_back(std::string(ipStr), port);
                serverCount++;
            }

            if (serverCount > 5000) break;
        }

        return serverCount > 0;
    }

    bool ServerQueryManager::QueryMasterServerFromStart(const std::string & startAddr, std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();
        const char* masterIP = "hl2master.steampowered.com";
        const int masterPort = 27011;

        struct hostent* host = gethostbyname(masterIP);
        if (!host) {
            return false;
        }

        sockaddr_in masterAddr;
        masterAddr.sin_family = AF_INET;
        masterAddr.sin_port = htons(masterPort);
        memcpy(&masterAddr.sin_addr, host->h_addr_list[0], host->h_length);

        std::vector<uint8_t> query;
        query.push_back(0x31);
        query.push_back(0xFF);
        for (char c : startAddr) {
            query.push_back(static_cast<uint8_t>(c));
        }
        query.push_back(0x00);


        std::string filter = "\\appid\\221100";
        for (char c : filter) {
            query.push_back(static_cast<uint8_t>(c));
        }
        query.push_back(0x00);


        if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
            (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
            return false;
        }

        std::vector<uint8_t> buffer(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        DWORD timeout = 10000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);


        timeout = 5000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        if (bytesReceived <= 0) return false;

        buffer.resize(bytesReceived);


        if (buffer.size() < 6 ||
            buffer[0] != 0xFF || buffer[1] != 0xFF || buffer[2] != 0xFF ||
            buffer[3] != 0xFF || buffer[4] != 0x66 || buffer[5] != 0x0A) {
            return false;
        }


        size_t offset = 6;
        int serverCount = 0;

        while (offset + 6 <= buffer.size()) {
            uint32_t ip = (static_cast<uint32_t>(buffer[offset]) << 24) |
                (static_cast<uint32_t>(buffer[offset + 1]) << 16) |
                (static_cast<uint32_t>(buffer[offset + 2]) << 8) |
                static_cast<uint32_t>(buffer[offset + 3]);
            offset += 4;

            uint16_t port = (static_cast<uint16_t>(buffer[offset]) << 8) |
                static_cast<uint16_t>(buffer[offset + 1]);
            offset += 2;

            if (ip == 0 && port == 0) break;

            char ipStr[INET_ADDRSTRLEN];
            struct in_addr addr;
            addr.s_addr = htonl(ip);
            if (inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN)) {
                servers.emplace_back(std::string(ipStr), port);
                serverCount++;
            }

            if (serverCount > 5000) break;
        }

        return serverCount > 0;
    }

    bool ServerQueryManager::QueryMultipleMasterServers(std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();
        std::vector<std::pair<std::string, int>> allServers;

        LogError("=== QUERYING MULTIPLE MASTER SERVERS ===");


        std::vector<std::pair<std::string, int>> batch1;
        if (QuerySteamMasterServer(batch1)) {
            LogError("Main master server returned: " + std::to_string(batch1.size()) + " servers");
            allServers.insert(allServers.end(), batch1.begin(), batch1.end());
        }

        std::vector<std::pair<std::string, int>> batch2;
        if (QuerySteamMasterServerDirect(batch2)) {
            LogError("Direct master server returned: " + std::to_string(batch2.size()) + " servers");
            allServers.insert(allServers.end(), batch2.begin(), batch2.end());
        }

        std::vector<std::string> startPoints = {
            "100.0.0.0:0",
            "150.0.0.0:0",
            "200.0.0.0:0",
            "50.0.0.0:0"
        };

        for (const std::string& startPoint : startPoints) {
            std::vector<std::pair<std::string, int>> batchServers;
            if (QueryMasterServerFromStart(startPoint, batchServers)) {
                LogError("Start point " + startPoint + " returned: " + std::to_string(batchServers.size()) + " servers");
                allServers.insert(allServers.end(), batchServers.begin(), batchServers.end());
            }
            Sleep(2000);
        }


        std::sort(allServers.begin(), allServers.end());
        allServers.erase(std::unique(allServers.begin(), allServers.end()), allServers.end());

        servers = allServers;
        LogError("=== TOTAL UNIQUE SERVERS: " + std::to_string(servers.size()) + " ===");

        return !servers.empty();
    }




    bool ServerQueryManager::QuerySteamMasterServerDirect(std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();


        std::vector<std::string> masterIPs = {
            "208.64.200.52",
            "208.64.200.39",
            "69.28.151.162"
        };

        for (const std::string& masterIP : masterIPs) {
            LogError("Trying direct master server IP: " + masterIP);

            sockaddr_in masterAddr;
            masterAddr.sin_family = AF_INET;
            masterAddr.sin_port = htons(27011);

            if (inet_pton(AF_INET, masterIP.c_str(), &masterAddr.sin_addr) != 1) {
                LogError("Invalid IP address: " + masterIP);
                continue;
            }


            std::vector<uint8_t> query;
            query.push_back(0x31);
            query.push_back(0xFF);


            std::string startAddr = "0.0.0.0:0";
            for (char c : startAddr) {
                query.push_back(static_cast<uint8_t>(c));
            }
            query.push_back(0x00);


            std::string filter = "\\appid\\221100";
            for (char c : filter) {
                query.push_back(static_cast<uint8_t>(c));
            }
            query.push_back(0x00);


            if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
                (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
                LogError("Failed to send to " + masterIP + ": " + GetLastSocketError());
                continue;
            }


            std::vector<uint8_t> buffer(1400);
            DWORD timeout = 10000;
            setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

            sockaddr_in fromAddr;
            int fromLen = sizeof(fromAddr);
            int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
                (sockaddr*)&fromAddr, &fromLen);

            if (bytesReceived > 6) {
                buffer.resize(bytesReceived);


                if (buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF &&
                    buffer[3] == 0xFF && buffer[4] == 0x66 && buffer[5] == 0x0A) {

                    LogError("Success! Got response from " + masterIP);
                    ParseMasterServerResponse(buffer, servers);

                    if (!servers.empty()) {
                        LogError("Found " + std::to_string(servers.size()) + " servers from " + masterIP);
                        return true;
                    }
                }
            }

            LogError("No valid response from " + masterIP);
        }

        return false;
    }


    bool ServerQueryManager::IsLANAddress(const std::string & ip) {

        if (ip.substr(0, 3) == "10.") {
            return true;
        }

        if (ip.substr(0, 8) == "192.168.") {
            return true;
        }

        if (ip.substr(0, 4) == "127.") {
            return true;
        }


        if (ip.substr(0, 4) == "172.") {
            size_t secondDot = ip.find('.', 4);
            if (secondDot != std::string::npos) {
                std::string secondOctet = ip.substr(4, secondDot - 4);
                try {
                    int octet = std::stoi(secondOctet);
                    if (octet >= 16 && octet <= 31) {
                        return true;
                    }
                }
                catch (...) {

                }
            }
        }

        return false;
    }


    bool ServerQueryManager::QuerySpecificMasterServer(const std::string & masterIP, int masterPort, std::vector<std::pair<std::string, int>>&servers) {
        sockaddr_in masterAddr;
        masterAddr.sin_family = AF_INET;
        masterAddr.sin_port = htons(masterPort);
        if (inet_pton(AF_INET, masterIP.c_str(), &masterAddr.sin_addr) != 1) {
            return false;
        }


        std::vector<uint8_t> query;
        query.push_back(0x31); // A2M_GET_SERVERS_BATCH2
        query.push_back(0xFF); // All regions

        for (int i = 0; i < 6; i++) query.push_back(0x00);

        std::string filter = "\\appid\\221100";
        for (char c : filter) query.push_back(static_cast<uint8_t>(c));
        query.push_back(0x00);


        if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
            (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
            return false;
        }


        std::vector<uint8_t> buffer(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        DWORD timeout = 8000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        timeout = 5000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        if (bytesReceived <= 0) return false;

        buffer.resize(bytesReceived);
        ParseMasterServerResponse(buffer, servers);

        return !servers.empty();
    }



    void ServerQueryManager::LogError(const std::string & message) {
        OutputDebugStringA(("ServerQuery: " + message + "\n").c_str());


#ifdef _DEBUG
        std::cout << "ServerQuery: " << message << std::endl;
#endif
    }

    bool ServerQueryManager::QueryAllRegions(std::vector<std::pair<std::string, int>>&servers) {
        if (!initialized) return false;

        servers.clear();
        std::vector<std::pair<std::string, int>> allServers;


        std::vector<uint8_t> regions = {
            0xFF, // All regions (worldwide)
            0x00, // US East coast
            0x01, // US West coast  
            0x02, // South America
            0x03, // Europe
            0x04, // Asia
            0x05, // Australia
            0x06, // Middle East
            0x07  // Africa
        };

        for (uint8_t region : regions) {
            LogError("Querying region: " + std::to_string(region));

            std::vector<std::pair<std::string, int>> regionServers;


            if (QueryMasterServerByRegion(region, regionServers)) {
                LogError("Region " + std::to_string(region) + " returned " + std::to_string(regionServers.size()) + " servers");
                allServers.insert(allServers.end(), regionServers.begin(), regionServers.end());
            }


            Sleep(2000);
        }

        std::sort(allServers.begin(), allServers.end());
        allServers.erase(std::unique(allServers.begin(), allServers.end()), allServers.end());

        servers = allServers;
        LogError("Total unique servers from all regions: " + std::to_string(servers.size()));

        return !servers.empty();
    }


    bool ServerQueryManager::QueryMasterServerByRegion(uint8_t region, std::vector<std::pair<std::string, int>>&servers) {
        const char* masterIP = "208.64.200.52";
        const int masterPort = 27011;

        sockaddr_in masterAddr;
        masterAddr.sin_family = AF_INET;
        masterAddr.sin_port = htons(masterPort);
        inet_pton(AF_INET, masterIP, &masterAddr.sin_addr);


        std::vector<uint8_t> query;
        query.push_back(0x31);
        query.push_back(region);
        for (int i = 0; i < 6; i++) {
            query.push_back(0x00);
        }


        std::string filter = "\\appid\\221100";
        query.insert(query.end(), filter.begin(), filter.end());
        query.push_back(0x00);

        if (sendto(udpSocket, (char*)query.data(), static_cast<int>(query.size()), 0,
            (sockaddr*)&masterAddr, sizeof(masterAddr)) == SOCKET_ERROR) {
            return false;
        }

        std::vector<uint8_t> buffer(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived <= 0) return false;

        buffer.resize(bytesReceived);
        ParseMasterServerResponse(buffer, servers);

        return true;
    }




    bool ServerQueryManager::SendQuery(const std::string & ip, int port, const ServerQuery & query,
        std::vector<uint8_t>&response, int timeoutMs) {
        if (!initialized) return false;

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);


        if (sendto(udpSocket, query.payload, static_cast<int>(query.payloadSize), 0,
            (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            return false;
        }


        response.resize(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        int bytesReceived = recvfrom(udpSocket, (char*)response.data(), static_cast<int>(response.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived <= 0) return false;

        response.resize(bytesReceived);
        return true;
    }

    bool ServerQueryManager::SendQueryWithChallenge(const std::string & ip, int port, const ServerQuery & query,
        std::vector<uint8_t>&response, int timeoutMs) {

        uint32_t challenge;
        if (!GetChallenge(ip, port, challenge)) {
            return false;
        }


        std::vector<uint8_t> challengeQuery;
        challengeQuery.insert(challengeQuery.end(),
            reinterpret_cast<const uint8_t*>(query.payload),
            reinterpret_cast<const uint8_t*>(query.payload) + query.payloadSize);

        challengeQuery.insert(challengeQuery.end(),
            reinterpret_cast<const uint8_t*>(&challenge),
            reinterpret_cast<const uint8_t*>(&challenge) + sizeof(challenge));

        ServerQuery challengeQueryStruct;
        challengeQueryStruct.header = query.header;
        challengeQueryStruct.payload = reinterpret_cast<const char*>(challengeQuery.data());
        challengeQueryStruct.payloadSize = challengeQuery.size();

        return SendQuery(ip, port, challengeQueryStruct, response, timeoutMs);
    }

    std::string ServerQueryManager::ReadNullTerminatedString(const std::vector<uint8_t>&data, size_t & offset) {
        std::string result;

        try {
            while (offset < data.size() && data[offset] != 0) {
                char c = static_cast<char>(data[offset]);

                if (c >= 32 && c <= 126) {
                    result += c;
                }
                else if (c == '\t' || c == '\n' || c == '\r') {
                    result += ' ';
                }


                offset++;


                if (result.length() > 512) {
                    LogError("String too long, truncating");
                    break;
                }
            }


            if (offset < data.size() && data[offset] == 0) {
                offset++;
            }


            result.erase(0, result.find_first_not_of(" \t\r\n"));
            result.erase(result.find_last_not_of(" \t\r\n") + 1);

        }
        catch (...) {
            LogError("Exception reading null-terminated string");
            result = "Read Error";
        }

        return result;
    }



    uint8_t ServerQueryManager::ReadUint8(const std::vector<uint8_t>&data, size_t & offset) {
        if (offset < data.size()) {
            return data[offset++];
        }
        return 0;
    }

    uint16_t ServerQueryManager::ReadUint16(const std::vector<uint8_t>&data, size_t & offset) {
        if (offset + 1 < data.size()) {
            uint16_t value = *reinterpret_cast<const uint16_t*>(&data[offset]);
            offset += 2;
            return value;
        }
        return 0;
    }

    uint32_t ServerQueryManager::ReadUint32(const std::vector<uint8_t>&data, size_t & offset) {
        if (offset + 3 < data.size()) {
            uint32_t value = *reinterpret_cast<const uint32_t*>(&data[offset]);
            offset += 4;
            return value;
        }
        return 0;
    }

    uint64_t ServerQueryManager::ReadUint64(const std::vector<uint8_t>&data, size_t & offset) {
        if (offset + 7 < data.size()) {
            uint64_t value = *reinterpret_cast<const uint64_t*>(&data[offset]);
            offset += 8;
            return value;
        }
        return 0;
    }

    float ServerQueryManager::ReadFloat(const std::vector<uint8_t>&data, size_t & offset) {
        if (offset + 3 < data.size()) {
            float value = *reinterpret_cast<const float*>(&data[offset]);
            offset += 4;
            return value;
        }
        return 0.0f;
    }



    bool ServerQueryManager::IsValidResponse(const std::vector<uint8_t>&data, uint8_t expectedType) {
        return data.size() >= 5 && data[4] == expectedType;
    }


    bool ServerQueryManager::SetSocketTimeout(SOCKET sock, int timeoutMs) {
        DWORD timeout = static_cast<DWORD>(timeoutMs);
        return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == 0;
    }

    bool ServerQueryManager::SetSocketNonBlocking(SOCKET sock, bool nonBlocking) {
        u_long mode = nonBlocking ? 1 : 0;
        return ioctlsocket(sock, FIONBIO, &mode) == 0;
    }

    std::string ServerQueryManager::GetLastSocketError() {
        int error = WSAGetLastError();
        return "Socket error: " + std::to_string(error);
    }

    // Challenge handling
    bool ServerQueryManager::GetChallenge(const std::string & ip, int port, uint32_t & challenge) {
        challenge = 0xFFFFFFFF;
        lastChallenge = challenge;
        return true;
    }


    std::vector<std::pair<std::string, int>> ServerQueryManager::GetLANServers() {
        std::vector<std::pair<std::string, int>> lanServers;

        return lanServers;
    }



    bool ServerQueryManager::QueryPlayerList(const std::string & ip, int port, A2SPlayerResponse & response) {

        if (!initialized) return false;

        std::vector<uint8_t> query = {
            0xFF, 0xFF, 0xFF, 0xFF,
            A2S_PLAYER,
            0xFF, 0xFF, 0xFF, 0xFF
        };

        std::vector<uint8_t> responseData;
        ServerQuery playerQuery;
        playerQuery.header = A2S_PLAYER;
        playerQuery.payload = reinterpret_cast<const char*>(query.data());
        playerQuery.payloadSize = query.size();

        if (SendQuery(ip, port, playerQuery, responseData)) {
            ParseA2SPlayer(responseData, response);
            return true;
        }

        return false;
    }


    bool ServerQueryManager::QueryServerRules(const std::string & ip, int port, A2SRulesResponse & response) {
        if (!initialized) return false;

        response = A2SRulesResponse{};

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
            return false;
        }


        std::vector<uint8_t> initialQuery = {
            0xFF, 0xFF, 0xFF, 0xFF,
            A2S_RULES,
            0xFF, 0xFF, 0xFF, 0xFF
        };

        if (sendto(udpSocket, (char*)initialQuery.data(), static_cast<int>(initialQuery.size()), 0,
            (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            return false;
        }

        std::vector<uint8_t> buffer(1400);
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        DWORD timeout = 5000;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        int bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
            (sockaddr*)&fromAddr, &fromLen);

        if (bytesReceived <= 0) return false;

        buffer.resize(bytesReceived);


        if (bytesReceived == 9 && buffer.size() >= 9 &&
            buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF && buffer[3] == 0xFF &&
            buffer[4] == 0x41) {


            uint32_t challenge = *reinterpret_cast<const uint32_t*>(&buffer[5]);

            std::vector<uint8_t> challengeQuery = {
                0xFF, 0xFF, 0xFF, 0xFF,
                A2S_RULES
            };

            challengeQuery.insert(challengeQuery.end(),
                reinterpret_cast<const uint8_t*>(&challenge),
                reinterpret_cast<const uint8_t*>(&challenge) + sizeof(challenge));

            if (sendto(udpSocket, (char*)challengeQuery.data(), static_cast<int>(challengeQuery.size()), 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                return false;
            }


            bytesReceived = recvfrom(udpSocket, (char*)buffer.data(), static_cast<int>(buffer.size()), 0,
                (sockaddr*)&fromAddr, &fromLen);

            if (bytesReceived <= 0) return false;
            buffer.resize(bytesReceived);
        }


        if (buffer.size() >= 6 &&
            buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF && buffer[3] == 0xFF &&
            buffer[4] == 0x45) {

            ParseA2SRules(buffer, response);
            return true;
        }

        return false;
    }


    bool ServerQueryManager::QueryMasterServerRegion(const std::string & region, std::vector<std::pair<std::string, int>>&servers) {

        return QuerySteamMasterServer(servers);
    }

    bool ServerQueryManager::GetCompleteServerInfo(const std::string & ip, int port, ServerInfo & info) {
        A2SInfoResponse infoResponse;
        A2SPlayerResponse playerResponse;
        A2SRulesResponse rulesResponse;

        bool hasInfo = QueryServerInfo(ip, port, infoResponse);
        bool hasPlayers = QueryPlayerList(ip, port, playerResponse);
        bool hasRules = QueryServerRules(ip, port, rulesResponse);

        if (hasInfo) {
            info.name = infoResponse.name;
            info.map = infoResponse.map;
            info.ip = ip;
            info.port = port;
            info.players = infoResponse.players;
            info.maxPlayers = infoResponse.maxPlayers;
            info.version = infoResponse.version;
            info.hasVAC = (infoResponse.vac == 1);
            info.isPassworded = (infoResponse.visibility == 1);
            info.folder = infoResponse.folder;
            info.ping = PingServer(ip, port);
            info.lastUpdated = time(nullptr);

            info.isOfficial = (info.name.find("Official") != std::string::npos) ||
                (info.name.find("DayZ") != std::string::npos && info.name.find("DE") != std::string::npos) ||
                (info.name.find("DayZ") != std::string::npos && info.name.find("US") != std::string::npos) ||
                (info.name.find("DayZ") != std::string::npos && info.name.find("UK") != std::string::npos);

            return true;
        }

        return false;
    }

    bool ServerQueryManager::GetBasicServerInfo(const std::string & ip, int port, ServerInfo & info) {
        A2SInfoResponse response;
        if (QueryServerInfo(ip, port, response)) {
            info.name = response.name;
            info.map = response.map;
            info.ip = ip;
            info.port = port;
            info.players = response.players;
            info.maxPlayers = response.maxPlayers;
            info.version = response.version;
            info.hasVAC = (response.vac == 1);
            info.isPassworded = (response.visibility == 1);
            info.ping = PingServer(ip, port);
            info.isOfficial = (info.name.find("Official") != std::string::npos);
            info.lastUpdated = time(nullptr);
            return true;
        }
        return false;
    }

    void ServerQueryManager::QueryMultipleServers(const std::vector<std::pair<std::string, int>>&addresses,
        std::vector<ServerInfo>&results) {
        results.clear();
        results.reserve(addresses.size());

        for (const auto& addr : addresses) {
            ServerInfo info;
            if (GetBasicServerInfo(addr.first, addr.second, info)) {
                results.push_back(info);
            }
        }
    }

    std::vector<std::pair<std::string, int>> ServerQueryManager::DiscoverLANServers() {
        std::vector<std::pair<std::string, int>> lanServers;


        std::vector<int> ports = { 2302, 2402, 2502, 2602 };

        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            hostent* host = gethostbyname(hostname);
            if (host && host->h_addr_list[0]) {

            }
        }

        return lanServers;
    }

    void ServerQueryManager::ParseA2SPlayer(const std::vector<uint8_t>&data, A2SPlayerResponse & response) {
        if (data.size() < 6) return;

        size_t offset = 5;

        response.playerCount = ReadUint8(data, offset);
        response.players.clear();
        response.players.reserve(response.playerCount);

        for (int i = 0; i < response.playerCount && offset < data.size(); ++i) {
            A2SPlayerResponse::Player player;
            player.index = ReadUint8(data, offset);
            player.name = ReadNullTerminatedString(data, offset);
            player.score = static_cast<int32_t>(ReadUint32(data, offset));
            player.duration = ReadFloat(data, offset);
            response.players.push_back(player);
        }
    }

    void ServerQueryManager::ParseA2SRules(const std::vector<uint8_t>&data, A2SRulesResponse & response) {
        response = A2SRulesResponse{};

        if (data.size() < 7) {
            LogError("A2S_RULES response too short: " + std::to_string(data.size()) + " bytes");
            return;
        }

        size_t offset = 5;

        try {
            response.ruleCount = ReadUint16(data, offset);
            response.rules.clear();
            response.rules.reserve(response.ruleCount);

            LogError("Parsing " + std::to_string(response.ruleCount) + " server rules");

            for (int i = 0; i < response.ruleCount && offset < data.size(); ++i) {
                A2SRulesResponse::Rule rule;
                rule.name = ReadNullTerminatedString(data, offset);
                rule.value = ReadNullTerminatedString(data, offset);

                if (!rule.name.empty()) {
                    response.rules.push_back(rule);
                    LogError("Rule: '" + rule.name + "' = '" + rule.value + "'");
                }
            }

            LogError("Successfully parsed " + std::to_string(response.rules.size()) + " rules");
        }
        catch (const std::exception& e) {
            LogError("Exception in ParseA2SRules: " + std::string(e.what()));
        }
        catch (...) {
            LogError("Unknown exception in ParseA2SRules");
        }
    }


    void ServerQueryManager::ParseMasterServerResponse(const std::vector<uint8_t>&data,
        std::vector<std::pair<std::string, int>>&servers) {
        if (data.size() < 6) return;

        size_t offset = 6;

        while (offset + 6 <= data.size()) {

            uint32_t ip = (static_cast<uint32_t>(data[offset]) << 24) |
                (static_cast<uint32_t>(data[offset + 1]) << 16) |
                (static_cast<uint32_t>(data[offset + 2]) << 8) |
                static_cast<uint32_t>(data[offset + 3]);
            offset += 4;


            uint16_t port = (static_cast<uint16_t>(data[offset]) << 8) |
                static_cast<uint16_t>(data[offset + 1]);
            offset += 2;


            if (ip == 0 && port == 0) {
                break;
            }


            char ipStr[INET_ADDRSTRLEN];
            struct in_addr addr;
            addr.s_addr = htonl(ip);
            inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN);

            servers.emplace_back(std::string(ipStr), port);
        }
    }

    std::string ServerInfo::toJson() const {
        std::ostringstream json;
        json << "{"
            << "\"name\":\"" << name << "\","
            << "\"ip\":\"" << ip << "\","
            << "\"port\":" << port << ","
            << "\"map\":\"" << map << "\","
            << "\"players\":" << players << ","
            << "\"maxPlayers\":" << maxPlayers << ","
            << "\"ping\":" << ping << ","
            << "\"isOfficial\":" << (isOfficial ? "true" : "false") << ","
            << "\"isFavorite\":" << (isFavorite ? "true" : "false") << ","
            << "\"isPassworded\":" << (isPassworded ? "true" : "false") << ","
            << "\"hasVAC\":" << (hasVAC ? "true" : "false") << ","
            << "\"version\":\"" << version << "\","
            << "\"gameMode\":\"" << gameMode << "\","
            << "\"folder\":\"" << folder << "\","
            << "\"lastUpdated\":" << lastUpdated
            << "}";
        return json.str();
    }

    ServerInfo ServerInfo::fromJson(const std::string & json) {
        ServerInfo server;


        size_t pos = 0;
        auto findValue = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            size_t start = json.find(search, pos);
            if (start == std::string::npos) return "";
            start += search.length();
            size_t end = json.find("\"", start);
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
            };

        auto findNumber = [&](const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            size_t start = json.find(search);
            if (start == std::string::npos) return 0;
            start += search.length();
            size_t end = json.find_first_of(",}", start);
            if (end == std::string::npos) return 0;
            std::string numStr = json.substr(start, end - start);
            try {
                return std::stoi(numStr);
            }
            catch (...) {
                return 0;
            }
            };

        auto findBool = [&](const std::string& key) -> bool {
            std::string search = "\"" + key + "\":true";
            return json.find(search) != std::string::npos;
            };

        server.name = findValue("name");
        server.ip = findValue("ip");
        server.map = findValue("map");
        server.version = findValue("version");
        server.gameMode = findValue("gameMode");
        server.folder = findValue("folder");

        server.port = findNumber("port");
        server.players = findNumber("players");
        server.maxPlayers = findNumber("maxPlayers");
        server.ping = findNumber("ping");
        server.lastUpdated = findNumber("lastUpdated");

        server.isOfficial = findBool("isOfficial");
        server.isFavorite = findBool("isFavorite");
        server.isPassworded = findBool("isPassworded");
        server.hasVAC = findBool("hasVAC");

        return server;
    }


    namespace ServerUtils {
        bool IsOfficialServer(const ServerInfo& server) {
            return server.isOfficial ||
                server.name.find("Official") != std::string::npos ||
                server.name.find("DayZ DE") != std::string::npos ||
                server.name.find("DayZ US") != std::string::npos ||
                server.name.find("DayZ UK") != std::string::npos ||
                server.name.find("DayZ AU") != std::string::npos;
        }

        bool IsModdedServer(const ServerInfo& server) {
            return !server.mods.empty() ||
                server.name.find("Modded") != std::string::npos ||
                server.name.find("Custom") != std::string::npos ||
                server.folder != "dayz";
        }

        std::string GetPingCategory(int ping) {
            if (ping < 0) return "Unknown";
            if (ping < 50) return "Excellent";
            if (ping < 100) return "Good";
            if (ping < 150) return "Fair";
            if (ping < 250) return "Poor";
            return "Very Poor";
        }

        std::string GetPlayerCountCategory(const ServerInfo& server) {
            if (server.maxPlayers == 0) return "Unknown";

            float ratio = server.getPlayerRatio();
            if (ratio >= 1.0f) return "Full";
            if (ratio >= 0.8f) return "High";
            if (ratio >= 0.5f) return "Medium";
            if (ratio > 0.0f) return "Low";
            return "Empty";
        }

        std::string FormatUptime(int seconds) {
            if (seconds < 60) return std::to_string(seconds) + "s";
            if (seconds < 3600) return std::to_string(seconds / 60) + "m";
            if (seconds < 86400) return std::to_string(seconds / 3600) + "h " + std::to_string((seconds % 3600) / 60) + "m";
            return std::to_string(seconds / 86400) + "d " + std::to_string((seconds % 86400) / 3600) + "h";
        }

        std::string FormatLastSeen(time_t timestamp) {
            time_t now = time(nullptr);
            int diff = static_cast<int>(now - timestamp);

            if (diff < 60) return "Just now";
            if (diff < 3600) return std::to_string(diff / 60) + " minutes ago";
            if (diff < 86400) return std::to_string(diff / 3600) + " hours ago";
            return std::to_string(diff / 86400) + " days ago";
        }

        std::vector<std::string> ParseServerTags(const std::string& tags) {
            std::vector<std::string> result;
            std::stringstream ss(tags);
            std::string tag;

            while (std::getline(ss, tag, ',')) {

                tag.erase(0, tag.find_first_not_of(" \t"));
                tag.erase(tag.find_last_not_of(" \t") + 1);
                if (!tag.empty()) {
                    result.push_back(tag);
                }
            }

            return result;
        }

        std::string GetCountryFromIP(const std::string& ip) {
            if (ip.substr(0, 3) == "85.") return "Germany";
            if (ip.substr(0, 3) == "194") return "Netherlands";
            if (ip.substr(0, 3) == "185") return "France";
            if (ip.substr(0, 3) == "176") return "Russia";
            if (ip.substr(0, 3) == "198") return "United States";
            if (ip.substr(0, 3) == "139") return "Canada";
            return "Unknown";
        }
    }
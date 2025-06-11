#include "FavoritesManager.h"
#include "ServerQuery.h"  
#include <fstream>
#include <sstream>
#include <algorithm>


FavoriteServer::FavoriteServer(const ServerInfo& server) {
    name = server.name;
    ip = server.ip;
    port = server.port;
    comment = "";
    dateAdded = time(nullptr);
    lastConnected = 0;
    connectionCount = 0;
    notifyWhenOnline = false;
}

std::string FavoriteServer::toJson() const {
    std::ostringstream json;
    json << "{"
        << "\"name\":\"" << name << "\","
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"comment\":\"" << comment << "\","
        << "\"dateAdded\":" << dateAdded << ","
        << "\"lastConnected\":" << lastConnected << ","
        << "\"connectionCount\":" << connectionCount << ","
        << "\"notifyWhenOnline\":" << (notifyWhenOnline ? "true" : "false")
        << "}";
    return json.str();
}

FavoriteServer FavoriteServer::fromJson(const std::string& json) {
    FavoriteServer server;


    auto findStringValue = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        size_t start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.length();
        size_t end = json.find("\"", start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
        };

    auto findIntValue = [&](const std::string& key) -> int {
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

    auto findBoolValue = [&](const std::string& key) -> bool {
        std::string search = "\"" + key + "\":true";
        return json.find(search) != std::string::npos;
        };

    server.name = findStringValue("name");
    server.ip = findStringValue("ip");
    server.comment = findStringValue("comment");

    server.port = findIntValue("port");
    server.dateAdded = static_cast<time_t>(findIntValue("dateAdded"));
    server.lastConnected = static_cast<time_t>(findIntValue("lastConnected"));
    server.connectionCount = findIntValue("connectionCount");

    server.notifyWhenOnline = findBoolValue("notifyWhenOnline");

    return server;
}


std::string ServerHistory::toJson() const {
    std::ostringstream json;
    json << "{"
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"serverName\":\"" << serverName << "\","
        << "\"lastConnected\":" << lastConnected << ","
        << "\"connectionCount\":" << connectionCount << ","
        << "\"totalPlayTime\":" << totalPlayTime.count()
        << "}";
    return json.str();
}

ServerHistory ServerHistory::fromJson(const std::string& json) {
    ServerHistory history;

    auto findStringValue = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        size_t start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.length();
        size_t end = json.find("\"", start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
        };

    auto findIntValue = [&](const std::string& key) -> int {
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

    history.ip = findStringValue("ip");
    history.serverName = findStringValue("serverName");
    history.port = findIntValue("port");
    history.lastConnected = static_cast<time_t>(findIntValue("lastConnected"));
    history.connectionCount = findIntValue("connectionCount");
    history.totalPlayTime = std::chrono::seconds(findIntValue("totalPlayTime"));

    return history;
}


FavoritesManager::FavoritesManager(const std::string& favFile, const std::string& histFile)
    : favoritesFile(favFile), historyFile(histFile) {
  
}

FavoritesManager::~FavoritesManager() {
    SaveFavorites();
    SaveHistory();
}

void FavoritesManager::LoadFavorites() {
    std::lock_guard<std::mutex> lock(favoritesMutex);
    favorites.clear();
    favoriteAddresses.clear();

    std::ifstream file(favoritesFile);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            try {
                FavoriteServer server = FavoriteServer::fromJson(line);
                favorites.push_back(server);
                favoriteAddresses.insert(server.getAddressString());
            }
            catch (...) {

            }
        }
    }
}

void FavoritesManager::SaveFavorites() {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    std::ofstream file(favoritesFile);
    if (!file.is_open()) {
        return;
    }

    for (const auto& server : favorites) {
        file << server.toJson() << std::endl;
    }
}

bool FavoritesManager::AddFavorite(const ServerInfo& server, const std::string& comment) {
    std::lock_guard<std::mutex> lock(favoritesMutex);

   
    std::string address = server.ip + ":" + std::to_string(server.port);
    if (favoriteAddresses.find(address) != favoriteAddresses.end()) {
        return false;
    }

    FavoriteServer favorite(server);
    favorite.comment = comment;
    favorites.push_back(favorite);
    favoriteAddresses.insert(address);

    return true;
}

bool FavoritesManager::RemoveFavorite(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    std::string address = ip + ":" + std::to_string(port);

    auto it = std::remove_if(favorites.begin(), favorites.end(),
        [&ip, port](const FavoriteServer& server) {
            return server.ip == ip && server.port == port;
        });

    if (it != favorites.end()) {
        favorites.erase(it, favorites.end());
        favoriteAddresses.erase(address);
        return true;
    }

    return false;
}

bool FavoritesManager::IsFavorite(const std::string& ip, int port) const {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    std::string address = ip + ":" + std::to_string(port);
    return favoriteAddresses.find(address) != favoriteAddresses.end();
}

const std::vector<FavoriteServer>& FavoritesManager::GetFavorites() const {
    return favorites;
}

void FavoritesManager::ClearFavorites() {
    std::lock_guard<std::mutex> lock(favoritesMutex);
    favorites.clear();
    favoriteAddresses.clear();
}

bool FavoritesManager::UpdateFavoriteComment(const std::string& ip, int port, const std::string& comment) {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    for (auto& favorite : favorites) {
        if (favorite.ip == ip && favorite.port == port) {
            favorite.comment = comment;
            return true;
        }
    }

    return false;
}

bool FavoritesManager::SetNotificationEnabled(const std::string& ip, int port, bool enabled) {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    for (auto& favorite : favorites) {
        if (favorite.ip == ip && favorite.port == port) {
            favorite.notifyWhenOnline = enabled;
            return true;
        }
    }

    return false;
}

FavoriteServer* FavoritesManager::FindFavorite(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    for (auto& favorite : favorites) {
        if (favorite.ip == ip && favorite.port == port) {
            return &favorite;
        }
    }

    return nullptr;
}

const FavoriteServer* FavoritesManager::FindFavorite(const std::string& ip, int port) const {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    for (const auto& favorite : favorites) {
        if (favorite.ip == ip && favorite.port == port) {
            return &favorite;
        }
    }

    return nullptr;
}

bool FavoritesManager::ImportFavorites(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::vector<FavoriteServer> importedFavorites;

    while (std::getline(file, line)) {
        if (!line.empty()) {
            try {
                FavoriteServer server = FavoriteServer::fromJson(line);
                importedFavorites.push_back(server);
            }
            catch (...) {
         
            }
        }
    }


    std::lock_guard<std::mutex> lock(favoritesMutex);
    for (const auto& server : importedFavorites) {
        std::string address = server.ip + ":" + std::to_string(server.port);
        if (favoriteAddresses.find(address) == favoriteAddresses.end()) {
            favorites.push_back(server);
            favoriteAddresses.insert(address);
        }
    }

    return true;
}

bool FavoritesManager::ExportFavorites(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(favoritesMutex);

    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& server : favorites) {
        file << server.toJson() << std::endl;
    }

    return true;
}

bool FavoritesManager::ImportFromDayZSALauncher(const std::string& filename) {

    return false;
}

bool FavoritesManager::ImportFromDZSALauncher(const std::string& filename) {

    return false;
}

void FavoritesManager::LoadHistory() {
    std::lock_guard<std::mutex> lock(historyMutex);
    recentServers.clear();

    std::ifstream file(historyFile);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            try {
                ServerHistory history = ServerHistory::fromJson(line);
                recentServers.push_back(history);
            }
            catch (...) {
          
            }
        }
    }
}

void FavoritesManager::SaveHistory() {
    std::lock_guard<std::mutex> lock(historyMutex);

    std::ofstream file(historyFile);
    if (!file.is_open()) {
        return;
    }

    for (const auto& history : recentServers) {
        file << history.toJson() << std::endl;
    }
}

void FavoritesManager::AddToHistory(const ServerInfo& server) {
    std::lock_guard<std::mutex> lock(historyMutex);

    auto it = std::find_if(recentServers.begin(), recentServers.end(),
        [&server](const ServerHistory& history) {
            return history.ip == server.ip && history.port == server.port;
        });

    if (it != recentServers.end()) {
  
        it->serverName = server.name;
        it->lastConnected = time(nullptr);
        it->connectionCount++;
    }
    else {
     
        ServerHistory history;
        history.ip = server.ip;
        history.port = server.port;
        history.serverName = server.name;
        history.lastConnected = time(nullptr);
        history.connectionCount = 1;
        history.totalPlayTime = std::chrono::seconds(0);

        recentServers.push_back(history);
    }


    if (recentServers.size() > MAX_HISTORY_ENTRIES) {
      
        std::sort(recentServers.begin(), recentServers.end(),
            [](const ServerHistory& a, const ServerHistory& b) {
                return a.lastConnected > b.lastConnected;
            });
        recentServers.resize(MAX_HISTORY_ENTRIES);
    }
}

void FavoritesManager::RecordConnection(const std::string& ip, int port, const std::string& serverName) {
    std::lock_guard<std::mutex> lock(historyMutex);

    auto it = std::find_if(recentServers.begin(), recentServers.end(),
        [&ip, port](const ServerHistory& history) {
            return history.ip == ip && history.port == port;
        });

    if (it != recentServers.end()) {
        it->serverName = serverName;
        it->lastConnected = time(nullptr);
        it->connectionCount++;
    }
    else {
        ServerHistory history;
        history.ip = ip;
        history.port = port;
        history.serverName = serverName;
        history.lastConnected = time(nullptr);
        history.connectionCount = 1;
        history.totalPlayTime = std::chrono::seconds(0);

        recentServers.push_back(history);
    }
}

void FavoritesManager::RecordPlayTime(const std::string& ip, int port, std::chrono::seconds playTime) {
    std::lock_guard<std::mutex> lock(historyMutex);

    auto it = std::find_if(recentServers.begin(), recentServers.end(),
        [&ip, port](const ServerHistory& history) {
            return history.ip == ip && history.port == port;
        });

    if (it != recentServers.end()) {
        it->totalPlayTime += playTime;
    }
}

const std::vector<ServerHistory>& FavoritesManager::GetRecentServers() const {
    return recentServers;
}

void FavoritesManager::ClearHistory() {
    std::lock_guard<std::mutex> lock(historyMutex);
    recentServers.clear();
}

size_t FavoritesManager::GetFavoriteCount() const {
    std::lock_guard<std::mutex> lock(favoritesMutex);
    return favorites.size();
}

size_t FavoritesManager::GetHistoryCount() const {
    std::lock_guard<std::mutex> lock(historyMutex);
    return recentServers.size();
}

ServerHistory* FavoritesManager::GetMostPlayedServer() {
    std::lock_guard<std::mutex> lock(historyMutex);

    if (recentServers.empty()) {
        return nullptr;
    }

    auto it = std::max_element(recentServers.begin(), recentServers.end(),
        [](const ServerHistory& a, const ServerHistory& b) {
            return a.totalPlayTime < b.totalPlayTime;
        });

    return &(*it);
}

std::vector<ServerHistory> FavoritesManager::GetTopPlayedServers(size_t count) const {
    std::lock_guard<std::mutex> lock(historyMutex);

    std::vector<ServerHistory> sorted = recentServers;
    std::sort(sorted.begin(), sorted.end(),
        [](const ServerHistory& a, const ServerHistory& b) {
            return a.totalPlayTime > b.totalPlayTime;
        });

    if (sorted.size() > count) {
        sorted.resize(count);
    }

    return sorted;
}

std::chrono::seconds FavoritesManager::GetTotalPlayTime() const {
    std::lock_guard<std::mutex> lock(historyMutex);

    std::chrono::seconds total(0);
    for (const auto& history : recentServers) {
        total += history.totalPlayTime;
    }

    return total;
}

void FavoritesManager::CleanupOldHistory(std::chrono::hours maxAge) {
    std::lock_guard<std::mutex> lock(historyMutex);

    time_t cutoffTime = time(nullptr) - static_cast<time_t>(maxAge.count() * 3600);

    auto it = std::remove_if(recentServers.begin(), recentServers.end(),
        [cutoffTime](const ServerHistory& history) {
            return history.lastConnected < cutoffTime;
        });

    recentServers.erase(it, recentServers.end());
}

void FavoritesManager::OptimizeStorage() {

    std::lock_guard<std::mutex> lock1(favoritesMutex);
    std::lock_guard<std::mutex> lock2(historyMutex);


    std::sort(favorites.begin(), favorites.end(),
        [](const FavoriteServer& a, const FavoriteServer& b) {
            return a.getAddressString() < b.getAddressString();
        });

    auto favEnd = std::unique(favorites.begin(), favorites.end(),
        [](const FavoriteServer& a, const FavoriteServer& b) {
            return a.getAddressString() == b.getAddressString();
        });

    favorites.erase(favEnd, favorites.end());

 
    RebuildFavoriteAddressSet();


    std::sort(recentServers.begin(), recentServers.end(),
        [](const ServerHistory& a, const ServerHistory& b) {
            return a.getAddressString() < b.getAddressString();
        });

    auto histEnd = std::unique(recentServers.begin(), recentServers.end(),
        [](const ServerHistory& a, const ServerHistory& b) {
            return a.getAddressString() == b.getAddressString();
        });

    recentServers.erase(histEnd, recentServers.end());
}

void FavoritesManager::RebuildFavoriteAddressSet() {
    favoriteAddresses.clear();
    for (const auto& favorite : favorites) {
        favoriteAddresses.insert(favorite.getAddressString());
    }
}

void FavoritesManager::TrimHistory() {
    if (recentServers.size() > MAX_HISTORY_ENTRIES) {
   
        std::sort(recentServers.begin(), recentServers.end(),
            [](const ServerHistory& a, const ServerHistory& b) {
                return a.lastConnected > b.lastConnected;
            });

        recentServers.resize(MAX_HISTORY_ENTRIES);
    }
}

std::string FavoritesManager::AddressToString(const std::string& ip, int port) const {
    return ip + ":" + std::to_string(port);
}

bool FavoritesManager::CreateBackup(const std::string& filename) const {
    std::string backupFile = filename + ".backup";

    std::ifstream src(filename, std::ios::binary);
    std::ofstream dst(backupFile, std::ios::binary);

    if (!src.is_open() || !dst.is_open()) {
        return false;
    }

    dst << src.rdbuf();

    return true;
}

std::string FavoritesManager::VectorToJson(const std::vector<FavoriteServer>& favorites) const {
    std::ostringstream json;
    json << "[";

    for (size_t i = 0; i < favorites.size(); ++i) {
        if (i > 0) json << ",";
        json << favorites[i].toJson();
    }

    json << "]";
    return json.str();
}

std::vector<FavoriteServer> FavoritesManager::FavoritesFromJson(const std::string& json) const {
    std::vector<FavoriteServer> result;

    return result;
}

std::string FavoritesManager::HistoryToJson(const std::vector<ServerHistory>& history) const {
    std::ostringstream json;
    json << "[";

    for (size_t i = 0; i < history.size(); ++i) {
        if (i > 0) json << ",";
        json << history[i].toJson();
    }

    json << "]";
    return json.str();
}

std::vector<ServerHistory> FavoritesManager::HistoryFromJson(const std::string& json) const {
    std::vector<ServerHistory> result;

    return result;
}

bool FavoritesManager::ReadFile(const std::string& filename, std::string& content) const {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    content = ss.str();

    return true;
}

bool FavoritesManager::WriteFile(const std::string& filename, const std::string& content) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    return true;
}

bool FavoritesManager::FileExists(const std::string& filename) const {
    std::ifstream file(filename);
    return file.good();
}

bool FavoritesManager::ParseDayZSALauncherFile(const std::string& content, std::vector<FavoriteServer>& servers) {
   
    return false;
}

bool FavoritesManager::ParseDZSALauncherFile(const std::string& content, std::vector<FavoriteServer>& servers) {

    return false;
}


namespace FavoriteUtils {
    std::string FormatDateAdded(time_t timestamp) {
        if (timestamp == 0) return "Unknown";

        char buffer[64];
        struct tm timeinfo;
        localtime_s(&timeinfo, &timestamp);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeinfo);
        return std::string(buffer);
    }

    std::string FormatLastConnected(time_t timestamp) {
        if (timestamp == 0) return "Never";

        time_t now = time(nullptr);
        int diff = static_cast<int>(now - timestamp);

        if (diff < 60) return "Just now";
        if (diff < 3600) return std::to_string(diff / 60) + " minutes ago";
        if (diff < 86400) return std::to_string(diff / 3600) + " hours ago";
        if (diff < 2592000) return std::to_string(diff / 86400) + " days ago";

        char buffer[64];
        struct tm timeinfo;
        localtime_s(&timeinfo, &timestamp);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
        return std::string(buffer);
    }

    std::string FormatPlayTime(std::chrono::seconds playTime) {
        int seconds = static_cast<int>(playTime.count());

        if (seconds < 60) return std::to_string(seconds) + "s";
        if (seconds < 3600) return std::to_string(seconds / 60) + "m";
        if (seconds < 86400) return std::to_string(seconds / 3600) + "h " + std::to_string((seconds % 3600) / 60) + "m";

        int days = seconds / 86400;
        int hours = (seconds % 86400) / 3600;
        return std::to_string(days) + "d " + std::to_string(hours) + "h";
    }

    std::string FormatConnectionCount(int count) {
        if (count == 0) return "Never connected";
        if (count == 1) return "Connected once";
        return "Connected " + std::to_string(count) + " times";
    }

    bool IsValidServerAddress(const std::string& ip, int port) {
        if (port <= 0 || port > 65535) return false;
        if (ip.empty()) return false;


        int dots = 0;
        bool hasDigit = false;

        for (char c : ip) {
            if (c == '.') {
                if (!hasDigit) return false;
                dots++;
                hasDigit = false;
            }
            else if (c >= '0' && c <= '9') {
                hasDigit = true;
            }
            else {
                return false;
            }
        }

        return dots == 3 && hasDigit;
    }

    std::string SanitizeComment(const std::string& comment) {
        std::string sanitized = comment;

  
        std::replace(sanitized.begin(), sanitized.end(), '"', '\'');
        std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
        std::replace(sanitized.begin(), sanitized.end(), '\r', ' ');
        std::replace(sanitized.begin(), sanitized.end(), '\t', ' ');


        if (sanitized.length() > 256) {
            sanitized = sanitized.substr(0, 256);
        }

        return sanitized;
    }
}
#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <unordered_set>
#include <fstream>
#include <chrono>
#include <functional>

struct ServerInfo;

struct FavoriteServer {
    std::string name;
    std::string ip;
    int port;
    std::string comment;
    time_t dateAdded;
    time_t lastConnected;
    int connectionCount;
    bool notifyWhenOnline;

    FavoriteServer() : port(0), dateAdded(0), lastConnected(0),
        connectionCount(0), notifyWhenOnline(false) {
    }

    FavoriteServer(const ServerInfo& server);

    std::string getAddressString() const {
        return ip + ":" + std::to_string(port);
    }

    bool operator==(const FavoriteServer& other) const {
        return ip == other.ip && port == other.port;
    }


    std::string toJson() const;
    static FavoriteServer fromJson(const std::string& json);
};

struct ServerHistory {
    std::string ip;
    int port;
    std::string serverName;
    time_t lastConnected;
    int connectionCount;
    std::chrono::seconds totalPlayTime;

    ServerHistory() : port(0), lastConnected(0), connectionCount(0), totalPlayTime(0) {}

    std::string getAddressString() const {
        return ip + ":" + std::to_string(port);
    }

    std::string toJson() const;
    static ServerHistory fromJson(const std::string& json);
};

class FavoritesManager {
private:
    std::string favoritesFile;
    std::string historyFile;
    std::vector<FavoriteServer> favorites;
    std::vector<ServerHistory> recentServers;
    std::unordered_set<std::string> favoriteAddresses; 
    mutable std::mutex favoritesMutex;
    mutable std::mutex historyMutex;

    static const size_t MAX_HISTORY_ENTRIES = 100;

public:
    FavoritesManager(const std::string& favFile = "favorites.json",
        const std::string& histFile = "history.json");
    ~FavoritesManager();

 
    void LoadFavorites();
    void SaveFavorites();
    bool AddFavorite(const ServerInfo& server, const std::string& comment = "");
    bool RemoveFavorite(const std::string& ip, int port);
    bool IsFavorite(const std::string& ip, int port) const;
    const std::vector<FavoriteServer>& GetFavorites() const;
    void ClearFavorites();


    bool UpdateFavoriteComment(const std::string& ip, int port, const std::string& comment);
    bool SetNotificationEnabled(const std::string& ip, int port, bool enabled);
    FavoriteServer* FindFavorite(const std::string& ip, int port);
    const FavoriteServer* FindFavorite(const std::string& ip, int port) const;

    bool ImportFavorites(const std::string& filename);
    bool ExportFavorites(const std::string& filename) const;
    bool ImportFromDayZSALauncher(const std::string& filename);
    bool ImportFromDZSALauncher(const std::string& filename);

    void LoadHistory();
    void SaveHistory();
    void AddToHistory(const ServerInfo& server);
    void RecordConnection(const std::string& ip, int port, const std::string& serverName);
    void RecordPlayTime(const std::string& ip, int port, std::chrono::seconds playTime);
    const std::vector<ServerHistory>& GetRecentServers() const;
    void ClearHistory();

 
    size_t GetFavoriteCount() const;
    size_t GetHistoryCount() const;
    ServerHistory* GetMostPlayedServer();
    std::vector<ServerHistory> GetTopPlayedServers(size_t count = 10) const;
    std::chrono::seconds GetTotalPlayTime() const;

 
    void CleanupOldHistory(std::chrono::hours maxAge = std::chrono::hours(24 * 30));
    void OptimizeStorage();



private:

    void RebuildFavoriteAddressSet();
    void TrimHistory();
    std::string AddressToString(const std::string& ip, int port) const;
    bool CreateBackup(const std::string& filename) const;


    std::string VectorToJson(const std::vector<FavoriteServer>& favorites) const;
    std::vector<FavoriteServer> FavoritesFromJson(const std::string& json) const;
    std::string HistoryToJson(const std::vector<ServerHistory>& history) const;
    std::vector<ServerHistory> HistoryFromJson(const std::string& json) const;


    bool ReadFile(const std::string& filename, std::string& content) const;
    bool WriteFile(const std::string& filename, const std::string& content) const;
    bool FileExists(const std::string& filename) const;


    bool ParseDayZSALauncherFile(const std::string& content, std::vector<FavoriteServer>& servers);
    bool ParseDZSALauncherFile(const std::string& content, std::vector<FavoriteServer>& servers);
};


namespace FavoriteUtils {
    std::string FormatDateAdded(time_t timestamp);
    std::string FormatLastConnected(time_t timestamp);
    std::string FormatPlayTime(std::chrono::seconds playTime);
    std::string FormatConnectionCount(int count);
    bool IsValidServerAddress(const std::string& ip, int port);
    std::string SanitizeComment(const std::string& comment);
}
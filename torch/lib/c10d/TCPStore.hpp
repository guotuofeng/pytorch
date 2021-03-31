#pragma once

#include <memory>
#include <thread>
#include <unordered_map>

#include <c10d/Store.hpp>

#ifdef _WIN32
#include <c10d/WinSockUtils.hpp>
#else
#include <c10d/UnixSockUtils.hpp>
#endif

namespace c10d {

// Abstract base class to handle thread state
class BackgroundThread {
  public:
    explicit BackgroundThread(int storeListenSocket);
    virtual ~BackgroundThread() = 0;
  protected:
    std::thread daemonThread_;
    int storeListenSocket_;
    std::vector<int> sockets_;
#ifdef _WIN32
    const std::chrono::milliseconds checkTimeout_
        = std::chrono::milliseconds(10);
    HANDLE ghStopEvent_;
    char* eventName_;
#else
    std::vector<int> controlPipeFd_{-1, -1};
#endif
  private:
    void join();
    void stop();
    void initStopSignal();
    void closeStopSignal();
};

// Run on master process
class TCPStoreDaemon : public BackgroundThread {
 public:
  // Empty constructor used for derived classes
  explicit TCPStoreDaemon(int storeListenSocket);

 protected:
  void run();
  void queryFds(std::vector<struct pollfd>& fds);
  void query(int socket);

  void setHandler(int socket);
  void compareSetHandler(int socket);
  void addHandler(int socket);
  void getHandler(int socket) const;
  void checkHandler(int socket) const;
  void getNumKeysHandler(int socket) const;
  void deleteHandler(int socket);
  void waitHandler(int socket);
  void watchHandler(int socket);

  bool checkKeys(const std::vector<std::string>& keys) const;
  void wakeupWaitingClients(const std::string& key);
  void sendKeyUpdatesToClients(const std::string& key,
      std::vector<uint8_t>& oldData,
      std::vector<uint8_t>& newData);

  std::unordered_map<std::string, std::vector<uint8_t>> tcpStore_;
  // From key -> the list of sockets waiting on it
  std::unordered_map<std::string, std::vector<int>> waitingSockets_;
  // From socket -> number of keys awaited
  std::unordered_map<int, size_t> keysAwaited_;
  // From key -> the list of sockets waiting on it
  std::unordered_map<std::string, std::vector<int>> watchedSockets_;
#ifdef _WIN32
  char* eventName_ = "tcpStoreDaemonStopEvent";
#endif
};

// Listener thread runs on all processes
// Right now only handles callbacks registered from watchKey()
class ListenThread : public BackgroundThread {
  public:
    explicit ListenThread(int listenSocket);
    // Adds a callback to run key change
    void addCallback(std::string key, std::function<void(std::string, std::string)> cb);

  protected:
    void run();
    void callbackHandler(int socket);
    // List of callbacks map each watched key
    std::unordered_map<std::string, std::function<void(std::string, std::string)>> keyToCallbacks_;
#ifdef _WIN32
    char* eventName_ = "listenThreadStopEvent";
#endif
};

class TCPStore : public Store {
 public:
  explicit TCPStore(
      const std::string& masterAddr,
      PortType masterPort,
      c10::optional<int> numWorkers = c10::nullopt_t(-1),
      bool isServer = false,
      const std::chrono::milliseconds& timeout = kDefaultTimeout,
      bool waitWorkers = true);

  virtual ~TCPStore();

  void set(const std::string& key, const std::vector<uint8_t>& value) override;

  std::vector<uint8_t> compareSet(
      const std::string& key,
      const std::vector<uint8_t>& currentValue,
      const std::vector<uint8_t>& newValue) override;

  std::vector<uint8_t> get(const std::string& key) override;

  int64_t add(const std::string& key, int64_t value) override;

  bool deleteKey(const std::string& key) override;

  // callback function takes arguments (string oldValue, string newValue)
  void watchKey(const std::string& key, std::function<void(std::string, std::string)> callback) override;

  bool check(const std::vector<std::string>& keys) override;

  int64_t getNumKeys() override;

  void wait(const std::vector<std::string>& keys) override;

  void wait(
      const std::vector<std::string>& keys,
      const std::chrono::milliseconds& timeout) override;

  // Waits for all workers to join.
  void waitForWorkers();

  // Returns the hostname used by the TCPStore.
  const std::string& getHost() const noexcept;

  // Returns the port used by the TCPStore.
  PortType getPort() const noexcept;

 protected:
  int64_t addHelper_(const std::string& key, int64_t value);
  std::vector<uint8_t> getHelper_(const std::string& key);
  void waitHelper_(
      const std::vector<std::string>& keys,
      const std::chrono::milliseconds& timeout);

  bool isServer_;
  int storeSocket_ = -1;
  int listenSocket_ = -1;
  int masterListenSocket_ = -1;
  std::thread listenThread_;

  std::string tcpStoreAddr_;
  PortType tcpStorePort_;

  c10::optional<int> numWorkers_;
  const std::string initKey_;
  const std::string regularPrefix_;

  // Only needs to be launched as the server
  std::unique_ptr<TCPStoreDaemon> tcpStoreDaemon_ = nullptr;

  // Launched from all clients
  std::unique_ptr<ListenThread> watchListener_ = nullptr;
};

} // namespace c10d

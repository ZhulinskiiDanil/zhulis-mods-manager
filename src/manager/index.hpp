#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "../registry/index.hpp"
#include "../github/index.hpp"

using namespace geode::prelude;

namespace manager
{
  enum class Status
  {
    Loading,
    NotInstalled,
    UpdateAvailable,
    UpToDate,
    OnIndex, // the Geode Index has this version or newer, Geode handles it
    NoRelease,
    Downloading,
    Downloaded, // waits for a restart
    Error,
  };

  struct ModState
  {
    registry::ModEntry entry;
    std::vector<github::Release> releases; // newest first
    std::optional<VersionInfo> installed;
    std::optional<VersionInfo> indexVersion;
    Status status = Status::Loading;
    std::string error;

    float progress = 0.f;      // 0..1 while downloading
    std::string downloadedTag; // set once downloaded
    std::vector<std::string> missingDeps;

    int pending = 0; // requests still running for this mod

    github::Release const *latest() const { return releases.empty() ? nullptr : &releases.front(); }
  };

  class Manager
  {
  private:
    using Task = async::TaskHolder<web::WebResponse>;

    std::vector<ModState> m_mods;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_force = false;

    Task m_registryTask;
    std::map<std::string, Task> m_releaseTasks;
    std::map<std::string, Task> m_indexTasks;
    std::map<std::string, Task> m_downloadTasks;
    std::set<std::string> m_restartPrompted;

    size_t m_nextSubscriber = 0;
    std::map<size_t, std::function<void()>> m_subscribers;

    Manager() = default;

    void setMods(std::vector<registry::ModEntry> entries);
    void fetchMod(std::string const &id);
    void setReleases(ModState &state, std::optional<std::string> const &body, std::string error);

    void onRegistry(web::WebResponse const &response);
    void onRelease(std::string const &id, web::WebResponse const &response);
    void onIndex(std::string const &id, web::WebResponse const &response);
    void finishRequest(std::string const &id);
    void onDownload(std::string const &id, github::Release const &release, web::WebResponse const &response);
    void failInstall(ModState &state, std::string error);
    void promptRestart();

    void updateStatus(ModState &state);
    ModState *findMut(std::string_view id);
    void notify();

  public:
    static Manager &get();

    // `force` skips the releases cache, for a manual refresh
    void refresh(bool force = false);
    // Installs the given tag, or the latest release
    void install(std::string const &id, std::optional<std::string> tag = std::nullopt);
    void installAll();

    std::vector<ModState> const &mods() const { return m_mods; }
    ModState const *find(std::string_view id) const;
    size_t updatesCount() const;
    bool isLoading() const { return m_loading; }
    bool isLoaded() const { return m_loaded; }

    // Called on the main thread whenever any mod state changes
    size_t subscribe(std::function<void()> callback);
    void unsubscribe(size_t id);
  };

  char const *statusText(Status status);

  // Markdown with the notes of every release newer than the installed one
  std::string changelog(ModState const &state);
}

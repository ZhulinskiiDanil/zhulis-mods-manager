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
    NoRelease,
    Downloading,
    Downloaded, // waits for a restart
    Error,
  };

  struct ModState
  {
    registry::ModEntry entry;
    std::optional<github::Release> release;
    std::optional<VersionInfo> installed;
    Status status = Status::Loading;
    std::string error;
  };

  class Manager
  {
  private:
    using Task = async::TaskHolder<web::WebResponse>;

    std::vector<ModState> m_mods;
    bool m_loading = false;
    bool m_loaded = false;

    Task m_registryTask;
    std::map<std::string, Task> m_releaseTasks;
    std::map<std::string, Task> m_downloadTasks;

    size_t m_nextSubscriber = 0;
    std::map<size_t, std::function<void()>> m_subscribers;

    Manager() = default;

    void setMods(std::vector<registry::ModEntry> entries);
    void fetchRelease(std::string const &id);

    void onRegistry(web::WebResponse const &response);
    void onRelease(std::string const &id, web::WebResponse const &response);
    void onDownload(std::string const &id, web::WebResponse const &response);
    void failInstall(ModState &state, std::string error);
    void updateStatus(ModState &state);
    ModState *findMut(std::string_view id);
    void notify();

  public:
    static Manager &get();

    void refresh();
    void install(std::string const &id);

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
}

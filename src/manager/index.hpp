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

    // ! --- Nightly --- !
    std::optional<github::Nightly> nightly; // looked up on demand
    bool nightlyChecked = false;
    bool nightlyLoading = false;
    std::string nightlyError;
    std::optional<std::string> installedNightly; // commit of the installed nightly build
    bool nightlyUpdate = false;                  // the update is a newer nightly, not a release
    bool installingNightly = false;              // the last install was a nightly, for Retry

    github::Release const *latest() const { return releases.empty() ? nullptr : &releases.front(); }

    // What "Update" installs: a release tag or a nightly label
    std::string updateName() const
    {
      if (nightlyUpdate && nightly)
        return nightly->label();

      return latest() ? latest()->tag : "";
    }
  };

  class Manager
  {
  private:
    using Task = async::TaskHolder<web::WebResponse>;
    using NightlyTask = async::TaskHolder<Result<std::optional<github::Nightly>>>;

    std::vector<ModState> m_mods;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_force = false;

    Task m_registryTask;
    std::map<std::string, Task> m_releaseTasks;
    std::map<std::string, Task> m_indexTasks;
    std::map<std::string, Task> m_downloadTasks;
    std::map<std::string, NightlyTask> m_nightlyTasks;
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
    // `counted` when it's part of a refresh and the mod status waits for it
    void loadNightly(std::string const &id, bool counted);
    void onNightly(std::string const &id, Result<std::optional<github::Nightly>> result, bool counted);

    void startDownload(ModState &state, bool nightly);
    Function<void(web::WebProgress const &)> progressCallback(std::string const &id);
    void onDownload(std::string const &id, github::Release const &release, web::WebResponse const &response);
    void onNightlyDownload(std::string const &id, github::Nightly const &nightly, web::WebResponse const &response);
    // Checks the downloaded package at `temp` and puts it into the mods folder
    void installPackage(ModState &state, std::filesystem::path const &temp,
                        std::string label, std::optional<std::string> nightlySha);
    void failInstall(ModState &state, std::string error);
    void promptRestart();

    void updateStatus(ModState &state);
    ModState *findMut(std::string_view id);
    void notify();

  public:
    static Manager &get();

    // `force` skips the releases cache, for a manual refresh
    void refresh(bool force = false);
    // Installs the given tag, or what "Update" offers
    void install(std::string const &id, std::optional<std::string> tag = std::nullopt);
    void installNightly(std::string const &id);
    // Looks up the latest nightly build, cached like releases
    void fetchNightly(std::string const &id);
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

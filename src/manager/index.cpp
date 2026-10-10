#include "index.hpp"

#include <Geode/ui/GeodeUI.hpp>

#include "../cache/index.hpp"
#include "../geodeindex/index.hpp"

using namespace manager;

Manager &Manager::get()
{
  // Leaked on purpose: running tasks must not be destroyed at exit
  static auto instance = new Manager();
  return *instance;
}

static bool isBusy(Status status)
{
  return status == Status::Downloading || status == Status::Downloaded || status == Status::Changed;
}

// ! --- Installed nightlies --- !
// A nightly build has the version of the last release, so its commit is saved along with it

static std::string nightlyKey(std::string const &id, std::string_view field)
{
  return fmt::format("nightly-{}-{}", id, field);
}

// Commit of the installed build, if it's still the nightly the manager installed
static std::optional<std::string> installedNightly(std::string const &id)
{
  auto mod = Loader::get()->getInstalledMod(id);
  auto sha = Mod::get()->getSavedValue<std::string>(nightlyKey(id, "sha"));
  auto version = Mod::get()->getSavedValue<std::string>(nightlyKey(id, "version"));

  if (!mod || sha.empty() || version != mod->getVersion().toVString())
    return std::nullopt;

  return sha;
}

// An empty `sha` forgets it, after a release is installed
static void saveInstalledNightly(std::string const &id, std::string const &sha, VersionInfo const &version)
{
  Mod::get()->setSavedValue(nightlyKey(id, "sha"), sha);
  Mod::get()->setSavedValue(nightlyKey(id, "version"), sha.empty() ? std::string() : version.toVString());
}

static std::string nightlyCacheKey(std::string const &repo)
{
  return repo + "-nightly";
}

static std::optional<github::Nightly> nightlyFromCache(cache::Entry const &entry)
{
  return github::nightlyFromJson(matjson::parse(entry.body).unwrapOrDefault());
}

// ! --- Fetching --- !

void Manager::refresh(bool force)
{
  if (m_loading)
    return;

  m_loading = true;
  m_force = force;
  m_releaseTasks.clear();
  m_indexTasks.clear();

  for (auto &state : m_mods)
  {
    if (!isBusy(state.status))
      state.status = Status::Loading;
  }

  notify();

  m_registryTask.spawn(registry::fetch(), [this](web::WebResponse response)
                       { onRegistry(response); });
}

void Manager::onRegistry(web::WebResponse const &response)
{
  auto entries = registry::parse(response);

  if (entries.isErr())
  {
    log::warn("Using built-in registry: {}", entries.unwrapErr());
    setMods(registry::fallback());
  }
  else
    setMods(entries.unwrap());

  for (auto const &state : m_mods)
    fetchMod(state.entry.id);
}

void Manager::setMods(std::vector<registry::ModEntry> entries)
{
  // ! --- Self-update --- !
  // The manager always tracks itself, first in the list
  auto selfID = Mod::get()->getID();
  auto self = std::ranges::find_if(entries, [&](auto const &entry)
                                   { return entry.id == selfID; });

  if (self == entries.end())
  {
    auto fallback = registry::fallback();
    auto selfEntry = std::ranges::find_if(fallback, [&](auto const &entry)
                                          { return entry.id == selfID; });

    if (selfEntry != fallback.end())
      entries.insert(entries.begin(), *selfEntry);
  }
  else
    std::rotate(entries.begin(), self, self + 1);

  std::vector<ModState> mods;

  for (auto &entry : entries)
  {
    auto old = findMut(entry.id);

    // Keep download state across refreshes
    if (old && isBusy(old->status))
    {
      old->entry = std::move(entry);
      mods.push_back(std::move(*old));
      continue;
    }

    ModState state{std::move(entry)};

    // A nightly lookup may still be running
    if (old)
    {
      state.nightly = std::move(old->nightly);
      state.nightlyChecked = old->nightlyChecked;
      state.nightlyLoading = old->nightlyLoading;
      state.nightlyError = std::move(old->nightlyError);
      state.installingNightly = old->installingNightly;
    }

    mods.push_back(std::move(state));
  }

  m_mods = std::move(mods);

  if (m_mods.empty())
  {
    m_loading = false;
    m_loaded = true;
  }

  notify();
}

void Manager::fetchMod(std::string const &id)
{
  auto state = findMut(id);

  if (!state)
    return;

  // On the nightly channel, or the user already looked at the nightly
  bool nightly = installedNightly(id) || state->nightlyChecked;

  state->pending = nightly ? 3 : 2;

  if (nightly)
    loadNightly(id, true);

  m_indexTasks[id].spawn(geodeindex::fetchLatest(id), [this, id](web::WebResponse response)
                         { onIndex(id, response); });

  auto cached = cache::load(state->entry.repo);

  // Saves the rate limit on frequent restarts
  if (cached && !m_force && cache::isFresh(*cached))
  {
    setReleases(*state, cached->body, "");
    finishRequest(id);
    return;
  }

  m_releaseTasks[id].spawn(
      github::fetchReleases(state->entry.repo, cached ? cached->etag : ""),
      [this, id](web::WebResponse response)
      { onRelease(id, response); });
}

void Manager::onRelease(std::string const &id, web::WebResponse const &response)
{
  if (auto state = findMut(id))
  {
    auto const &repo = state->entry.repo;
    auto cached = cache::load(repo);

    if (response.code() == 304 && cached)
    {
      cache::save(repo, *cached); // still fresh
      setReleases(*state, cached->body, "");
    }
    else if (response.ok())
    {
      auto body = response.string().unwrapOr("");
      cache::save(repo, {std::string(response.header("ETag").value_or("")), body});
      setReleases(*state, body, "");
    }
    // Offline or rate limited: the last known releases are better than nothing
    else if (cached)
    {
      log::warn("{}: {}, using cached releases", id, github::describeError(response));
      setReleases(*state, cached->body, "");
    }
    else
      setReleases(*state, std::nullopt, github::describeError(response));
  }

  finishRequest(id);
}

void Manager::setReleases(ModState &state, std::optional<std::string> const &body, std::string error)
{
  state.releases.clear();
  state.error = std::move(error);

  if (!body)
  {
    log::warn("{}: {}", state.entry.id, state.error);
    return;
  }

  auto releases = github::parseReleases(
      *body, state.entry.id, Mod::get()->getSettingValue<bool>("include-prereleases"));

  if (releases.isOk())
    state.releases = releases.unwrap();
  else
    state.error = releases.unwrapErr();
}

void Manager::onIndex(std::string const &id, web::WebResponse const &response)
{
  if (auto state = findMut(id))
    state->indexVersion = geodeindex::parseLatest(response);

  finishRequest(id);
}

void Manager::fetchNightly(std::string const &id)
{
  if (!m_nightlyTasks[id].isPending())
    loadNightly(id, false);
}

void Manager::loadNightly(std::string const &id, bool counted)
{
  auto state = findMut(id);

  if (!state)
    return;

  auto key = nightlyCacheKey(state->entry.repo);
  auto cached = cache::load(key);

  // Several requests per lookup, the cache matters even more than for releases
  if (cached && !(counted && m_force) && cache::isFresh(*cached))
  {
    state->nightly = nightlyFromCache(*cached);
    state->nightlyChecked = true;
    state->nightlyLoading = false;
    state->nightlyError.clear();
    m_nightlyTasks[id].cancel();

    if (counted)
      return finishRequest(id);

    if (state->pending == 0)
      updateStatus(*state);

    notify();
    return;
  }

  state->nightlyLoading = true;

  if (!counted)
    notify();

  m_nightlyTasks[id].spawn(
      github::fetchNightly(state->entry.repo),
      [this, id, counted](Result<std::optional<github::Nightly>> result)
      { onNightly(id, std::move(result), counted); });
}

void Manager::onNightly(std::string const &id, Result<std::optional<github::Nightly>> result, bool counted)
{
  if (auto state = findMut(id))
  {
    auto key = nightlyCacheKey(state->entry.repo);

    state->nightlyChecked = true;
    state->nightlyLoading = false;
    state->nightlyError.clear();

    if (result.isOk())
    {
      state->nightly = std::move(result).unwrap();
      cache::save(key, {"", github::nightlyToJson(state->nightly).dump(matjson::NO_INDENTATION)});
    }
    else if (auto cached = cache::load(key))
    {
      log::warn("{}: {}, using cached nightly", id, result.unwrapErr());
      state->nightly = nightlyFromCache(*cached);
    }
    else
    {
      log::warn("{}: {}", id, result.unwrapErr());
      state->nightly.reset();
      state->nightlyError = std::move(result).unwrapErr();
    }

    // A newer nightly can be an update on its own
    if (!counted && state->pending == 0)
      updateStatus(*state);
  }

  if (counted)
    finishRequest(id);
  else
    notify();
}

void Manager::finishRequest(std::string const &id)
{
  if (auto state = findMut(id); state && state->pending > 0)
  {
    if (--state->pending == 0)
      updateStatus(*state);
  }

  m_loading = std::ranges::any_of(m_mods, [](auto const &mod)
                                  { return mod.pending > 0; });
  m_loaded = !m_loading;

  notify();
}

void Manager::updateStatus(ModState &state)
{
  if (isBusy(state.status))
    return;

  auto mod = Loader::get()->getInstalledMod(state.entry.id);

  if (mod && !mod->isUninstalled())
    state.installed = mod->getVersion();
  else
    state.installed.reset();

  // Turned off in Geode: updates wait until it's on again
  if (state.installed && !mod->isOrWillBeEnabled())
  {
    state.status = Status::Disabled;
    return;
  }

  state.installedNightly = installedNightly(state.entry.id);
  state.nightlyUpdate = false;

  auto latest = state.latest();
  auto const &index = state.indexVersion;
  auto const &installed = state.installed;

  // ! --- Nightly channel --- !
  // A newer build of the default branch, unless a release or the Geode Index got ahead of it
  if (installed && state.installedNightly && state.nightly && state.nightly->sha != *state.installedNightly &&
      !(latest && *installed < latest->version) && !(index && *installed < *index))
  {
    state.status = Status::UpdateAvailable;
    state.nightlyUpdate = true;
    return;
  }

  // GitHub has nothing newer than the Geode Index: let Geode handle it
  if (index && (!latest || latest->version <= *index))
  {
    state.status = installed && *index <= *installed ? Status::UpToDate : Status::OnIndex;
    return;
  }

  if (!latest)
    state.status = state.error.empty() ? Status::NoRelease : Status::Error;
  else if (!installed)
    state.status = Status::NotInstalled;
  // A newer build installed some other way is left alone
  else if (*installed < latest->version)
    state.status = Status::UpdateAvailable;
  else
    state.status = Status::UpToDate;
}

// ! --- Installing --- !

void Manager::install(std::string const &id, std::optional<std::string> tag)
{
  auto state = findMut(id);

  if (!state || state->status == Status::Downloading)
    return;

  if (!tag && state->nightlyUpdate)
    return installNightly(id);

  if (state->releases.empty())
    return;

  auto release = state->releases.front();

  if (tag)
  {
    auto it = std::ranges::find_if(state->releases, [&](auto const &release)
                                   { return release.tag == *tag; });

    if (it == state->releases.end())
      return;

    release = *it;
  }

  startDownload(*state, false);

  m_downloadTasks[id].spawn(
      github::download(release.assetUrl, progressCallback(id)),
      [this, id, release](web::WebResponse response)
      { onDownload(id, release, response); });
}

void Manager::installNightly(std::string const &id)
{
  auto state = findMut(id);

  if (!state || state->status == Status::Downloading || !state->nightly)
    return;

  auto nightly = *state->nightly;

  startDownload(*state, true);

  m_downloadTasks[id].spawn(
      github::download(nightly.assetUrl, progressCallback(id)),
      [this, id, nightly](web::WebResponse response)
      { onNightlyDownload(id, nightly, response); });
}

void Manager::startDownload(ModState &state, bool nightly)
{
  state.status = Status::Downloading;
  state.progress = 0.f;
  state.missingDeps.clear();
  state.installingNightly = nightly;
  m_restartPrompted.erase(state.entry.id);
  notify();
}

Function<void(web::WebProgress const &)> Manager::progressCallback(std::string const &id)
{
  // Progress callbacks already run on the main thread
  return [this, id](web::WebProgress const &progress)
  {
    if (auto state = findMut(id))
      state->progress = progress.downloadProgress().value_or(0.f) / 100.f;
  };
}

void Manager::installAll()
{
  std::vector<std::string> ids;

  for (auto const &state : m_mods)
  {
    if (state.status == Status::UpdateAvailable)
      ids.push_back(state.entry.id);
  }

  for (auto const &id : ids)
    install(id);
}

static std::vector<std::string> findMissingDeps(ModMetadata const &metadata, std::vector<ModState> const &mods)
{
  std::vector<std::string> missing;

  for (auto const &dep : metadata.getDependencies())
  {
    if (!dep.isRequired() || dep.getID() == "geode.loader")
      continue;

    // Already downloaded by the manager, loads after the restart
    auto pending = std::ranges::find_if(mods, [&](auto const &mod)
                                        { return mod.entry.id == dep.getID() && mod.status == Status::Downloaded; });

    if (pending != mods.end())
      continue;

    auto mod = Loader::get()->getInstalledMod(dep.getID());

    if (!mod || !dep.getVersion().compare(mod->getVersion()))
      missing.push_back(dep.getID());
  }

  return missing;
}

void Manager::onDownload(std::string const &id, github::Release const &release, web::WebResponse const &response)
{
  auto state = findMut(id);

  if (!state)
    return;

  if (!response.ok())
    return failInstall(*state, fmt::format("Download failed ({})", response.code()));

  // ! --- Integrity --- !
  if (!release.sha256.empty())
  {
    auto actual = geode::sha256(response.data()).toString();

    if (actual != release.sha256)
    {
      log::error("{}: hash mismatch, expected {}, got {}", id, release.sha256, actual);
      return failInstall(*state, "The download is corrupted (hash mismatch)");
    }
  }

  auto temp = dirs::getModsDir() / (id + ".geode.tmp");

  if (auto res = response.into(temp); res.isErr())
    return failInstall(*state, fmt::format("Can't save the file: {}", res.unwrapErr()));

  installPackage(*state, temp, release.tag, std::nullopt);
}

void Manager::onNightlyDownload(std::string const &id, github::Nightly const &nightly, web::WebResponse const &response)
{
  auto state = findMut(id);

  if (!state)
    return;

  if (!response.ok())
    return failInstall(*state, fmt::format("Download failed ({})", response.code()));

  // ! --- Integrity --- !
  // The zip comes through nightly.link, GitHub's digest is what makes it trustworthy
  auto actual = geode::sha256(response.data()).toString();

  if (nightly.sha256.empty() || actual != nightly.sha256)
  {
    log::error("{}: nightly hash mismatch, expected {}, got {}", id, nightly.sha256, actual);
    return failInstall(*state, "The download is corrupted (hash mismatch)");
  }

  // ! --- Artifact --- !
  auto archive = utils::file::Unzip::create(response.data());

  if (archive.isErr())
    return failInstall(*state, fmt::format("Invalid build archive: {}", archive.unwrapErr()));

  auto &unzip = archive.unwrap();
  auto entries = unzip.getEntries();
  auto entry = std::ranges::find(entries, std::filesystem::path(id + ".geode"));

  if (entry == entries.end())
    entry = std::ranges::find_if(entries, [](auto const &path)
                                 { return path.extension() == ".geode"; });

  if (entry == entries.end())
    return failInstall(*state, "The build has no .geode file");

  auto data = unzip.extract(*entry);

  if (data.isErr())
    return failInstall(*state, fmt::format("Can't unpack the build: {}", data.unwrapErr()));

  auto temp = dirs::getModsDir() / (id + ".geode.tmp");

  if (auto res = utils::file::writeBinary(temp, data.unwrap()); res.isErr())
    return failInstall(*state, fmt::format("Can't save the file: {}", res.unwrapErr()));

  installPackage(*state, temp, nightly.label(), nightly.sha);
}

void Manager::installPackage(ModState &state, std::filesystem::path const &temp,
                             std::string label, std::optional<std::string> nightlySha)
{
  auto const &id = state.entry.id;
  auto target = dirs::getModsDir() / (id + ".geode");
  std::error_code ec;

  // ! --- Compatibility --- !
  auto metadata = ModMetadata::createFromGeodeFile(temp);
  std::optional<std::string> invalid;

  if (metadata.hasErrors())
    invalid = fmt::format("Invalid mod package: {}", metadata.getErrors().front());
  else if (metadata.getID() != id)
    invalid = fmt::format("The package is for another mod ({})", metadata.getID());
  else if (auto compatible = metadata.checkTargetVersions(); compatible.isErr())
    invalid = fmt::format("{} isn't compatible: {}", label, compatible.unwrapErr());

  if (invalid)
  {
    std::filesystem::remove(temp, ec);
    return failInstall(state, *invalid);
  }

  std::filesystem::rename(temp, target, ec);

  if (ec)
  {
    std::filesystem::remove(temp, ec);
    return failInstall(state, "Can't replace the installed mod file");
  }

  saveInstalledNightly(id, nightlySha.value_or(""), metadata.getVersion());

  state.status = Status::Downloaded;
  state.downloadedTag = std::move(label);
  state.error.clear();
  state.missingDeps = findMissingDeps(metadata, m_mods);
  notify();

  // One prompt for the whole batch
  bool downloading = std::ranges::any_of(m_mods, [](auto const &mod)
                                         { return mod.status == Status::Downloading; });

  if (!downloading)
    promptRestart();
}

void Manager::promptRestart()
{
  std::vector<std::string> installed;
  std::vector<std::string> missing;

  for (auto const &state : m_mods)
  {
    if (state.status != Status::Downloaded || m_restartPrompted.contains(state.entry.id))
      continue;

    m_restartPrompted.insert(state.entry.id);
    installed.push_back(fmt::format("<cg>{}</c> {}", state.entry.name, state.downloadedTag));

    for (auto const &dep : state.missingDeps)
    {
      if (std::ranges::find(missing, dep) == missing.end())
        missing.push_back(dep);
    }
  }

  if (installed.empty())
    return;

  auto list = fmt::format("{}", fmt::join(installed, ", "));

  // ! --- Dependencies --- !
  if (!missing.empty())
  {
    createQuickPopup(
        "Dependencies required",
        fmt::format("{} installed, but {} also needed: <cy>{}</c>\n"
                    "Install them from the Geode Index, then restart the game.",
                    list, missing.size() == 1 ? "this mod is" : "these mods are",
                    fmt::join(missing, ", ")),
        "Later", "Install",
        [missing](auto, bool install)
        {
          if (install)
            (void)openInfoPopup(missing.front());
        });

    return;
  }

  createQuickPopup(
      "Restart required",
      fmt::format("{} installed. Restart the game to load {}.",
                  list, installed.size() == 1 ? "it" : "them"),
      "Later", "Restart",
      [](auto, bool restart)
      {
        if (restart)
          game::restart(true);
      });
}

void Manager::failInstall(ModState &state, std::string error)
{
  log::error("Installing {} failed: {}", state.entry.id, error);

  state.status = Status::Error;
  state.error = std::move(error);
  notify();

  FLAlertLayer::create("Install failed", state.error, "OK")->show();

  // Mods that did download in the same batch still need their prompt
  bool downloading = std::ranges::any_of(m_mods, [](auto const &mod)
                                         { return mod.status == Status::Downloading; });

  if (!downloading)
    promptRestart();
}

// ! --- State --- !

ModState *Manager::findMut(std::string_view id)
{
  auto it = std::ranges::find_if(m_mods, [&](auto const &mod)
                                 { return mod.entry.id == id; });

  return it == m_mods.end() ? nullptr : &*it;
}

ModState const *Manager::find(std::string_view id) const
{
  return const_cast<Manager *>(this)->findMut(id);
}

// ! --- Enable, uninstall --- !

void Manager::enable(std::string const &id)
{
  auto state = findMut(id);
  auto mod = Loader::get()->getInstalledMod(id);

  if (!state || !mod)
    return;

  if (auto result = mod->enable(); !result)
  {
    Notification::create(fmt::format("Can't enable {}: {}", state->entry.name, result.unwrapErr()), NotificationIcon::Error)->show();
    return;
  }

  state->status = Status::Changed;
  state->change = "Enabled";
  notify();
}

void Manager::uninstall(std::string const &id)
{
  auto state = findMut(id);
  auto mod = Loader::get()->getInstalledMod(id);

  // The manager can't take itself away from here
  if (!state || !mod || mod == Mod::get())
    return;

  if (auto result = mod->uninstall(false); !result)
  {
    Notification::create(fmt::format("Can't uninstall {}: {}", state->entry.name, result.unwrapErr()), NotificationIcon::Error)->show();
    return;
  }

  // A nightly that is gone can't be the installed one anymore
  Mod::get()->getSaveContainer().erase(nightlyKey(id, "sha"));
  Mod::get()->getSaveContainer().erase(nightlyKey(id, "version"));

  state->status = Status::Changed;
  state->change = "Uninstalled";
  notify();
}

size_t Manager::updatesCount() const
{
  return std::ranges::count_if(m_mods, [](auto const &mod)
                               { return mod.status == Status::UpdateAvailable; });
}

size_t Manager::subscribe(std::function<void()> callback)
{
  auto id = m_nextSubscriber++;
  m_subscribers[id] = std::move(callback);
  return id;
}

void Manager::unsubscribe(size_t id)
{
  m_subscribers.erase(id);
}

void Manager::notify()
{
  // Copy: a callback may (un)subscribe
  auto subscribers = m_subscribers;

  for (auto const &[_, callback] : subscribers)
    callback();
}

char const *manager::statusText(Status status)
{
  switch (status)
  {
  case Status::Loading:
    return "Checking...";
  case Status::NotInstalled:
    return "Not installed";
  case Status::UpdateAvailable:
    return "Update available";
  case Status::UpToDate:
    return "Up to date";
  case Status::OnIndex:
    return "On the Geode Index";
  case Status::NoRelease:
    return "No releases yet";
  case Status::Downloading:
    return "Downloading...";
  case Status::Downloaded:
  case Status::Changed:
    return "Restart to apply";
  case Status::Disabled:
    return "Disabled";
  case Status::Error:
    return "Error";
  }

  return "";
}

std::string manager::changelog(ModState const &state)
{
  static constexpr size_t MAX_RELEASES = 10;

  std::string text;
  size_t count = 0;

  for (auto const &release : state.releases)
  {
    if (state.installed && release.version <= *state.installed)
      break;

    if (count++ == MAX_RELEASES)
      break;

    text += fmt::format("# {}\n\n{}\n\n", release.tag, release.body.empty() ? "No notes." : release.body);

    // Not installed: only what's new in the latest
    if (!state.installed)
      break;
  }

  // Up to date: show the installed release's notes
  if (text.empty() && state.latest())
    text = fmt::format("# {}\n\n{}", state.latest()->tag, state.latest()->body);

  return text;
}

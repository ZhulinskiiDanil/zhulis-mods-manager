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
  return status == Status::Downloading || status == Status::Downloaded;
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
    // Keep download state across refreshes
    if (auto old = findMut(entry.id); old && isBusy(old->status))
    {
      old->entry = std::move(entry);
      mods.push_back(std::move(*old));
      continue;
    }

    mods.push_back({std::move(entry)});
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

  state->pending = 2;

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

  if (auto mod = Loader::get()->getInstalledMod(state.entry.id))
    state.installed = mod->getVersion();
  else
    state.installed.reset();

  auto latest = state.latest();
  auto const &index = state.indexVersion;
  auto const &installed = state.installed;

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

  if (!state || state->status == Status::Downloading || state->releases.empty())
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

  state->status = Status::Downloading;
  state->progress = 0.f;
  state->missingDeps.clear();
  m_restartPrompted.erase(id);
  notify();

  // Progress callbacks already run on the main thread
  auto onProgress = [this, id](web::WebProgress const &progress)
  {
    if (auto state = findMut(id))
      state->progress = progress.downloadProgress().value_or(0.f) / 100.f;
  };

  m_downloadTasks[id].spawn(
      github::download(release.assetUrl, std::move(onProgress)),
      [this, id, release](web::WebResponse response)
      { onDownload(id, release, response); });
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

  auto target = dirs::getModsDir() / (id + ".geode");
  auto temp = dirs::getModsDir() / (id + ".geode.tmp");

  if (auto res = response.into(temp); res.isErr())
    return failInstall(*state, fmt::format("Can't save the file: {}", res.unwrapErr()));

  std::error_code ec;

  // ! --- Compatibility --- !
  auto metadata = ModMetadata::createFromGeodeFile(temp);
  std::optional<std::string> invalid;

  if (metadata.hasErrors())
    invalid = fmt::format("Invalid mod package: {}", metadata.getErrors().front());
  else if (metadata.getID() != id)
    invalid = fmt::format("The package is for another mod ({})", metadata.getID());
  else if (auto compatible = metadata.checkTargetVersions(); compatible.isErr())
    invalid = fmt::format("{} isn't compatible: {}", release.tag, compatible.unwrapErr());

  if (invalid)
  {
    std::filesystem::remove(temp, ec);
    return failInstall(*state, *invalid);
  }

  std::filesystem::rename(temp, target, ec);

  if (ec)
  {
    std::filesystem::remove(temp, ec);
    return failInstall(*state, "Can't replace the installed mod file");
  }

  state->status = Status::Downloaded;
  state->downloadedTag = release.tag;
  state->error.clear();
  state->missingDeps = findMissingDeps(metadata, m_mods);
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
    return "Restart to apply";
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

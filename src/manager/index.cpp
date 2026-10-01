#include "index.hpp"

using namespace manager;

Manager &Manager::get()
{
  // Leaked on purpose: running tasks must not be destroyed at exit
  static auto instance = new Manager();
  return *instance;
}

// ! --- Fetching --- !

void Manager::refresh()
{
  if (m_loading)
    return;

  m_loading = true;
  m_releaseTasks.clear();

  for (auto &state : m_mods)
  {
    if (state.status != Status::Downloading && state.status != Status::Downloaded)
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
    fetchRelease(state.entry.id);
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
    if (auto old = findMut(entry.id); old && (old->status == Status::Downloading || old->status == Status::Downloaded))
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

void Manager::fetchRelease(std::string const &id)
{
  auto state = findMut(id);

  if (!state)
    return;

  m_releaseTasks[id].spawn(github::fetchReleases(state->entry.repo), [this, id](web::WebResponse response)
                           { onRelease(id, response); });
}

void Manager::onRelease(std::string const &id, web::WebResponse const &response)
{
  if (auto state = findMut(id))
  {
    auto release = github::parseLatest(
        response, id, Mod::get()->getSettingValue<bool>("include-prereleases"));

    if (release.isOk())
    {
      state->release = release.unwrap();
      state->error.clear();
    }
    else
    {
      state->release.reset();
      state->error = release.unwrapErr();
      log::warn("{}: {}", id, state->error);
    }

    updateStatus(*state);
  }

  m_loading = false;

  for (auto const &mod : m_mods)
  {
    if (mod.status == Status::Loading)
      m_loading = true;
  }

  m_loaded = !m_loading;

  notify();
}

void Manager::updateStatus(ModState &state)
{
  if (state.status == Status::Downloading || state.status == Status::Downloaded)
    return;

  if (auto mod = Loader::get()->getInstalledMod(state.entry.id))
    state.installed = mod->getVersion();
  else
    state.installed.reset();

  if (!state.release)
    state.status = state.error == "No releases yet" ? Status::NoRelease : Status::Error;
  else if (!state.installed)
    state.status = Status::NotInstalled;
  // A newer build from the Geode Index is left alone
  else if (*state.installed < state.release->version)
    state.status = Status::UpdateAvailable;
  else
    state.status = Status::UpToDate;
}

// ! --- Installing --- !

void Manager::install(std::string const &id)
{
  auto state = findMut(id);

  if (!state || !state->release || state->status == Status::Downloading)
    return;

  state->status = Status::Downloading;
  notify();

  m_downloadTasks[id].spawn(github::download(state->release->assetUrl), [this, id](web::WebResponse response)
                            { onDownload(id, response); });
}

void Manager::onDownload(std::string const &id, web::WebResponse const &response)
{
  auto state = findMut(id);

  if (!state)
    return;

  if (!response.ok())
    return failInstall(*state, fmt::format("Download failed ({})", response.code()));

  auto target = dirs::getModsDir() / (id + ".geode");
  auto temp = dirs::getModsDir() / (id + ".geode.tmp");

  if (auto res = response.into(temp); res.isErr())
    return failInstall(*state, fmt::format("Can't save the file: {}", res.unwrapErr()));

  std::error_code ec;
  std::filesystem::rename(temp, target, ec);

  if (ec)
  {
    std::filesystem::remove(temp, ec);
    return failInstall(*state, "Can't replace the installed mod file");
  }

  state->status = Status::Downloaded;
  state->error.clear();
  notify();

  createQuickPopup(
      "Restart required",
      fmt::format("<cg>{}</c> {} was installed. Restart the game to load it.",
                  state->entry.name, state->release->tag),
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

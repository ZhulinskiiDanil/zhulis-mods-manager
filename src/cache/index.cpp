#include "index.hpp"

using namespace geode::prelude;

static int64_t now()
{
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool cache::isFresh(Entry const &entry)
{
  auto age = now() - entry.fetchedAt;
  return age >= 0 && age < std::chrono::duration_cast<std::chrono::seconds>(TTL).count();
}

static std::filesystem::path pathFor(std::string const &repo)
{
  auto name = repo;
  std::ranges::replace(name, '/', '_');

  return Mod::get()->getSaveDir() / "releases-cache" / (name + ".json");
}

std::optional<cache::Entry> cache::load(std::string const &repo)
{
  auto json = utils::file::readJson(pathFor(repo));

  if (json.isErr())
    return std::nullopt;

  auto etag = json.unwrap()["etag"].asString();
  auto body = json.unwrap()["body"].asString();

  if (etag.isErr() || body.isErr())
    return std::nullopt;

  return Entry{etag.unwrap(), body.unwrap(), json.unwrap()["fetchedAt"].as<int64_t>().unwrapOr(0)};
}

void cache::save(std::string const &repo, Entry const &entry)
{
  auto path = pathFor(repo);
  (void)utils::file::createDirectoryAll(path.parent_path());

  auto json = matjson::makeObject({{"etag", entry.etag}, {"body", entry.body}, {"fetchedAt", now()}});

  if (auto res = utils::file::writeStringSafe(path, json.dump(matjson::NO_INDENTATION)); res.isErr())
    log::warn("Can't cache releases of {}: {}", repo, res.unwrapErr());
}

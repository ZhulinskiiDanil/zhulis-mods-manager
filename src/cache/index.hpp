#pragma once

#include <Geode/Geode.hpp>

// Last GitHub releases response per repo.
// Unauthenticated requests are limited to 60 an hour, and a 304 counts too
namespace cache
{
  // Releases fetched more recently than this are reused without a request
  constexpr auto TTL = std::chrono::minutes(15);

  struct Entry
  {
    std::string etag;
    std::string body;
    int64_t fetchedAt = 0; // unix seconds
  };

  bool isFresh(Entry const &entry);

  std::optional<Entry> load(std::string const &repo);
  void save(std::string const &repo, Entry const &entry);
}

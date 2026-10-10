#pragma once

#include <Geode/Geode.hpp>

#include "../../manager/index.hpp"

using namespace geode::prelude;

// The nightly build and every GitHub release of a mod, to install a specific one or roll back
class VersionsPopup : public geode::Popup
{
private:
  std::string m_modID;
  ScrollLayer *m_list = nullptr;
  std::optional<size_t> m_subscription;

  ~VersionsPopup();

  bool init(std::string modID);

  void rebuildList();
  void onRebuild(float);
  CCNode *createRowBase(std::string const &id, float width, float height);
  CCNode *createNightlyRow(manager::ModState const &state, float width);
  CCNode *createRow(manager::ModState const &state, github::Release const &release, float width);
  // The installed version when it isn't one of the releases
  CCNode *createInstalledRow(manager::ModState const &state, float width);
  // The repo on GitHub, for its README, issues and every release note
  CCNode *createRepoRow(manager::ModState const &state, float width);
  // Last in the list, for an installed mod other than the manager
  CCNode *createUninstallRow(manager::ModState const &state, float width);

public:
  static VersionsPopup *create(std::string modID);
};

#pragma once

#include <Geode/Geode.hpp>

#include "../../manager/index.hpp"

using namespace geode::prelude;

// Every GitHub release of a mod, to install a specific one or roll back
class VersionsPopup : public geode::Popup
{
private:
  std::string m_modID;

  bool init(std::string modID);
  CCNode *createRow(manager::ModState const &state, github::Release const &release, float width);

public:
  static VersionsPopup *create(std::string modID);
};

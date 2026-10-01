#pragma once

#include <Geode/Geode.hpp>

#include "../../manager/index.hpp"

using namespace geode::prelude;

class ManagerPopup : public geode::Popup
{
private:
  ScrollLayer *m_list = nullptr;
  CCLabelBMFont *m_statusLabel = nullptr;
  CCMenuItemSpriteExtra *m_updateAllBtn = nullptr;
  std::optional<size_t> m_subscription;
  bool m_built = false;

  ~ManagerPopup();

  bool init();

  void rebuildList();
  void onRebuild(float);
  void onTick(float);
  CCNode *createRow(manager::ModState const &state, float width);

  void onRefresh(CCObject *);
  void onUpdateAll(CCObject *);

public:
  static ManagerPopup *create();
};

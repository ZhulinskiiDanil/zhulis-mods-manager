#pragma once

#include <Geode/Geode.hpp>

#include "../../manager/index.hpp"

using namespace geode::prelude;

class ManagerPopup : public geode::Popup
{
private:
  ScrollLayer *m_list = nullptr;
  CCLabelBMFont *m_statusLabel = nullptr;
  std::optional<size_t> m_subscription;

  ~ManagerPopup();

  bool init();

  void rebuildList();
  void onRebuild(float);
  CCNode *createRow(manager::ModState const &state, float width);

  void onRefresh(CCObject *);

public:
  static ManagerPopup *create();
};

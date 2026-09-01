#pragma once

#include <QList>
#include <memory>

#include "preferences/configobject.h"
#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class KeyboardEventFilter;
class QKeySequenceEdit;
class QLabel;
class QTableWidget;

class DlgPrefKeyboardMapping final : public DlgPreferencePage {
    Q_OBJECT

  public:
    DlgPrefKeyboardMapping(QWidget* pParent,
            UserSettingsPointer pConfig,
            std::shared_ptr<ConfigObject<ConfigValueKbd>> pKeyboardConfig,
            std::shared_ptr<KeyboardEventFilter> pKeyboardEventFilter);

    bool okayToClose() const override;

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private:
    struct Mapping {
        QString category;
        QString action;
        ConfigKey key;
    };

    void loadFromConfig(const ConfigObject<ConfigValueKbd>& config);
    void validateMappings();

    UserSettingsPointer m_pConfig;
    std::shared_ptr<ConfigObject<ConfigValueKbd>> m_pKeyboardConfig;
    std::shared_ptr<KeyboardEventFilter> m_pKeyboardEventFilter;
    QList<Mapping> m_mappings;
    QList<QKeySequenceEdit*> m_editors;
    QTableWidget* m_pTable;
    QLabel* m_pStatusLabel;
    bool m_valid = true;
};

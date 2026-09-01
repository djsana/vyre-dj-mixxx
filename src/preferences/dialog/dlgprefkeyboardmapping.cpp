#include "preferences/dialog/dlgprefkeyboardmapping.h"

#include <QDir>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "moc_dlgprefkeyboardmapping.cpp"

namespace {
ConfigKey vyreKey(const QString& item) {
    return ConfigKey(QStringLiteral("[VYREActiveDeck]"), item);
}
} // namespace

DlgPrefKeyboardMapping::DlgPrefKeyboardMapping(QWidget* pParent,
        UserSettingsPointer pConfig,
        std::shared_ptr<ConfigObject<ConfigValueKbd>> pKeyboardConfig,
        std::shared_ptr<KeyboardEventFilter> pKeyboardEventFilter)
        : DlgPreferencePage(pParent),
          m_pConfig(std::move(pConfig)),
          m_pKeyboardConfig(std::move(pKeyboardConfig)),
          m_pKeyboardEventFilter(std::move(pKeyboardEventFilter)),
          m_pTable(new QTableWidget(this)),
          m_pStatusLabel(new QLabel(this)) {
    m_mappings = {
            {tr("Deck selection"), tr("Select Deck A / B"), ConfigKey("[VYRE]", "select")},
            {tr("Transport"), tr("Play / pause"), vyreKey("play_space")},
            {tr("Transport"), tr("Stop"), vyreKey("stop")},
            {tr("Transport"), tr("Cue"), vyreKey("cue_default")},
            {tr("Tempo"), tr("Reset pitch / tempo"), vyreKey("rate_set_default")},
            {tr("Tempo"), tr("Nudge forward"), vyreKey("rate_temp_up_small")},
            {tr("Tempo"), tr("Nudge backward"), vyreKey("rate_temp_down_small")},
            {tr("Loop"), tr("Enable loop"), vyreKey("beatloop_activate")},
            {tr("Loop"), tr("1 beat"), vyreKey("beatloop_1_toggle")},
            {tr("Loop"), tr("2 beats"), vyreKey("beatloop_2_toggle")},
            {tr("Loop"), tr("4 beats"), vyreKey("beatloop_4_toggle")},
            {tr("Loop"), tr("8 beats"), vyreKey("beatloop_8_toggle")},
            {tr("Loop"), tr("16 beats"), vyreKey("beatloop_16_toggle")},
            {tr("Loop"), tr("32 beats"), vyreKey("beatloop_32_toggle")},
            {tr("Loop"), tr("Halve loop"), vyreKey("loop_halve")},
            {tr("Loop"), tr("Double loop"), vyreKey("loop_double")},
            {tr("Loop"), tr("Reloop"), vyreKey("reloop_toggle")},
            {tr("Effects"), tr("Effect 1"), vyreKey("effect_active_1")},
            {tr("Effects"), tr("Effect 2"), vyreKey("effect_active_2")},
            {tr("Effects"), tr("Effect 3"), vyreKey("effect_active_3")},
            {tr("Effects"), tr("Filter"), vyreKey("filter_activate")},
    };
    for (int sampler = 1; sampler <= 12; ++sampler) {
        m_mappings.append({tr("Sampler"),
                tr("Sampler %1 play / stop").arg(sampler),
                ConfigKey(QStringLiteral("[Sampler%1]").arg(sampler),
                        QStringLiteral("start_stop"))});
    }

    auto* pLayout = new QVBoxLayout(this);
    auto* pTitle = new QLabel(tr("Keyboard Mapping"), this);
    QFont titleFont = pTitle->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    pTitle->setFont(titleFont);
    pLayout->addWidget(pTitle);

    auto* pDescription = new QLabel(tr(
            "Choose a shortcut for each VYRE DJ action. Deck actions control the "
            "deck selected with Tab. Click a shortcut and press the new key; use "
            "the clear button in the shortcut field to remove it."),
            this);
    pDescription->setWordWrap(true);
    pLayout->addWidget(pDescription);

    m_pTable->setColumnCount(3);
    m_pTable->setHorizontalHeaderLabels({tr("Section"), tr("Action"), tr("Shortcut")});
    m_pTable->setRowCount(m_mappings.size());
    m_pTable->verticalHeader()->hide();
    m_pTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setSelectionMode(QAbstractItemView::NoSelection);

    for (int row = 0; row < m_mappings.size(); ++row) {
        const Mapping& mapping = m_mappings.at(row);
        auto* pCategoryItem = new QTableWidgetItem(mapping.category);
        pCategoryItem->setFlags(pCategoryItem->flags() & ~Qt::ItemIsEditable);
        m_pTable->setItem(row, 0, pCategoryItem);
        auto* pActionItem = new QTableWidgetItem(mapping.action);
        pActionItem->setFlags(pActionItem->flags() & ~Qt::ItemIsEditable);
        m_pTable->setItem(row, 1, pActionItem);
        auto* pEditor = new QKeySequenceEdit(m_pTable);
        pEditor->setClearButtonEnabled(true);
        pEditor->setMaximumSequenceLength(1);
        connect(pEditor,
                &QKeySequenceEdit::keySequenceChanged,
                this,
                &DlgPrefKeyboardMapping::validateMappings);
        m_pTable->setCellWidget(row, 2, pEditor);
        m_editors.append(pEditor);
    }
    pLayout->addWidget(m_pTable, 1);

    m_pStatusLabel->setWordWrap(true);
    pLayout->addWidget(m_pStatusLabel);
    slotUpdate();
}

bool DlgPrefKeyboardMapping::okayToClose() const {
    return m_valid;
}

void DlgPrefKeyboardMapping::slotUpdate() {
    loadFromConfig(*m_pKeyboardConfig);
}

void DlgPrefKeyboardMapping::slotApply() {
    validateMappings();
    if (!m_valid) {
        return;
    }

    const QString customPath =
            QDir(m_pConfig->getSettingsPath()).filePath(QStringLiteral("Custom.kbd.cfg"));
    m_pKeyboardConfig->reopen(customPath);
    // VYRE uses Space only for transport. Remove the earlier P binding if a
    // user upgrades from the first VYRE keyboard layout.
    m_pKeyboardConfig->remove(vyreKey(QStringLiteral("play")));

    for (int row = 0; row < m_mappings.size(); ++row) {
        const QKeySequence sequence = m_editors.at(row)->keySequence();
        if (sequence.isEmpty()) {
            m_pKeyboardConfig->remove(m_mappings.at(row).key);
        } else {
            m_pKeyboardConfig->set(
                    m_mappings.at(row).key, ConfigValueKbd(sequence));
        }
    }

    if (!m_pKeyboardConfig->save()) {
        m_valid = false;
        m_pStatusLabel->setText(tr("VYRE DJ could not save the keyboard mapping."));
        m_pStatusLabel->setStyleSheet(QStringLiteral("color: #e66b6b;"));
        return;
    }

    if (m_pConfig->getValue<bool>(ConfigKey("[Keyboard]", "Enabled"), true)) {
        m_pKeyboardEventFilter->setKeyboardConfig(m_pKeyboardConfig.get());
    }
    m_pStatusLabel->setText(tr("Custom keyboard mapping saved and applied."));
    m_pStatusLabel->setStyleSheet(QStringLiteral("color: #35c9a5;"));
}

void DlgPrefKeyboardMapping::slotResetToDefaults() {
    const QString defaultPath = QDir(m_pConfig->getResourcePath())
                                        .filePath(QStringLiteral("keyboard/en_US.kbd.cfg"));
    ConfigObject<ConfigValueKbd> defaultConfig(defaultPath);
    loadFromConfig(defaultConfig);
    m_pStatusLabel->setText(tr("VYRE defaults loaded. Select Apply to save them."));
}

void DlgPrefKeyboardMapping::loadFromConfig(
        const ConfigObject<ConfigValueKbd>& config) {
    for (int row = 0; row < m_mappings.size(); ++row) {
        m_editors.at(row)->setKeySequence(
                QKeySequence(config.getValueString(m_mappings.at(row).key)));
    }
    validateMappings();
}

void DlgPrefKeyboardMapping::validateMappings() {
    QSet<QString> shortcuts;
    QString duplicate;
    for (QKeySequenceEdit* pEditor : std::as_const(m_editors)) {
        const QString shortcut =
                pEditor->keySequence().toString(QKeySequence::PortableText);
        if (shortcut.isEmpty()) {
            continue;
        }
        if (shortcuts.contains(shortcut)) {
            duplicate = shortcut;
            break;
        }
        shortcuts.insert(shortcut);
    }

    m_valid = duplicate.isEmpty();
    if (m_valid) {
        m_pStatusLabel->clear();
        m_pStatusLabel->setStyleSheet(QString());
    } else {
        m_pStatusLabel->setText(
                tr("%1 is assigned more than once. Choose a different shortcut.")
                        .arg(duplicate));
        m_pStatusLabel->setStyleSheet(QStringLiteral("color: #e66b6b;"));
    }
}

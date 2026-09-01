#include "controllers/keyboard/keyboardeventfilter.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QtDebug>

#include "moc_keyboardeventfilter.cpp"
#include "util/cmdlineargs.h"

namespace {

bool isTextEntryObject(const QObject* object) {
    for (const QObject* current = object; current; current = current->parent()) {
        if (qobject_cast<const QLineEdit*>(current) ||
                qobject_cast<const QTextEdit*>(current) ||
                qobject_cast<const QPlainTextEdit*>(current) ||
                qobject_cast<const QAbstractSpinBox*>(current)) {
            return true;
        }
        const auto* comboBox = qobject_cast<const QComboBox*>(current);
        if (comboBox && comboBox->isEditable()) {
            return true;
        }
    }
    return false;
}

int physicalKeyId(const QKeyEvent* event) {
#ifdef __APPLE__
    return event->key();
#else
    return event->nativeScanCode();
#endif
}

} // namespace

// The legacy skin parser installs KeyboardEventFilter on individual widgets.
// A focused Qt control can still consume Space or Tab before that widget-level
// filter sees it. This narrow application filter reserves only VYRE's selected
// deck transport and deck-selection mappings, while leaving text entry alone.
class VyreGlobalKeyFilter final : public QObject {
  public:
    explicit VyreGlobalKeyFilter(KeyboardEventFilter* keyboard)
            : QObject(keyboard), m_pKeyboard(keyboard) {
    }

  protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        return m_pKeyboard->handleVyreGlobalEvent(object, event);
    }

  private:
    KeyboardEventFilter* const m_pKeyboard;
};

KeyboardEventFilter::KeyboardEventFilter(ConfigObject<ConfigValueKbd>* pKbdConfigObject,
        QObject* parent,
        const char* name)
        : QObject(parent),
#ifndef __APPLE__
          m_altPressedWithoutKey(false),
#endif
          m_pKbdConfigObject(nullptr),
          m_pVyreActiveDeck(std::make_unique<ControlObject>(
                  ConfigKey(QStringLiteral("[VYRE]"), QStringLiteral("active_deck")))),
          m_pVyreDeck1Selected(std::make_unique<ControlObject>(
                  ConfigKey(QStringLiteral("[VYRE]"), QStringLiteral("deck1_selected")))),
          m_pVyreDeck2Selected(std::make_unique<ControlObject>(
                  ConfigKey(QStringLiteral("[VYRE]"), QStringLiteral("deck2_selected")))) {
    setObjectName(name);
    m_pVyreActiveDeck->set(1.0);
    m_pVyreDeck1Selected->set(1.0);
    m_pVyreDeck2Selected->set(0.0);
    connect(m_pVyreDeck1Selected.get(),
            &ControlObject::valueChanged,
            this,
            [this](double value) {
                if (value > 0.0) {
                    selectVyreDeck(1);
                } else if (m_vyreActiveDeck == 1) {
                    m_pVyreDeck1Selected->set(1.0);
                }
            });
    connect(m_pVyreDeck2Selected.get(),
            &ControlObject::valueChanged,
            this,
            [this](double value) {
                if (value > 0.0) {
                    selectVyreDeck(2);
                } else if (m_vyreActiveDeck == 2) {
                    m_pVyreDeck2Selected->set(1.0);
                }
    });
    setKeyboardConfig(pKbdConfigObject);
    m_pVyreGlobalKeyFilter = std::make_unique<VyreGlobalKeyFilter>(this);
    if (auto* application = QCoreApplication::instance()) {
        application->installEventFilter(m_pVyreGlobalKeyFilter.get());
    }
}

KeyboardEventFilter::~KeyboardEventFilter() {
}

bool KeyboardEventFilter::handleVyreGlobalEvent(QObject* object, QEvent* event) {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return false;
    }

    if (isTextEntryObject(object) || isTextEntryObject(QApplication::focusWidget())) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int keyId = physicalKeyId(keyEvent);
    if (event->type() == QEvent::KeyRelease) {
        return m_vyreOneShotKeys.remove(keyId);
    }

    const QKeySequence sequence = getKeySeq(keyEvent);
    if (sequence.isEmpty()) {
        return false;
    }

    const ConfigValueKbd configuredSequence(sequence);
    bool handled = false;
    for (auto it = m_keySequenceToControlHash.constFind(configuredSequence);
         it != m_keySequenceToControlHash.constEnd() && it.key() == configuredSequence;
         ++it) {
        const ConfigKey& configKey = it.value();
        if (configKey.group == "[VYRE]" && configKey.item == "select") {
            if (!keyEvent->isAutoRepeat()) {
                selectNextVyreDeck();
            }
            handled = true;
        } else if (configKey.group == "[VYREActiveDeck]" &&
                configKey.item == "play_space") {
            if (!keyEvent->isAutoRepeat()) {
                toggleVyrePlay();
            }
            handled = true;
        }
    }

    if (handled) {
        m_vyreOneShotKeys.insert(keyId);
    }
    return handled;
}

bool KeyboardEventFilter::eventFilter(QObject*, QEvent* e) {
    if (e->type() == QEvent::FocusOut) {
        // If we lose focus, we need to clear out the active key list
        // because we might not get Key Release events.
        m_qActiveKeyList.clear();
        m_vyreOneShotKeys.clear();
    } else if (e->type() == QEvent::KeyPress) {
        QKeyEvent* ke = (QKeyEvent *)e;

#ifdef __APPLE__
        // On Mac OSX the nativeScanCode is empty (const 1) http://doc.qt.nokia.com/4.7/qkeyevent.html#nativeScanCode
        // We may loose the release event if a the shift key is pressed later
        // and there is character shift like "1" -> "!"
        int keyId = ke->key();
#else
        int keyId = ke->nativeScanCode();
#endif

        if (shouldSkipHeldKey(keyId)) {
            return true;
        }

        QKeySequence ks = getKeySeq(ke);
        if (!ks.isEmpty()) {
#ifndef __APPLE__
            m_altPressedWithoutKey = false;
#endif
            ConfigValueKbd ksv(ks);
            // Check if a shortcut is defined
            bool result = false;
            // using const_iterator here is faster than QMultiHash::values()
            for (auto it = m_keySequenceToControlHash.constFind(ksv);
                 it != m_keySequenceToControlHash.constEnd() && it.key() == ksv; ++it) {
                const ConfigKey& configKey = it.value();
                if (configKey.group != "[KeyboardShortcuts]") {
                    if (configKey.group == "[VYRE]" && configKey.item == "select") {
                        if (!ke->isAutoRepeat()) {
                            selectNextVyreDeck();
                        }
                        result = true;
                        continue;
                    }

                    if (configKey.group == "[VYREActiveDeck]" &&
                            configKey.item == "play_space") {
                        // Transport is a one-shot action in VYRE. Do not route
                        // it through the generic MIDI NoteOn/NoteOff path: a
                        // held Space key may auto-repeat and its release may
                        // otherwise activate the widget that currently has
                        // keyboard focus.
                        m_vyreOneShotKeys.insert(keyId);
                        if (!ke->isAutoRepeat()) {
                            toggleVyrePlay();
                        }
                        result = true;
                        continue;
                    }

                    ControlObject* control = resolveVyreControl(configKey);
                    if (control) {
                        //qDebug() << configKey << "MidiOpCode::NoteOn" << 1;
                        // Add key to active key list
                        m_qActiveKeyList.append(KeyDownInformation(
                            keyId, ke->modifiers(), control));
                        // Since setting the value might cause us to go down
                        // a route that would eventually clear the active
                        // key list, do that last.
                        control->setValueFromMidi(MidiOpCode::NoteOn, 1);
                        result = true;
                    } else {
                        qDebug() << "Warning: Keyboard key is configured for nonexistent control:"
                                 << configKey.group << configKey.item;
                    }
                }
            }
            return result;
#ifndef __APPLE__
        } else {
            // getKeySeq() returns empty string if the press was a modifier only
            // On most system Alt sends Alt + Qt::Key_Alt, but with Qt 6.9 (on Linux)
            // this changed apparently so that it's just Qt::Key_Alt
            if (((ke->modifiers() & Qt::AltModifier) || ke->key() == Qt::Key_Alt) &&
                    !m_altPressedWithoutKey) {
                m_altPressedWithoutKey = true;
            }
#endif
        }
    } else if (e->type() == QEvent::KeyRelease) {
        QKeyEvent* ke = (QKeyEvent*)e;

#ifndef __APPLE__
        // QAction hotkeys are consumed by the object that created them, e.g.
        // WMainMenuBar, so we will not receive menu hotkey keypress events here.
        // However, it may happen that we receive a RELEASE event for an Alt+key
        // combo for which no KEYPRESS was registered.
        // So react only to Alt-only releases.
        if (m_altPressedWithoutKey && ke->key() == Qt::Key_Alt) {
            emit altPressedWithoutKeys();
        }
        m_altPressedWithoutKey = false;
#endif

#ifdef __APPLE__
        // On Mac OSX the nativeScanCode is empty
        int keyId = ke->key();
#else
        int keyId = ke->nativeScanCode();
#endif
        if (m_vyreOneShotKeys.remove(keyId)) {
            return true;
        }
        bool autoRepeat = ke->isAutoRepeat();

        //qDebug() << "KeyRelease event =" << ke->key() << "AutoRepeat =" << autoRepeat << "KeyId =" << keyId;

        int clearModifiers = 0;
#ifdef __APPLE__
        // OS X apparently doesn't deliver KeyRelease events when you are
        // holding Ctrl. So release all key-presses that were triggered with
        // Ctrl.
        if (ke->key() == Qt::Key_Control) {
            clearModifiers = Qt::ControlModifier;
        }
#endif

        bool matched = false;
        // Run through list of active keys to see if the released key is active
        for (int i = m_qActiveKeyList.size() - 1; i >= 0; i--) {
            const KeyDownInformation& keyDownInfo = m_qActiveKeyList[i];
            ControlObject* pControl = keyDownInfo.pControl;
            if (keyDownInfo.keyId == keyId ||
                    (clearModifiers > 0 && keyDownInfo.modifiers == clearModifiers)) {
                if (!autoRepeat) {
                    //qDebug() << pControl->getKey() << "MidiOpCode::NoteOff" << 0;
                    pControl->setValueFromMidi(MidiOpCode::NoteOff, 0);
                    m_qActiveKeyList.removeAt(i);
                }
                // Due to the modifier clearing workaround we might match multiple keys for
                // release.
                matched = true;
            }
        }
        return matched;
    } else if (e->type() == QEvent::KeyboardLayoutChange) {
        // This event is not fired on ubunty natty, why?
        // TODO(XXX): find a way to support KeyboardLayoutChange Bug #997811
        //qDebug() << "QEvent::KeyboardLayoutChange";
    }
    return false;
}

// static
QKeySequence KeyboardEventFilter::getKeySeq(QKeyEvent* e) {
    if ((e->key() >= Qt::Key_Shift && e->key() <= Qt::Key_Alt) ||
            e->key() == Qt::Key_AltGr) {
        // Do not act on Modifier only, Shift, Ctrl, Meta, Alt and AltGr
        // avoid returning "khmer vowel sign ie (U+17C0)"
        return {};
    }

    // Note: test for individual modifiers, don't use e->modifiers() for composing
    // the QKeySequence because on macOS arrow key events are sent with the Num
    // modifier for some reason. This result in a key sequence for which there
    // would be no match in our keyseq/control hash.
    // See https://github.com/mixxxdj/mixxx/issues/13305
    QString modseq;
    if (e->modifiers() & Qt::ShiftModifier) {
        modseq += "Shift+";
    }
    if (e->modifiers() & Qt::ControlModifier) {
        modseq += "Ctrl+";
    }
    if (e->modifiers() & Qt::AltModifier) {
        modseq += "Alt+";
    }
    if (e->modifiers() & Qt::MetaModifier) {
        modseq += "Meta+";
    }

    const QString keyseq = QKeySequence(e->key()).toString();
    const QKeySequence k = QKeySequence(modseq + keyseq);

    if (CmdlineArgs::Instance().getDeveloper()) {
        if (e->type() == QEvent::KeyPress) {
            qDebug() << "keyboard press: " << k.toString();
        } else if (e->type() == QEvent::KeyRelease) {
            qDebug() << "keyboard release: " << k.toString();
        }
    }

    return k;
}

void KeyboardEventFilter::setKeyboardConfig(ConfigObject<ConfigValueKbd>* pKbdConfigObject) {
    // Keyboard configs are a surjection from ConfigKey to key sequence. We
    // invert the mapping to create an injection from key sequence to
    // ConfigKey. This allows a key sequence to trigger multiple controls in
    // Mixxx.
    m_keySequenceToControlHash = pKbdConfigObject->transpose();
    m_pKbdConfigObject = pKbdConfigObject;
}

ConfigObject<ConfigValueKbd>* KeyboardEventFilter::getKeyboardConfig() {
    return m_pKbdConfigObject;
}

ControlObject* KeyboardEventFilter::resolveVyreControl(const ConfigKey& configKey) {
    if (configKey.group != "[VYREActiveDeck]") {
        return ControlObject::getControl(configKey);
    }

    const QString channelGroup = QStringLiteral("[Channel%1]").arg(m_vyreActiveDeck);

    if (configKey.item == "filter_activate") {
        return ControlObject::getControl(ConfigKey(
                QStringLiteral("[QuickEffectRack1_%1_Effect1]").arg(channelGroup),
                QStringLiteral("enabled")));
    }

    if (configKey.item.startsWith(QStringLiteral("effect_active_"))) {
        bool ok = false;
        const int effectSlot = configKey.item.sliced(14).toInt(&ok);
        if (!ok || effectSlot < 1 || effectSlot > 3) {
            return nullptr;
        }

        const QString effectUnitGroup =
                QStringLiteral("[EffectRack1_EffectUnit%1]").arg(m_vyreActiveDeck);
        ControlObject::set(ConfigKey(effectUnitGroup,
                                   QStringLiteral("group_%1_enable").arg(channelGroup)),
                1.0);
        return ControlObject::getControl(ConfigKey(
                QStringLiteral("[EffectRack1_EffectUnit%1_Effect%2]")
                        .arg(m_vyreActiveDeck)
                        .arg(effectSlot),
                QStringLiteral("enabled")));
    }

    return ControlObject::getControl(ConfigKey(channelGroup, configKey.item));
}

void KeyboardEventFilter::toggleVyrePlay() {
    const ConfigKey playKey(
            QStringLiteral("[Channel%1]").arg(m_vyreActiveDeck),
            QStringLiteral("play"));
    const bool wasPlaying = ControlObject::toBool(playKey);
    ControlObject::set(playKey, wasPlaying ? 0.0 : 1.0);
    qInfo() << "VYRE Space transport:" << (wasPlaying ? "pause" : "play")
            << "deck" << m_vyreActiveDeck;
}

void KeyboardEventFilter::selectNextVyreDeck() {
    selectVyreDeck(m_vyreActiveDeck == 1 ? 2 : 1);
}

void KeyboardEventFilter::selectVyreDeck(int deck) {
    if (deck != 1 && deck != 2) {
        return;
    }
    m_vyreActiveDeck = deck;
    m_pVyreActiveDeck->set(static_cast<double>(m_vyreActiveDeck));
    m_pVyreDeck1Selected->set(m_vyreActiveDeck == 1 ? 1.0 : 0.0);
    m_pVyreDeck2Selected->set(m_vyreActiveDeck == 2 ? 1.0 : 0.0);
    qInfo() << "VYRE keyboard deck selected:" << m_vyreActiveDeck;
}

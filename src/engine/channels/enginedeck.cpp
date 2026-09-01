#include "engine/channels/enginedeck.h"

#include "control/controlpushbutton.h"
#include "effects/effectsmanager.h"
#include "engine/effects/engineeffectsmanager.h"
#include "engine/enginebuffer.h"
#include "engine/enginepregain.h"
#include "moc_enginedeck.cpp"
#include "util/sample.h"

EngineDeck::EngineDeck(
        const ChannelHandleAndGroup& handleGroup,
        UserSettingsPointer pConfig,
        EngineMixer* pMixingEngine,
        EffectsManager* pEffectsManager,
        EngineChannel::ChannelOrientation defaultOrientation,
        bool primaryDeck)
        : EngineChannel(handleGroup, defaultOrientation, pEffectsManager,
                  /*isTalkoverChannel*/ false,
                  primaryDeck),
          m_pConfig(pConfig),
          m_pStemVocals(new ControlPushButton(
                  ConfigKey(getGroup(), "stem_vocals"))),
          m_pStemInstrumental(new ControlPushButton(
                  ConfigKey(getGroup(), "stem_instrumental"))),
          m_pInputConfigured(new ControlObject(ConfigKey(getGroup(), "input_configured"))),
          m_pPassing(new ControlPushButton(ConfigKey(getGroup(), "passthrough"))) {
    m_pStemVocals->setButtonMode(ControlPushButton::TOGGLE);
    m_pStemInstrumental->setButtonMode(ControlPushButton::TOGGLE);
    connect(m_pStemVocals,
            &ControlObject::valueChanged,
            this,
            [this](double value) {
                if (value > 0.0 && m_pStemInstrumental->toBool()) {
                    m_pStemInstrumental->set(0.0);
                }
            });
    connect(m_pStemInstrumental,
            &ControlObject::valueChanged,
            this,
            [this](double value) {
                if (value > 0.0 && m_pStemVocals->toBool()) {
                    m_pStemVocals->set(0.0);
                }
            });
    m_pInputConfigured->setReadOnly();
    // Set up passthrough utilities and fields
    m_pPassing->setButtonMode(ControlPushButton::POWERWINDOW);
    m_bPassthroughIsActive = false;
    m_bPassthroughWasActive = false;

    // Ensure that input is configured before enabling passthrough
    m_pPassing->connectValueChangeRequest(
            this,
            &EngineDeck::slotPassthroughChangeRequest,
            Qt::DirectConnection);

    m_pPregain = new EnginePregain(getGroup());
    m_pBuffer = new EngineBuffer(getGroup(), pConfig, this, pMixingEngine);
}

EngineDeck::~EngineDeck() {
    delete m_pStemInstrumental;
    delete m_pStemVocals;
    delete m_pPassing;
    delete m_pBuffer;
    delete m_pPregain;
}

void EngineDeck::process(CSAMPLE* pOut, const int iBufferSize) {
    // Feed the incoming audio through if passthrough is active
    const CSAMPLE* sampleBuffer = m_sampleBuffer; // save pointer on stack
    if (isPassthroughActive() && sampleBuffer) {
        SampleUtil::copy(pOut, sampleBuffer, iBufferSize);
        m_bPassthroughWasActive = true;
        m_sampleBuffer = nullptr;
        m_pPregain->setSpeedAndScratching(1, false);
    } else {
        // If passthrough is no longer enabled, zero out the buffer
        if (m_bPassthroughWasActive) {
            SampleUtil::clear(pOut, iBufferSize);
            m_bPassthroughWasActive = false;
            return;
        }

        // Process the raw audio
        m_pBuffer->process(pOut, iBufferSize);
        m_pPregain->setSpeedAndScratching(m_pBuffer->getSpeed(), m_pBuffer->getScratching());
        m_bPassthroughWasActive = false;
    }

    // Apply pregain
    m_pPregain->process(pOut, iBufferSize);

    // VYRE Stem Lite uses mid/side separation rather than a neural model. It
    // is intentionally cheap enough to run inside the audio callback without
    // analysis jobs, model files, or extra latency.
    processStemLite(pOut, iBufferSize);

    EngineEffectsManager* pEngineEffectsManager = m_pEffectsManager->getEngineEffectsManager();
    if (pEngineEffectsManager != nullptr) {
        pEngineEffectsManager->processPreFaderInPlace(m_group.handle(),
                m_pEffectsManager->getMainHandle(),
                pOut,
                iBufferSize,
                mixxx::audio::SampleRate::fromDouble(m_sampleRate.get()));
    }

    // Update VU meter
    m_vuMeter.process(pOut, iBufferSize);
}

void EngineDeck::processStemLite(CSAMPLE* pOutput, int sampleCount) {
    constexpr int kStereoSamplesPerFrame = 2;
    const int frameCount = sampleCount / kStereoSamplesPerFrame;
    if (frameCount <= 0) {
        return;
    }

    const CSAMPLE_GAIN vocalTarget = m_pStemVocals->toBool() ? 1.0f : 0.0f;
    const CSAMPLE_GAIN instrumentalTarget =
            m_pStemInstrumental->toBool() ? 1.0f : 0.0f;
    const CSAMPLE_GAIN vocalStep =
            (vocalTarget - m_stemVocalMix) / frameCount;
    const CSAMPLE_GAIN instrumentalStep =
            (instrumentalTarget - m_stemInstrumentalMix) / frameCount;

    for (int frame = 0; frame < frameCount; ++frame) {
        m_stemVocalMix += vocalStep;
        m_stemInstrumentalMix += instrumentalStep;

        const int leftIndex = frame * kStereoSamplesPerFrame;
        const int rightIndex = leftIndex + 1;
        CSAMPLE left = pOutput[leftIndex];
        CSAMPLE right = pOutput[rightIndex];
        const CSAMPLE mid = (left + right) * 0.5f;
        const CSAMPLE side = (left - right) * 0.5f;

        left += (mid - left) * m_stemVocalMix;
        right += (mid - right) * m_stemVocalMix;
        left += (side - left) * m_stemInstrumentalMix;
        right += (-side - right) * m_stemInstrumentalMix;

        pOutput[leftIndex] = left;
        pOutput[rightIndex] = right;
    }

    m_stemVocalMix = vocalTarget;
    m_stemInstrumentalMix = instrumentalTarget;
}

void EngineDeck::collectFeatures(GroupFeatureState* pGroupFeatures) const {
    m_pBuffer->collectFeatures(pGroupFeatures);
    m_vuMeter.collectFeatures(pGroupFeatures);
    m_pPregain->collectFeatures(pGroupFeatures);
}

void EngineDeck::postProcessLocalBpm() {
    m_pBuffer->postProcessLocalBpm();
}

void EngineDeck::postProcess(const int iBufferSize) {
    m_pBuffer->postProcess(iBufferSize);
}

EngineBuffer* EngineDeck::getEngineBuffer() {
    return m_pBuffer;
}

EngineChannel::ActiveState EngineDeck::updateActiveState() {
    bool active = false;
    if (m_bPassthroughWasActive && !m_bPassthroughIsActive) {
        active = true;
    } else {
        active = m_pBuffer->isTrackLoaded() || isPassthroughActive();
    }

    if (active) {
        m_active = true;
        return ActiveState::Active;
    }
    if (m_active) {
        m_vuMeter.reset();
        m_active = false;
        return ActiveState::WasActive;
    }
    return ActiveState::Inactive;
}

void EngineDeck::receiveBuffer(
        const AudioInput& input, const CSAMPLE* pBuffer, unsigned int nFrames) {
    Q_UNUSED(input);
    Q_UNUSED(nFrames);
    // Skip receiving audio input if passthrough is not active
    if (!m_bPassthroughIsActive) {
        m_sampleBuffer = nullptr;
        return;
    } else {
        m_sampleBuffer = pBuffer;
    }
}

void EngineDeck::onInputConfigured(const AudioInput& input) {
    if (input.getType() != AudioPathType::VinylControl) {
        // This is an error!
        qDebug() << "WARNING: EngineDeck connected to AudioInput for a non-vinylcontrol type!";
        return;
    }
    m_pInputConfigured->forceSet(1.0);
    m_sampleBuffer = nullptr;
}

void EngineDeck::onInputUnconfigured(const AudioInput& input) {
    if (input.getType() != AudioPathType::VinylControl) {
        // This is an error!
        qDebug() << "WARNING: EngineDeck connected to AudioInput for a non-vinylcontrol type!";
        return;
    }
    m_pInputConfigured->forceSet(0.0);
    m_sampleBuffer = nullptr;
}

bool EngineDeck::isPassthroughActive() const {
    return (m_bPassthroughIsActive && m_sampleBuffer);
}

void EngineDeck::slotPassthroughToggle(double v) {
    m_bPassthroughIsActive = v > 0;
}

void EngineDeck::slotPassthroughChangeRequest(double v) {
    if (v <= 0 || m_pInputConfigured->get() > 0) {
        m_pPassing->setAndConfirm(v);

        // Pass confirmed value to slotPassthroughToggle. We cannot use the
        // valueChanged signal for this, because the change originates from the
        // same ControlObject instance.
        slotPassthroughToggle(v);
    } else {
        emit noPassthroughInputConfigured();
    }
}

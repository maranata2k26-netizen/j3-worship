#include "MainComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto background = 0xff0b0e13;
constexpr auto topBar = 0xff11151c;
constexpr auto panel = 0xff151a22;
constexpr auto panel2 = 0xff1d2430;
constexpr auto panel3 = 0xff252e3b;
constexpr auto accent = 0xff4da3ff;
constexpr auto accentDeep = 0xff286da8;
constexpr auto good = 0xff43d17d;
constexpr auto warning = 0xffffb84d;
constexpr auto danger = 0xffff5b68;
constexpr auto text = 0xfff3f6fa;
constexpr auto mutedText = 0xff9da8b7;

float dbToGain(double db)
{
    return static_cast<float>(juce::Decibels::decibelsToGain(db, -90.0));
}

juce::String outputName(juce::AudioIODevice& device, int index)
{
    const auto names = device.getOutputChannelNames();
    if (index >= 0 && index < names.size() && names[index].isNotEmpty())
        return names[index];
    return "OUT " + juce::String(index + 1);
}
}

MainComponent::MixerStrip::MixerStrip(int index, const juce::String& title,
                                      std::atomic<float>& gain, std::atomic<float>& pan,
                                      std::atomic<bool>& muted, std::atomic<float>& meter,
                                      std::atomic<int>& bus, std::atomic<int>& dca)
    : index_(index), gain_(gain), pan_(pan), muted_(muted), meter_(meter),
      bus_(bus), dca_(dca), meterBar_(meterValue_)
{
    setOpaque(false);

    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    addAndMakeVisible(title_);

    fader_.setSliderStyle(juce::Slider::LinearVertical);
    fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 22);
    fader_.setRange(-60.0, 6.0, 0.1);
    fader_.setDoubleClickReturnValue(true, 0.0);
    fader_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    fader_.setColour(juce::Slider::trackColourId, juce::Colour(accentDeep));
    fader_.onValueChange = [this] { gain_.store(dbToGain(fader_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(fader_);

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 20);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDoubleClickReturnValue(true, 0.0);
    panSlider_.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accent));
    panSlider_.onValueChange = [this] { pan_.store(static_cast<float>(panSlider_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(panSlider_);

    muteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    muteButton_.onClick = [this] { muted_.store(muteButton_.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(muteButton_);

    busBox_.addItem("MASTER", 1);
    for (int i = 0; i < kBuses; ++i) busBox_.addItem("BUS " + juce::String(i + 1), i + 2);
    busBox_.onChange = [this] { bus_.store(busBox_.getSelectedId() <= 1 ? -1 : busBox_.getSelectedId() - 2, std::memory_order_relaxed); };
    addAndMakeVisible(busBox_);

    dcaBox_.addItem("NO DCA", 1);
    for (int i = 0; i < kDcas; ++i) dcaBox_.addItem("DCA " + juce::String(i + 1), i + 2);
    dcaBox_.onChange = [this] { dca_.store(dcaBox_.getSelectedId() <= 1 ? -1 : dcaBox_.getSelectedId() - 2, std::memory_order_relaxed); };
    addAndMakeVisible(dcaBox_);

    meterBar_.setPercentageDisplay(false);
    meterBar_.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(good));
    meterBar_.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff090c10));
    addAndMakeVisible(meterBar_);
    syncFromModel();
}

void MainComponent::MixerStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(juce::Colour(0xff303946));
    g.drawRoundedRectangle(r, 8.0f, 1.0f);
}

void MainComponent::MixerStrip::resized()
{
    auto r = getLocalBounds().reduced(8);
    title_.setBounds(r.removeFromTop(26));
    meterBar_.setBounds(r.removeFromRight(10).reduced(0, 14));
    r.removeFromRight(4);
    dcaBox_.setBounds(r.removeFromBottom(28));
    r.removeFromBottom(3);
    busBox_.setBounds(r.removeFromBottom(28));
    r.removeFromBottom(3);
    muteButton_.setBounds(r.removeFromBottom(28));
    panSlider_.setBounds(r.removeFromBottom(82));
    fader_.setBounds(r.reduced(2, 4));
}

void MainComponent::MixerStrip::timerTick()
{
    const auto peak = meter_.exchange(0.0f, std::memory_order_relaxed);
    const auto target = juce::jlimit(0.0, 1.0, static_cast<double>(peak));
    meterValue_ = std::max(target, meterValue_ * 0.86);
}

void MainComponent::MixerStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-6f, gain_.load(std::memory_order_relaxed));
    fader_.setValue(juce::Decibels::gainToDecibels(gain, -60.0f), juce::dontSendNotification);
    panSlider_.setValue(pan_.load(std::memory_order_relaxed), juce::dontSendNotification);
    muteButton_.setToggleState(muted_.load(std::memory_order_relaxed), juce::dontSendNotification);
    const int bus = bus_.load(std::memory_order_relaxed);
    const int dca = dca_.load(std::memory_order_relaxed);
    busBox_.setSelectedId(bus >= 0 && bus < kBuses ? bus + 2 : 1, juce::dontSendNotification);
    dcaBox_.setSelectedId(dca >= 0 && dca < kDcas ? dca + 2 : 1, juce::dontSendNotification);
}

MainComponent::GroupStrip::GroupStrip(const juce::String& title, std::atomic<float>& gain, std::atomic<bool>& muted)
    : gain_(gain), muted_(muted)
{
    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    title_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(title_);
    fader_.setSliderStyle(juce::Slider::LinearVertical);
    fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 22);
    fader_.setRange(-60.0, 12.0, 0.1);
    fader_.setDoubleClickReturnValue(true, 0.0);
    fader_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    fader_.onValueChange = [this] { gain_.store(dbToGain(fader_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(fader_);
    muteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    muteButton_.onClick = [this] { muted_.store(muteButton_.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(muteButton_);
    syncFromModel();
}

void MainComponent::GroupStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(juce::Colour(0xff303946));
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
}

void MainComponent::GroupStrip::resized()
{
    auto r = getLocalBounds().reduced(7);
    title_.setBounds(r.removeFromTop(26));
    muteButton_.setBounds(r.removeFromBottom(28));
    fader_.setBounds(r.reduced(2, 4));
}

void MainComponent::GroupStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-6f, gain_.load(std::memory_order_relaxed));
    fader_.setValue(juce::Decibels::gainToDecibels(gain, -60.0f), juce::dontSendNotification);
    muteButton_.setToggleState(muted_.load(std::memory_order_relaxed), juce::dontSendNotification);
}

MainComponent::IemSendStrip::IemSendStrip(int sourceIndex, const juce::String& title,
                                          std::atomic<float>& gain, std::atomic<float>& pan)
    : sourceIndex_(sourceIndex), gain_(gain), panValue_(pan)
{
    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    title_.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    addAndMakeVisible(title_);

    level_.setSliderStyle(juce::Slider::LinearVertical);
    level_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 22);
    level_.setRange(-80.0, 6.0, 0.1);
    level_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    level_.onValueChange = [this] { gain_.store(dbToGain(level_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(level_);

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 20);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDoubleClickReturnValue(true, 0.0);
    panSlider_.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accent));
    panSlider_.onValueChange = [this] { panValue_.store(static_cast<float>(panSlider_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(panSlider_);
    syncFromModel();
}

void MainComponent::IemSendStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(juce::Colour(0xff303946));
    g.drawRoundedRectangle(r, 8.0f, 1.0f);
}

void MainComponent::IemSendStrip::resized()
{
    auto r = getLocalBounds().reduced(8);
    title_.setBounds(r.removeFromTop(26));
    panSlider_.setBounds(r.removeFromBottom(86));
    level_.setBounds(r.reduced(2, 4));
}

void MainComponent::IemSendStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-8f, gain_.load(std::memory_order_relaxed));
    level_.setValue(juce::Decibels::gainToDecibels(gain, -80.0f), juce::dontSendNotification);
    panSlider_.setValue(panValue_.load(std::memory_order_relaxed), juce::dontSendNotification);
}

MainComponent::MainComponent()
{
    setOpaque(true);
    for (int i = 0; i < kMaxChannels; ++i)
    {
        channelGain_[i].store(dbToGain(-6.0));
        channelPan_[i].store(0.0f);
        channelMute_[i].store(false);
        channelMeter_[i].store(0.0f);
        channelBus_[i].store(-1);
        channelDca_[i].store(-1);
    }
    for (int i = 0; i < kBuses; ++i)
    {
        busGain_[i].store(1.0f);
        busMute_[i].store(false);
    }
    for (int i = 0; i < kDcas; ++i)
    {
        dcaGain_[i].store(1.0f);
        dcaMute_[i].store(false);
    }
    for (int m = 0; m < kIemMixes; ++m)
    {
        iemMaster_[m].store(1.0f);
        iemMute_[m].store(false);
        iemOutLeft_[m].store(-1);
        iemOutRight_[m].store(-1);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            iemSendGain_[m][ch].store(0.0f);
            iemSendPan_[m][ch].store(0.0f);
        }
    }

    brandLabel_.setText("J3 WORSHIP", juce::dontSendNotification);
    brandLabel_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    brandLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    addAndMakeVisible(brandLabel_);

    versionLabel_.setText("1.0.0", juce::dontSendNotification);
    versionLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    versionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff6f7b8a));
    addAndMakeVisible(versionLabel_);

    statusLabel_.setText("Audio: inicializando...", juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    addAndMakeVisible(statusLabel_);

    audioSettingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    audioSettingsButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(text));
    audioSettingsButton_.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible(audioSettingsButton_);

    liveMonitorButton_.setTooltip("Habilita el monitoreo de entradas hacia el PA. Arranca apagado por seguridad.");
    liveMonitorButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    liveMonitorButton_.onClick = [this]
    {
        if (liveMonitorButton_.getToggleState())
        {
            auto* device = deviceManager_.getCurrentAudioDevice();
            if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
            {
                liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
                showAudioError("No hay entradas de audio activas. Configurá primero AUDIO / MIDI.");
                return;
            }
            if (!routeIsSafe(paLeft_.load(), paRight_.load(), clickOutput_.load()))
            {
                liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
                showAudioError("Routing bloqueado: CLICK no puede compartir una salida asignada al PA.");
                return;
            }
        }
        liveMonitorEnabled_.store(liveMonitorButton_.getToggleState(), std::memory_order_release);
        updateDiagnostics();
    };
    addAndMakeVisible(liveMonitorButton_);

    nowLabel_.setText("NOW: STOPPED", juce::dontSendNotification);
    nowLabel_.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    nowLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    nowLabel_.setJustificationType(juce::Justification::centred);
    nextLabel_.setText("NEXT: —", juce::dontSendNotification);
    nextLabel_.setFont(juce::FontOptions(20.0f));
    nextLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffbdc6d3));
    nextLabel_.setJustificationType(juce::Justification::centred);
    liveHint_.setText("LIVE WORSHIP · cambios cuantizados · FREE / PAD mantiene ambiente · monitoreo PA arranca apagado", juce::dontSendNotification);
    liveHint_.setJustificationType(juce::Justification::centred);
    liveHint_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    livePage_.addAndMakeVisible(nowLabel_);
    livePage_.addAndMakeVisible(nextLabel_);
    livePage_.addAndMakeVisible(liveHint_);

    const std::array<juce::String, 8> names { "INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "BRIDGE", "INSTRUMENTAL", "FREE / PAD", "ENDING" };
    const std::array<j3::SectionKind, 8> kinds { j3::SectionKind::Intro, j3::SectionKind::Verse, j3::SectionKind::PreChorus,
        j3::SectionKind::Chorus, j3::SectionKind::Bridge, j3::SectionKind::Instrumental, j3::SectionKind::FreePad, j3::SectionKind::Ending };
    for (size_t i = 0; i < names.size(); ++i)
    {
        auto b = std::make_unique<juce::TextButton>(names[i]);
        b->setColour(juce::TextButton::buttonColourId, i == 6 ? juce::Colour(0xff593d86) : juce::Colour(panel2));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(accent));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(text));
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        b->onClick = [this, name = names[i], kind = kinds[i]] { setLiveSection(name, kind, kind == j3::SectionKind::FreePad ? 1 : 4); };
        livePage_.addAndMakeVisible(*b);
        liveButtons_[i] = std::move(b);
    }

    mixerPrevButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    mixerNextButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    mixerPrevButton_.onClick = [this] { setMixerBank(mixerBankStart_ - kVisibleChannels); };
    mixerNextButton_.onClick = [this] { setMixerBank(mixerBankStart_ + kVisibleChannels); };
    mixerBankLabel_.setJustificationType(juce::Justification::centred);
    mixerBankLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    mixerBankLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    mixerPage_.addAndMakeVisible(mixerPrevButton_);
    mixerPage_.addAndMakeVisible(mixerNextButton_);
    mixerPage_.addAndMakeVisible(mixerBankLabel_);
    rebuildMixerBank();

    groupsTitle_.setText("8 SUBGROUPS + 8 DCA", juce::dontSendNotification);
    groupsTitle_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    groupsTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    groupsPage_.addAndMakeVisible(groupsTitle_);
    for (int i = 0; i < kBuses; ++i)
    {
        busStrips_[i] = std::make_unique<GroupStrip>("BUS " + juce::String(i + 1), busGain_[i], busMute_[i]);
        groupsPage_.addAndMakeVisible(*busStrips_[i]);
    }
    for (int i = 0; i < kDcas; ++i)
    {
        dcaStrips_[i] = std::make_unique<GroupStrip>("DCA " + juce::String(i + 1), dcaGain_[i], dcaMute_[i]);
        groupsPage_.addAndMakeVisible(*dcaStrips_[i]);
    }

    iemTitle_.setText("16 STEREO IEM MIXES", juce::dontSendNotification);
    iemTitle_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    iemTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    iemPage_.addAndMakeVisible(iemTitle_);
    for (int i = 0; i < kIemMixes; ++i)
        iemMixBox_.addItem("IEM " + juce::String(i + 1), i + 1);
    iemMixBox_.setSelectedId(1, juce::dontSendNotification);
    iemMixBox_.onChange = [this]
    {
        selectedIemMix_ = juce::jlimit(0, kIemMixes - 1, iemMixBox_.getSelectedId() - 1);
        rebuildIemBank();
        refreshIemUi();
    };
    iemPage_.addAndMakeVisible(iemMixBox_);

    iemRouteLabel_.setText("PHYSICAL OUTPUT PAIR", juce::dontSendNotification);
    iemRouteLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    iemPage_.addAndMakeVisible(iemRouteLabel_);
    iemOutLeftBox_.onChange = [this] { applyIemRoutingFromControls(); };
    iemOutRightBox_.onChange = [this] { applyIemRoutingFromControls(); };
    iemPage_.addAndMakeVisible(iemOutLeftBox_);
    iemPage_.addAndMakeVisible(iemOutRightBox_);

    iemMasterSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    iemMasterSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 24);
    iemMasterSlider_.setRange(-60.0, 12.0, 0.1);
    iemMasterSlider_.setTextValueSuffix(" dB");
    iemMasterSlider_.onValueChange = [this]
    {
        iemMaster_[selectedIemMix_].store(dbToGain(iemMasterSlider_.getValue()), std::memory_order_relaxed);
    };
    iemPage_.addAndMakeVisible(iemMasterSlider_);
    iemMuteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    iemMuteButton_.onClick = [this] { iemMute_[selectedIemMix_].store(iemMuteButton_.getToggleState(), std::memory_order_relaxed); };
    iemPage_.addAndMakeVisible(iemMuteButton_);

    iemPrevButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    iemNextButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    iemPrevButton_.onClick = [this]
    {
        iemBankStart_ = juce::jlimit(0, kMaxChannels - kVisibleChannels, iemBankStart_ - kVisibleChannels);
        rebuildIemBank();
    };
    iemNextButton_.onClick = [this]
    {
        iemBankStart_ = juce::jlimit(0, kMaxChannels - kVisibleChannels, iemBankStart_ + kVisibleChannels);
        rebuildIemBank();
    };
    iemBankLabel_.setJustificationType(juce::Justification::centred);
    iemBankLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    iemPage_.addAndMakeVisible(iemPrevButton_);
    iemPage_.addAndMakeVisible(iemNextButton_);
    iemPage_.addAndMakeVisible(iemBankLabel_);
    rebuildIemBank();

    clickTitle_.setText("J3 CLICK", juce::dontSendNotification);
    clickTitle_.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    clickTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    clickPage_.addAndMakeVisible(clickTitle_);

    clickEnabledButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    clickEnabledButton_.onClick = [this]
    {
        clickAudible_.store(clickEnabledButton_.getToggleState(), std::memory_order_release);
        if (clickAudible_.load())
        {
            transportRunning_.store(true, std::memory_order_release);
            clickGenerator_.setEnabled(true);
        }
        if (clickAudible_.load() && clickOutput_.load() < 0)
        {
            clickEnabledButton_.setToggleState(false, juce::dontSendNotification);
            clickAudible_.store(false, std::memory_order_release);
            if (!liveStarted_)
            {
                transportRunning_.store(false, std::memory_order_release);
                clickGenerator_.setEnabled(false);
            }
            showAudioError("Elegí una salida CLICK / GUIDE distinta del PA antes de activar el click.");
        }
        updateClickUi();
        saveAppState();
    };
    clickPage_.addAndMakeVisible(clickEnabledButton_);

    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 28);
    bpmSlider_.setRange(40.0, 240.0, 0.1);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    bpmSlider_.onValueChange = [this]
    {
        clickGenerator_.setTempo(bpmSlider_.getValue());
        if (liveStarted_) liveEngine_.setTempo(bpmSlider_.getValue(), clickGenerator_.numerator());
    };
    clickPage_.addAndMakeVisible(bpmSlider_);

    tapTempoButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    tapTempoButton_.onClick = [this] { handleTapTempo(); };
    clickPage_.addAndMakeVisible(tapTempoButton_);

    timeSignatureBox_.addItem("4/4", 1);
    timeSignatureBox_.addItem("3/4", 2);
    timeSignatureBox_.addItem("6/8", 3);
    timeSignatureBox_.addItem("2/4", 4);
    timeSignatureBox_.setSelectedId(1, juce::dontSendNotification);
    timeSignatureBox_.onChange = [this]
    {
        const int id = timeSignatureBox_.getSelectedId();
        const int n = id == 2 ? 3 : (id == 3 ? 6 : (id == 4 ? 2 : 4));
        const int d = id == 3 ? 8 : 4;
        clickGenerator_.setTimeSignature(n, d);
        if (liveStarted_) liveEngine_.setTempo(bpmSlider_.getValue(), n);
        saveAppState();
    };
    clickPage_.addAndMakeVisible(timeSignatureBox_);

    subdivisionBox_.addItem("Quarter", 1);
    subdivisionBox_.addItem("Eighth", 2);
    subdivisionBox_.addItem("Sixteenth", 3);
    subdivisionBox_.setSelectedId(1, juce::dontSendNotification);
    subdivisionBox_.onChange = [this]
    {
        const int id = subdivisionBox_.getSelectedId();
        clickGenerator_.setSubdivision(id == 3 ? 4 : (id == 2 ? 2 : 1));
        saveAppState();
    };
    clickPage_.addAndMakeVisible(subdivisionBox_);

    accentButton_.setToggleState(true, juce::dontSendNotification);
    accentButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    accentButton_.onClick = [this]
    {
        clickGenerator_.setAccentEnabled(accentButton_.getToggleState());
        saveAppState();
    };
    clickPage_.addAndMakeVisible(accentButton_);

    clickVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    clickVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 26);
    clickVolumeSlider_.setRange(0.0, 1.0, 0.01);
    clickVolumeSlider_.setValue(0.35, juce::dontSendNotification);
    clickVolumeSlider_.onValueChange = [this]
    {
        clickGenerator_.setLevel(static_cast<float>(clickVolumeSlider_.getValue()));
    };
    clickPage_.addAndMakeVisible(clickVolumeSlider_);

    clickRouteLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    clickRouteLabel_.setFont(juce::FontOptions(16.0f));
    clickRouteLabel_.setJustificationType(juce::Justification::topLeft);
    clickPage_.addAndMakeVisible(clickRouteLabel_);

    recordingTitle_.setText("RECORD SERVICE", juce::dontSendNotification);
    recordingTitle_.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    recordingTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    recordingPage_.addAndMakeVisible(recordingTitle_);
    recordButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b2430));
    recordButton_.onClick = [this] { startStopRecording(); };
    recordingPage_.addAndMakeVisible(recordButton_);
    recordingStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    recordingStatusLabel_.setFont(juce::FontOptions(17.0f));
    recordingStatusLabel_.setJustificationType(juce::Justification::topLeft);
    recordingPage_.addAndMakeVisible(recordingStatusLabel_);
    openRecordingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    openRecordingsButton_.onClick = [this] { recordingsRoot().revealToUser(); };
    recordingPage_.addAndMakeVisible(openRecordingsButton_);
    updateRecordingUi();

    setupTitle_.setText("AUDIO & ROUTING", juce::dontSendNotification);
    setupTitle_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    setupTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    setupPage_.addAndMakeVisible(setupTitle_);

    setupDeviceLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffbdc6d3));
    setupDeviceLabel_.setFont(juce::FontOptions(16.0f));
    setupDeviceLabel_.setJustificationType(juce::Justification::topLeft);
    setupPage_.addAndMakeVisible(setupDeviceLabel_);

    routingTitle_.setText("SAFE OUTPUT ROUTING", juce::dontSendNotification);
    routingTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    routingTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    setupPage_.addAndMakeVisible(routingTitle_);

    paLeftLabel_.setText("PA LEFT / MONO", juce::dontSendNotification);
    paRightLabel_.setText("PA RIGHT", juce::dontSendNotification);
    clickLabel_.setText("CLICK / GUIDE", juce::dontSendNotification);
    for (auto* label : { &paLeftLabel_, &paRightLabel_, &clickLabel_ })
    {
        label->setColour(juce::Label::textColourId, juce::Colour(mutedText));
        setupPage_.addAndMakeVisible(*label);
    }
    setupPage_.addAndMakeVisible(paLeftBox_);
    setupPage_.addAndMakeVisible(paRightBox_);
    setupPage_.addAndMakeVisible(clickBox_);
    paLeftBox_.onChange = [this] { applyRoutingFromControls(); };
    paRightBox_.onChange = [this] { applyRoutingFromControls(); };
    clickBox_.onChange = [this] { applyRoutingFromControls(); };

    rescanButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    testOutputButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    driverPanelButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    rescanButton_.onClick = [this]
    {
        scanAvailableDevices();
        updateDiagnostics();
        refreshRoutingControls();
    };
    testOutputButton_.onClick = [this]
    {
        if (deviceManager_.getCurrentAudioDevice() == nullptr)
            showAudioError("No hay un dispositivo de audio activo.");
        else
            deviceManager_.playTestSound();
    };
    driverPanelButton_.onClick = [this] { openDriverControlPanel(); };
    setupPage_.addAndMakeVisible(rescanButton_);
    setupPage_.addAndMakeVisible(testOutputButton_);
    setupPage_.addAndMakeVisible(driverPanelButton_);

    diagnosticsLabel_.setFont(juce::FontOptions(16.5f));
    diagnosticsLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    diagnosticsLabel_.setJustificationType(juce::Justification::topLeft);
    diagnosticsPage_.addAndMakeVisible(diagnosticsLabel_);
    runCheckButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    runCheckButton_.onClick = [this]
    {
        scanAvailableDevices();
        updateDiagnostics();
    };
    diagnosticsPage_.addAndMakeVisible(runCheckButton_);

    tabs_.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(background));
    tabs_.setTabBarDepth(46);
    tabs_.addTab("LIVE", juce::Colour(panel), &livePage_, false);
    tabs_.addTab("MIXER", juce::Colour(panel), &mixerPage_, false);
    tabs_.addTab("GROUPS", juce::Colour(panel), &groupsPage_, false);
    tabs_.addTab("IEM", juce::Colour(panel), &iemPage_, false);
    tabs_.addTab("CLICK", juce::Colour(panel), &clickPage_, false);
    tabs_.addTab("RECORD", juce::Colour(panel), &recordingPage_, false);
    tabs_.addTab("AUDIO / ROUTING", juce::Colour(panel), &setupPage_, false);
    tabs_.addTab("SYSTEM CHECK", juce::Colour(panel), &diagnosticsPage_, false);
    addAndMakeVisible(tabs_);

    recoveredAfterUncleanExit_ = getRuntimeLockFile().existsAsFile();
    getRuntimeLockFile().getParentDirectory().createDirectory();
    getRuntimeLockFile().replaceWithText("running");
    configureAudio();
    loadAppState();
    rebuildMixerBank();
    rebuildIemBank();
    refreshIemUi();
    updateClickUi();
    updateRecordingUi();
    startTimerHz(30);
    setSize(1440, 900);
}

MainComponent::~MainComponent()
{
    shuttingDown_.store(true, std::memory_order_release);
    stopTimer();
    liveMonitorEnabled_.store(false, std::memory_order_release);
    recordingEnabled_.store(false, std::memory_order_release);
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(this);
    std::string recordError;
    recorder_.stop(recordError);
    saveAppState();
    saveAudioState();
    deviceManager_.closeAudioDevice();
    getRuntimeLockFile().deleteFile();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(background));
    auto top = getLocalBounds().removeFromTop(66);
    g.setColour(juce::Colour(topBar));
    g.fillRect(top);
    g.setColour(juce::Colour(0xff2a3340));
    g.drawHorizontalLine(65, 0.0f, static_cast<float>(getWidth()));
}

juce::String MainComponent::inputChannelName(int channel) const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        if (channel >= 0 && channel < names.size() && names[channel].isNotEmpty())
            return names[channel];
    }
    return "IN " + juce::String(channel + 1);
}

void MainComponent::setMixerBank(int firstChannel)
{
    const int maxStart = std::max(0, kMaxChannels - kVisibleChannels);
    const int snapped = (juce::jlimit(0, maxStart, firstChannel) / kVisibleChannels) * kVisibleChannels;
    if (snapped == mixerBankStart_ && strips_[0] != nullptr)
        return;
    mixerBankStart_ = snapped;
    rebuildMixerBank();
    resized();
}

void MainComponent::rebuildMixerBank()
{
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        strips_[i].reset();
        const int channel = mixerBankStart_ + i;
        strips_[i] = std::make_unique<MixerStrip>(
            channel, inputChannelName(channel),
            channelGain_[channel], channelPan_[channel], channelMute_[channel], channelMeter_[channel],
            channelBus_[channel], channelDca_[channel]);
        mixerPage_.addAndMakeVisible(*strips_[i]);
    }
    mixerBankLabel_.setText("INPUTS " + juce::String(mixerBankStart_ + 1) + "–"
        + juce::String(std::min(kMaxChannels, mixerBankStart_ + kVisibleChannels))
        + " / " + juce::String(kMaxChannels), juce::dontSendNotification);
    mixerPrevButton_.setEnabled(mixerBankStart_ > 0);
    mixerNextButton_.setEnabled(mixerBankStart_ + kVisibleChannels < kMaxChannels);
}

void MainComponent::rebuildIemBank()
{
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        iemStrips_[i].reset();
        const int channel = iemBankStart_ + i;
        iemStrips_[i] = std::make_unique<IemSendStrip>(
            channel, inputChannelName(channel),
            iemSendGain_[selectedIemMix_][channel], iemSendPan_[selectedIemMix_][channel]);
        iemPage_.addAndMakeVisible(*iemStrips_[i]);
    }
    iemBankLabel_.setText("SOURCES " + juce::String(iemBankStart_ + 1) + "–"
        + juce::String(std::min(kMaxChannels, iemBankStart_ + kVisibleChannels))
        + " / " + juce::String(kMaxChannels), juce::dontSendNotification);
    iemPrevButton_.setEnabled(iemBankStart_ > 0);
    iemNextButton_.setEnabled(iemBankStart_ + kVisibleChannels < kMaxChannels);
}

void MainComponent::refreshIemUi()
{
    const int mix = juce::jlimit(0, kIemMixes - 1, selectedIemMix_);
    const auto master = std::max(1.0e-6f, iemMaster_[mix].load(std::memory_order_relaxed));
    iemMasterSlider_.setValue(juce::Decibels::gainToDecibels(master, -60.0f), juce::dontSendNotification);
    iemMuteButton_.setToggleState(iemMute_[mix].load(std::memory_order_relaxed), juce::dontSendNotification);

    const int left = iemOutLeft_[mix].load(std::memory_order_relaxed);
    const int right = iemOutRight_[mix].load(std::memory_order_relaxed);
    iemOutLeftBox_.setSelectedId(left >= 0 ? left + 2 : 1, juce::dontSendNotification);
    iemOutRightBox_.setSelectedId(right >= 0 ? right + 2 : 1, juce::dontSendNotification);

    for (auto& strip : iemStrips_)
        if (strip) strip->syncFromModel();
}

bool MainComponent::iemRouteIsSafe(int mix, int left, int right) const noexcept
{
    if (left < 0 || right < 0)
        return left < 0 && right < 0;
    if (left == right)
        return false;
    const int paL = paLeft_.load(std::memory_order_relaxed);
    const int paR = paRight_.load(std::memory_order_relaxed);
    const int click = clickOutput_.load(std::memory_order_relaxed);
    if (left == paL || left == paR || right == paL || right == paR || left == click || right == click)
        return false;
    for (int i = 0; i < kIemMixes; ++i)
    {
        if (i == mix) continue;
        const int l = iemOutLeft_[i].load(std::memory_order_relaxed);
        const int r = iemOutRight_[i].load(std::memory_order_relaxed);
        if (l < 0 || r < 0) continue;
        if (left == l || left == r || right == l || right == r)
            return false;
    }
    return true;
}

bool MainComponent::anyIemRouted() const noexcept
{
    for (int i = 0; i < kIemMixes; ++i)
        if (iemOutLeft_[i].load(std::memory_order_relaxed) >= 0
            && iemOutRight_[i].load(std::memory_order_relaxed) >= 0
            && !iemMute_[i].load(std::memory_order_relaxed))
            return true;
    return false;
}

void MainComponent::applyIemRoutingFromControls()
{
    const int mix = selectedIemMix_;
    const int left = iemOutLeftBox_.getSelectedId() <= 1 ? -1 : iemOutLeftBox_.getSelectedId() - 2;
    const int right = iemOutRightBox_.getSelectedId() <= 1 ? -1 : iemOutRightBox_.getSelectedId() - 2;
    const bool bothOff = left < 0 && right < 0;
    if (!bothOff && !iemRouteIsSafe(mix, left, right))
    {
        iemOutLeft_[mix].store(-1, std::memory_order_relaxed);
        iemOutRight_[mix].store(-1, std::memory_order_relaxed);
        iemOutLeftBox_.setSelectedId(1, juce::dontSendNotification);
        iemOutRightBox_.setSelectedId(1, juce::dontSendNotification);
        showAudioError("J3 SAFE ROUTING bloqueó ese IEM: usá dos salidas libres, distintas del PA, CLICK y otros IEM.");
        return;
    }
    iemOutLeft_[mix].store(left, std::memory_order_relaxed);
    iemOutRight_[mix].store(right, std::memory_order_relaxed);
    saveAppState();
    updateDiagnostics();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(66).reduced(16, 8);
    brandLabel_.setBounds(top.removeFromLeft(190));
    versionLabel_.setBounds(top.removeFromLeft(100).reduced(0, 11));
    audioSettingsButton_.setBounds(top.removeFromRight(150));
    top.removeFromRight(10);
    liveMonitorButton_.setBounds(top.removeFromRight(132));
    top.removeFromRight(10);
    statusLabel_.setBounds(top.removeFromRight(510));
    tabs_.setBounds(area.reduced(10));

    auto liveArea = livePage_.getLocalBounds().reduced(24);
    nowLabel_.setBounds(liveArea.removeFromTop(58));
    nextLabel_.setBounds(liveArea.removeFromTop(38));
    liveHint_.setBounds(liveArea.removeFromTop(36));
    liveArea.removeFromTop(18);
    const int gap = 12;
    const int columns = 4;
    const int rows = 2;
    const int buttonW = (liveArea.getWidth() - gap * (columns - 1)) / columns;
    const int buttonH = (liveArea.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < 8; ++i)
    {
        const int c = i % columns;
        const int r = i / columns;
        liveButtons_[i]->setBounds(liveArea.getX() + c * (buttonW + gap),
                                  liveArea.getY() + r * (buttonH + gap), buttonW, buttonH);
    }

    auto mixerArea = mixerPage_.getLocalBounds().reduced(12);
    auto mixerNav = mixerArea.removeFromTop(40);
    mixerPrevButton_.setBounds(mixerNav.removeFromLeft(110));
    mixerNextButton_.setBounds(mixerNav.removeFromRight(110));
    mixerBankLabel_.setBounds(mixerNav);
    mixerArea.removeFromTop(6);
    const int stripW = std::max(1, mixerArea.getWidth() / kVisibleChannels);
    for (int i = 0; i < kVisibleChannels; ++i)
        if (strips_[i]) strips_[i]->setBounds(mixerArea.removeFromLeft(stripW).reduced(3));

    auto groupsArea = groupsPage_.getLocalBounds().reduced(18);
    groupsTitle_.setBounds(groupsArea.removeFromTop(38));
    groupsArea.removeFromTop(8);
    auto busRow = groupsArea.removeFromTop(std::max(220, groupsArea.getHeight() / 2 - 5));
    const int groupW = std::max(1, busRow.getWidth() / kBuses);
    for (int i = 0; i < kBuses; ++i)
        if (busStrips_[i]) busStrips_[i]->setBounds(busRow.removeFromLeft(groupW).reduced(3));
    groupsArea.removeFromTop(10);
    const int dcaW = std::max(1, groupsArea.getWidth() / kDcas);
    for (int i = 0; i < kDcas; ++i)
        if (dcaStrips_[i]) dcaStrips_[i]->setBounds(groupsArea.removeFromLeft(dcaW).reduced(3));

    auto iemArea = iemPage_.getLocalBounds().reduced(18);
    auto iemTop = iemArea.removeFromTop(40);
    iemTitle_.setBounds(iemTop.removeFromLeft(300));
    iemMixBox_.setBounds(iemTop.removeFromLeft(150).reduced(4, 3));
    iemRouteLabel_.setBounds(iemTop.removeFromLeft(160));
    iemOutLeftBox_.setBounds(iemTop.removeFromLeft(170).reduced(4, 3));
    iemOutRightBox_.setBounds(iemTop.removeFromLeft(170).reduced(4, 3));
    iemMuteButton_.setBounds(iemTop.removeFromRight(120));
    iemArea.removeFromTop(8);
    auto iemMasterRow = iemArea.removeFromTop(40);
    iemMasterSlider_.setBounds(iemMasterRow.removeFromLeft(520));
    auto iemNav = iemMasterRow;
    iemPrevButton_.setBounds(iemNav.removeFromLeft(110));
    iemNextButton_.setBounds(iemNav.removeFromRight(110));
    iemBankLabel_.setBounds(iemNav);
    iemArea.removeFromTop(8);
    const int iemW = std::max(1, iemArea.getWidth() / kVisibleChannels);
    for (int i = 0; i < kVisibleChannels; ++i)
        if (iemStrips_[i]) iemStrips_[i]->setBounds(iemArea.removeFromLeft(iemW).reduced(3));

    auto clickArea = clickPage_.getLocalBounds().reduced(36);
    clickTitle_.setBounds(clickArea.removeFromTop(48));
    clickArea.removeFromTop(18);
    clickEnabledButton_.setBounds(clickArea.removeFromTop(40).removeFromLeft(180));
    clickArea.removeFromTop(18);
    auto tempoRow = clickArea.removeFromTop(58);
    bpmSlider_.setBounds(tempoRow.removeFromLeft(std::min(560, tempoRow.getWidth() - 180)));
    tempoRow.removeFromLeft(16);
    tapTempoButton_.setBounds(tempoRow.removeFromLeft(160));
    clickArea.removeFromTop(18);
    auto optionRow = clickArea.removeFromTop(48);
    timeSignatureBox_.setBounds(optionRow.removeFromLeft(180));
    optionRow.removeFromLeft(16);
    subdivisionBox_.setBounds(optionRow.removeFromLeft(180));
    optionRow.removeFromLeft(16);
    accentButton_.setBounds(optionRow.removeFromLeft(140));
    clickArea.removeFromTop(18);
    clickVolumeSlider_.setBounds(clickArea.removeFromTop(48).removeFromLeft(520));
    clickArea.removeFromTop(20);
    clickRouteLabel_.setBounds(clickArea.removeFromTop(120));

    auto recordArea = recordingPage_.getLocalBounds().reduced(36);
    recordingTitle_.setBounds(recordArea.removeFromTop(48));
    recordArea.removeFromTop(20);
    recordButton_.setBounds(recordArea.removeFromTop(58).removeFromLeft(220));
    recordArea.removeFromTop(18);
    openRecordingsButton_.setBounds(recordArea.removeFromTop(44).removeFromLeft(260));
    recordArea.removeFromTop(18);
    recordingStatusLabel_.setBounds(recordArea);

    auto setup = setupPage_.getLocalBounds().reduced(32);
    setupTitle_.setBounds(setup.removeFromTop(42));
    setupDeviceLabel_.setBounds(setup.removeFromTop(160));
    setup.removeFromTop(14);
    routingTitle_.setBounds(setup.removeFromTop(32));
    auto routeRow = setup.removeFromTop(78);
    const int routeGap = 16;
    const int routeWidth = (routeRow.getWidth() - routeGap * 2) / 3;
    auto left = routeRow.removeFromLeft(routeWidth);
    routeRow.removeFromLeft(routeGap);
    auto right = routeRow.removeFromLeft(routeWidth);
    routeRow.removeFromLeft(routeGap);
    auto click = routeRow;
    paLeftLabel_.setBounds(left.removeFromTop(24));
    paLeftBox_.setBounds(left.removeFromTop(36));
    paRightLabel_.setBounds(right.removeFromTop(24));
    paRightBox_.setBounds(right.removeFromTop(36));
    clickLabel_.setBounds(click.removeFromTop(24));
    clickBox_.setBounds(click.removeFromTop(36));
    setup.removeFromTop(20);
    auto buttons = setup.removeFromTop(44);
    rescanButton_.setBounds(buttons.removeFromLeft(180));
    buttons.removeFromLeft(12);
    testOutputButton_.setBounds(buttons.removeFromLeft(170));
    buttons.removeFromLeft(12);
    driverPanelButton_.setBounds(buttons.removeFromLeft(220));

    auto d = diagnosticsPage_.getLocalBounds().reduced(28);
    runCheckButton_.setBounds(d.removeFromTop(44).removeFromLeft(210));
    d.removeFromTop(18);
    diagnosticsLabel_.setBounds(d);
}

juce::File MainComponent::getAudioStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("audio-device.xml");
}

juce::File MainComponent::getAppStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("app-state.xml");
}

juce::File MainComponent::getRuntimeLockFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("runtime.lock");
}

juce::File MainComponent::recordingsRoot() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("Recordings");
}

void MainComponent::loadAppState()
{
    const auto file = getAppStateFile();
    if (!file.existsAsFile())
        return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("J3WorshipState"))
        return;

    paLeft_.store(xml->getIntAttribute("paLeft", 0));
    paRight_.store(xml->getIntAttribute("paRight", 1));
    clickOutput_.store(xml->getIntAttribute("clickOutput", -1));
    bpmSlider_.setValue(xml->getDoubleAttribute("bpm", 120.0), juce::dontSendNotification);
    timeSignatureBox_.setSelectedId(xml->getIntAttribute("timeSignatureId", 1), juce::dontSendNotification);
    subdivisionBox_.setSelectedId(xml->getIntAttribute("subdivisionId", 1), juce::dontSendNotification);
    accentButton_.setToggleState(xml->getBoolAttribute("accent", true), juce::dontSendNotification);
    clickVolumeSlider_.setValue(xml->getDoubleAttribute("clickVolume", 0.35), juce::dontSendNotification);
    clickAudible_.store(false, std::memory_order_release);
    clickEnabledButton_.setToggleState(false, juce::dontSendNotification);

    clickGenerator_.setTempo(bpmSlider_.getValue());
    const int sig = timeSignatureBox_.getSelectedId();
    clickGenerator_.setTimeSignature(sig == 2 ? 3 : (sig == 3 ? 6 : (sig == 4 ? 2 : 4)), sig == 3 ? 8 : 4);
    const int sub = subdivisionBox_.getSelectedId();
    clickGenerator_.setSubdivision(sub == 3 ? 4 : (sub == 2 ? 2 : 1));
    clickGenerator_.setAccentEnabled(accentButton_.getToggleState());
    clickGenerator_.setLevel(static_cast<float>(clickVolumeSlider_.getValue()));

    int index = 0;
    forEachXmlChildElementWithTagName(*xml, ch, "Channel")
    {
        if (index >= kVisibleChannels) break;
        channelGain_[index].store(static_cast<float>(ch->getDoubleAttribute("gain", dbToGain(-6.0))), std::memory_order_relaxed);
        channelPan_[index].store(static_cast<float>(ch->getDoubleAttribute("pan", 0.0)), std::memory_order_relaxed);
        channelMute_[index].store(ch->getBoolAttribute("mute", false), std::memory_order_relaxed);
        ++index;
    }
    for (auto& strip : strips_)
        if (strip) strip->syncFromModel();
    refreshRoutingControls();
}

void MainComponent::saveAppState()
{
    juce::XmlElement xml("J3WorshipState");
    xml.setAttribute("version", "0.5.0");
    xml.setAttribute("paLeft", paLeft_.load());
    xml.setAttribute("paRight", paRight_.load());
    xml.setAttribute("clickOutput", clickOutput_.load());
    xml.setAttribute("bpm", bpmSlider_.getValue());
    xml.setAttribute("timeSignatureId", timeSignatureBox_.getSelectedId());
    xml.setAttribute("subdivisionId", subdivisionBox_.getSelectedId());
    xml.setAttribute("accent", accentButton_.getToggleState());
    xml.setAttribute("clickVolume", clickVolumeSlider_.getValue());
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        auto* ch = xml.createNewChildElement("Channel");
        ch->setAttribute("index", i);
        ch->setAttribute("gain", static_cast<double>(channelGain_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("pan", static_cast<double>(channelPan_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("mute", channelMute_[i].load(std::memory_order_relaxed));
    }
    const auto file = getAppStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(xml.toString(), false, false, "\n");
}

void MainComponent::startStopRecording()
{
    if (recordingEnabled_.load(std::memory_order_acquire))
    {
        recordingEnabled_.store(false, std::memory_order_release);
        std::string error;
        if (!recorder_.stop(error) && !error.empty())
            showAudioError("No se pudo finalizar la grabación: " + juce::String(error));
        recordChannelCount_.store(0, std::memory_order_release);
        updateRecordingUi();
        return;
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        showAudioError("No hay una interfaz de audio activa.");
        return;
    }
    const auto active = device->getActiveInputChannels();
    const auto names = device->getInputChannelNames();
    std::vector<std::string> channelNames;
    channelNames.reserve(j3::kRecordMaxChannels);
    for (int i = 0; i < active.getHighestBit() + 1 && channelNames.size() < j3::kRecordMaxChannels; ++i)
        if (active[i])
            channelNames.push_back((i < names.size() && names[i].isNotEmpty() ? names[i] : "Input " + juce::String(i + 1)).toStdString());
    if (channelNames.empty())
    {
        showAudioError("No hay entradas activas para grabar. Activá entradas desde AUDIO / MIDI.");
        return;
    }

    const auto now = juce::Time::getCurrentTime();
    const auto dir = recordingsRoot().getChildFile(now.formatted("%Y-%m-%d")).getChildFile(now.formatted("%H%M%S-Service"));
    std::string error;
    if (!recorder_.start(dir.getFullPathName().toStdString(), static_cast<std::uint32_t>(sampleRate_.load()), channelNames, error))
    {
        showAudioError("No se pudo iniciar RECORD SERVICE: " + juce::String(error));
        return;
    }
    recordChannelCount_.store(static_cast<int>(channelNames.size()), std::memory_order_release);
    recordingEnabled_.store(true, std::memory_order_release);
    updateRecordingUi();
}

void MainComponent::updateRecordingUi()
{
    const bool active = recordingEnabled_.load(std::memory_order_acquire);
    recordButton_.setButtonText(active ? "STOP RECORDING" : "RECORD SERVICE");
    recordButton_.setColour(juce::TextButton::buttonColourId, active ? juce::Colour(danger) : juce::Colour(0xff9b2430));
    juce::String status;
    status << (active ? "● RECORDING\n" : "READY\n");
    status << "Folder: " << recordingsRoot().getFullPathName() << "\n";
    status << "Individual WAV files: " << recordChannelCount_.load() << "\n";
    status << "Dropped recording blocks: " << recorder_.overflowCount() << "\n";
    status << "Audio is copied into a lock-free queue; disk I/O stays off the real-time callback.";
    recordingStatusLabel_.setText(status, juce::dontSendNotification);
}

void MainComponent::handleTapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTapMs_ > 0.0)
    {
        const double interval = now - lastTapMs_;
        if (interval >= 250.0 && interval <= 2000.0)
            bpmSlider_.setValue(60000.0 / interval, juce::sendNotificationSync);
    }
    lastTapMs_ = now;
}

void MainComponent::updateClickUi()
{
    const int out = clickOutput_.load(std::memory_order_relaxed);
    juce::String route = "CLICK routing: ";
    if (out < 0) route << "OFF — choose an output in AUDIO / ROUTING.";
    else route << "Output " << (out + 1) << ". J3 Safe Routing blocks this output if it is also used by PA.";
    route << "\nTempo: " << juce::String(bpmSlider_.getValue(), 1) << " BPM · "
          << clickGenerator_.numerator() << "/" << clickGenerator_.denominator();
    clickRouteLabel_.setText(route, juce::dontSendNotification);
}

void MainComponent::configureAudio()
{
    deviceManager_.addChangeListener(this);
    const auto stateFile = getAudioStateFile();
    std::unique_ptr<juce::XmlElement> savedState;
    if (stateFile.existsAsFile())
        savedState = juce::XmlDocument::parse(stateFile);

    auto error = deviceManager_.initialise(kMaxChannels, kMaxChannels, savedState.get(), true, {}, nullptr);
    if (error.isNotEmpty() && savedState != nullptr)
        error = deviceManager_.initialise(kMaxChannels, kMaxChannels, nullptr, true, {}, nullptr);

    if (error.isNotEmpty())
    {
        lastAudioError_ = error;
        statusLabel_.setText("Audio: requiere configuración", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
    }
    else
    {
        if (savedState == nullptr)
            preferAsioWhenAvailable(true);
        deviceManager_.addAudioCallback(this);
    }

    scanAvailableDevices();
    refreshRoutingControls();
    updateDiagnostics();
    saveAudioState();
}

void MainComponent::preferAsioWhenAvailable(bool onlyIfNoSavedState)
{
    juce::ignoreUnused(onlyIfNoSavedState);
#if JUCE_WINDOWS && JUCE_ASIO
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        if (type == nullptr || !type->getTypeName().containsIgnoreCase("ASIO"))
            continue;
        type->scanForDevices();
        const auto outputs = type->getDeviceNames(false);
        const auto inputs = type->getDeviceNames(true);
        if (!outputs.isEmpty() || !inputs.isEmpty())
        {
            deviceManager_.setCurrentAudioDeviceType(type->getTypeName(), true);
            return;
        }
    }
#endif
}

void MainComponent::scanAvailableDevices()
{
    juce::String inventory;
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;
        type->scanForDevices();
        const auto outputs = type->getDeviceNames(false);
        const auto inputs = type->getDeviceNames(true);
        inventory << type->getTypeName() << ": ";
        if (outputs.isEmpty() && inputs.isEmpty())
        {
            inventory << "no devices";
        }
        else
        {
            juce::StringArray unique;
            unique.addArray(outputs);
            unique.addArray(inputs);
            unique.removeDuplicates(false);
            inventory << unique.joinIntoString(", ");
        }
        inventory << "\n";
    }
    deviceInventory_ = inventory.trimEnd();
}

void MainComponent::saveAudioState()
{
    if (shuttingDown_.load(std::memory_order_acquire) && deviceManager_.getCurrentAudioDevice() == nullptr)
        return;

    if (auto state = deviceManager_.createStateXml())
    {
        const auto file = getAudioStateFile();
        const auto result = file.getParentDirectory().createDirectory();
        if (result.wasOk())
            file.replaceWithText(state->toString(), false, false, "\n");
    }
}

void MainComponent::openAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager_, 0, kMaxChannels, 0, kMaxChannels, true, true, true, false);
    selector->setSize(800, 680);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "J3 Worship — Audio / MIDI";
    options.dialogBackgroundColour = juce::Colour(panel);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::openDriverControlPanel()
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        if (device->hasControlPanel())
        {
            device->showControlPanel();
            return;
        }
    }
    showAudioError("El driver actual no expone un panel de control propio. Usá AUDIO / MIDI para cambiar sample rate, buffer o dispositivo.");
}

void MainComponent::showAudioError(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "J3 Worship", message);
}

juce::String MainComponent::buildDeviceInventoryText() const
{
    if (deviceInventory_.isEmpty())
        return "No se escanearon drivers todavía.";
    return deviceInventory_;
}

void MainComponent::refreshRoutingControls()
{
    paLeftBox_.clear(juce::dontSendNotification);
    paRightBox_.clear(juce::dontSendNotification);
    clickBox_.clear(juce::dontSendNotification);
    clickBox_.addItem("OFF / NOT ROUTED", 1);

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        setupDeviceLabel_.setText("No hay interfaz activa. Abrí AUDIO / MIDI para elegir un driver.", juce::dontSendNotification);
        clickBox_.setSelectedId(1, juce::dontSendNotification);
        return;
    }

    const auto names = device->getOutputChannelNames();
    const int outputCount = names.size();
    for (int i = 0; i < outputCount; ++i)
    {
        const auto label = juce::String(i + 1) + " · " + outputName(*device, i);
        paLeftBox_.addItem(label, i + 1);
        paRightBox_.addItem(label, i + 1);
        clickBox_.addItem(label, i + 2);
    }

    const int left = juce::jlimit(0, std::max(0, outputCount - 1), paLeft_.load());
    const int rightDefault = outputCount > 1 ? 1 : 0;
    const int right = juce::jlimit(0, std::max(0, outputCount - 1), paRight_.load() < outputCount ? paRight_.load() : rightDefault);
    paLeft_.store(left);
    paRight_.store(right);
    paLeftBox_.setSelectedId(outputCount > 0 ? left + 1 : 0, juce::dontSendNotification);
    paRightBox_.setSelectedId(outputCount > 0 ? right + 1 : 0, juce::dontSendNotification);

    const int click = clickOutput_.load();
    if (click >= 0 && click < outputCount && click != left && click != right)
        clickBox_.setSelectedId(click + 2, juce::dontSendNotification);
    else
    {
        clickOutput_.store(-1);
        clickBox_.setSelectedId(1, juce::dontSendNotification);
    }

    setupDeviceLabel_.setText("ACTIVE DEVICE\n" + device->getName() + "  ·  " + deviceManager_.getCurrentAudioDeviceType()
        + "\n" + juce::String(device->getActiveInputChannels().countNumberOfSetBits()) + " active inputs  ·  "
        + juce::String(device->getActiveOutputChannels().countNumberOfSetBits()) + " active outputs\n"
        + "PA and monitor routes remain muted until LIVE INPUTS is enabled.", juce::dontSendNotification);
}

void MainComponent::applyRoutingFromControls()
{
    const int newLeft = paLeftBox_.getSelectedId() > 0 ? paLeftBox_.getSelectedId() - 1 : 0;
    const int newRight = paRightBox_.getSelectedId() > 0 ? paRightBox_.getSelectedId() - 1 : newLeft;
    const int newClick = clickBox_.getSelectedId() <= 1 ? -1 : clickBox_.getSelectedId() - 2;

    if (!routeIsSafe(newLeft, newRight, newClick))
    {
        clickOutput_.store(-1);
        clickBox_.setSelectedId(1, juce::dontSendNotification);
        showAudioError("J3 SAFE ROUTING bloqueó esa selección: CLICK / GUIDE no puede ir a una salida usada por el PA.");
    }
    else
    {
        paLeft_.store(newLeft);
        paRight_.store(newRight);
        clickOutput_.store(newClick);
    }
    updateDiagnostics();
}

void MainComponent::updateDiagnostics()
{
    juce::String report;
    if (auto* d = deviceManager_.getCurrentAudioDevice())
    {
        const auto sr = d->getCurrentSampleRate();
        const auto bs = d->getCurrentBufferSizeSamples();
        const auto inLatencyMs = sr > 0.0 ? (1000.0 * d->getInputLatencyInSamples() / sr) : 0.0;
        const auto outLatencyMs = sr > 0.0 ? (1000.0 * d->getOutputLatencyInSamples() / sr) : 0.0;
        const auto activeInputs = d->getActiveInputChannels().countNumberOfSetBits();
        const auto activeOutputs = d->getActiveOutputChannels().countNumberOfSetBits();
        const auto xruns = deviceManager_.getXRunCount();
        const auto isAsio = deviceManager_.getCurrentAudioDeviceType().containsIgnoreCase("ASIO");

        report << "✓ AUDIO DEVICE\n    " << d->getName() << "\n\n";
        report << (isAsio ? "✓" : "⚠") << " DRIVER TYPE\n    " << deviceManager_.getCurrentAudioDeviceType();
        if (!isAsio)
            report << "  (ASIO recommended for lowest-latency live use when the interface provides it)";
        report << "\n\n";
        report << "✓ I/O\n    " << activeInputs << " active inputs  ·  " << activeOutputs << " active outputs\n\n";
        report << "✓ SAMPLE RATE\n    " << juce::String(sr, 0) << " Hz\n\n";
        report << "✓ BUFFER\n    " << bs << " samples\n\n";
        report << "✓ REPORTED I/O LATENCY\n    Input " << juce::String(inLatencyMs, 2)
               << " ms  ·  Output " << juce::String(outLatencyMs, 2) << " ms\n\n";
        report << (xruns == 0 ? "✓" : "⚠") << " XRUNS / DROPOUTS\n    " << xruns << "\n\n";
        report << (routeIsSafe(paLeft_.load(), paRight_.load(), clickOutput_.load()) ? "✓" : "✕")
               << " SAFE ROUTING\n    PA L " << (paLeft_.load() + 1) << "  ·  PA R " << (paRight_.load() + 1)
               << "  ·  CLICK " << (clickOutput_.load() < 0 ? juce::String("OFF") : juce::String(clickOutput_.load() + 1)) << "\n\n";
        report << (liveMonitorEnabled_.load() ? "⚠ LIVE INPUT MONITORING: ON" : "✓ LIVE INPUT MONITORING: OFF (safe default)") << "\n\n";
        if (lastAudioError_.isNotEmpty())
            report << "⚠ LAST AUDIO ERROR\n    " << lastAudioError_ << "\n\n";
        report << "AVAILABLE AUDIO DRIVERS\n" << buildDeviceInventoryText();

        statusLabel_.setText("Audio: " + d->getName() + " · " + deviceManager_.getCurrentAudioDeviceType()
            + " · " + juce::String(sr / 1000.0, 1) + " kHz · " + juce::String(bs) + " smp", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, xruns == 0 ? juce::Colour(good) : juce::Colour(warning));
    }
    else
    {
        report = "✕ AUDIO DEVICE\n    No hay dispositivo seleccionado.\n\nAbrí AUDIO / MIDI y elegí un driver. J3 Worship soporta cualquier interfaz que Windows exponga a JUCE mediante ASIO o WASAPI.\n\nAVAILABLE AUDIO DRIVERS\n" + buildDeviceInventoryText();
        if (lastAudioError_.isNotEmpty())
            report << "\n\nLAST ERROR\n" << lastAudioError_;
        statusLabel_.setText("Audio: sin dispositivo", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
    }
    diagnosticsLabel_.setText(report, juce::dontSendNotification);
}

void MainComponent::setLiveSection(const juce::String& name, j3::SectionKind kind, int bars)
{
    j3::Section section { name.toStdString(), kind, bars };
    if (!liveStarted_)
    {
        liveEngine_.setTempo(bpmSlider_.getValue(), clickGenerator_.numerator());
        liveEngine_.start(section);
        liveStarted_ = true;
        transportRunning_.store(true, std::memory_order_release);
        clickGenerator_.setEnabled(true);
        clickGenerator_.reset();
    }
    else if (kind == j3::SectionKind::FreePad)
    {
        liveEngine_.enterFreePad();
    }
    else
    {
        liveEngine_.request(section, j3::Quantize::Bar);
    }
    refreshLiveLabels();
}

void MainComponent::refreshLiveLabels()
{
    nowLabel_.setText("NOW: " + juce::String(liveEngine_.now()), juce::dontSendNotification);
    nextLabel_.setText("NEXT: " + juce::String(liveEngine_.next()), juce::dontSendNotification);
}

bool MainComponent::routeIsSafe(int paLeft, int paRight, int clickOutput) const noexcept
{
    return clickOutput < 0 || (clickOutput != paLeft && clickOutput != paRight);
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    for (int o = 0; o < numOutputChannels; ++o)
        if (outputChannelData[o] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[o], numSamples);

    const int left = paLeft_.load(std::memory_order_relaxed);
    const int right = paRight_.load(std::memory_order_relaxed);
    const int click = clickOutput_.load(std::memory_order_relaxed);
    const bool safePa = left >= 0 && right >= 0 && left < numOutputChannels && right < numOutputChannels
        && routeIsSafe(left, right, click) && outputChannelData[left] != nullptr && outputChannelData[right] != nullptr;

    if (liveMonitorEnabled_.load(std::memory_order_acquire) && safePa)
    {
        auto* outL = outputChannelData[left];
        auto* outR = outputChannelData[right];
        const bool mono = left == right;
        const auto master = masterGain_.load(std::memory_order_relaxed);
        const int channels = std::min({ numInputChannels, kMaxChannels, kVisibleChannels });
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* in = inputChannelData[ch];
            if (in == nullptr || channelMute_[ch].load(std::memory_order_relaxed))
                continue;

            const auto gain = channelGain_[ch].load(std::memory_order_relaxed) * master;
            const auto pan = juce::jlimit(-1.0f, 1.0f, channelPan_[ch].load(std::memory_order_relaxed));
            const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const auto gl = gain * std::cos(angle);
            const auto gr = gain * std::sin(angle);
            float peak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                auto x = channelDsp_[ch].process(in[i]);
                peak = std::max(peak, std::abs(x));
                if (mono)
                    outL[i] += x * gain;
                else
                {
                    outL[i] += x * gl;
                    outR[i] += x * gr;
                }
            }
            auto previous = channelMeter_[ch].load(std::memory_order_relaxed);
            while (peak > previous && !channelMeter_[ch].compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
        }

        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = std::tanh(outL[i]);
            if (!mono)
                outR[i] = std::tanh(outR[i]);
        }
    }

    if (transportRunning_.load(std::memory_order_acquire))
    {
        float* clickOut = nullptr;
        if (clickAudible_.load(std::memory_order_relaxed) && click >= 0 && click < numOutputChannels
            && routeIsSafe(left, right, click))
            clickOut = outputChannelData[click];
        const int beats = clickGenerator_.process(clickOut, numSamples);
        if (beats > 0)
            pendingBeatEvents_.fetch_add(beats, std::memory_order_relaxed);
    }

    if (recordingEnabled_.load(std::memory_order_acquire))
    {
        const int wanted = recordChannelCount_.load(std::memory_order_relaxed);
        const int channels = std::min({ wanted, numInputChannels, static_cast<int>(j3::kRecordMaxChannels) });
        if (channels == wanted && channels > 0)
        {
            int offset = 0;
            while (offset < numSamples)
            {
                const int frames = std::min<int>(static_cast<int>(j3::kRecordMaxFrames), numSamples - offset);
                std::array<const float*, j3::kRecordMaxChannels> ptrs {};
                bool valid = true;
                for (int ch = 0; ch < channels; ++ch)
                {
                    if (inputChannelData[ch] == nullptr) { valid = false; break; }
                    ptrs[static_cast<std::size_t>(ch)] = inputChannelData[ch] + offset;
                }
                if (valid)
                    recorder_.submit(ptrs.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(frames));
                offset += frames;
            }
        }
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const auto sr = device->getCurrentSampleRate();
    sampleRate_.store(sr, std::memory_order_release);
    bufferSize_.store(device->getCurrentBufferSizeSamples(), std::memory_order_release);
    for (auto& dsp : channelDsp_)
        dsp.prepare(sr);
    clickGenerator_.prepare(sr);
    clickGenerator_.setTempo(bpmSlider_.getValue());
    clickGenerator_.setEnabled(transportRunning_.load(std::memory_order_acquire));
    audioRunning_.store(true, std::memory_order_release);
    reconnectAttempts_ = 0;
    lastAudioError_.clear();

    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->refreshRoutingControls();
            safe->updateDiagnostics();
            safe->saveAudioState();
        }
    });
}

void MainComponent::audioDeviceStopped()
{
    audioRunning_.store(false, std::memory_order_release);
    liveMonitorEnabled_.store(false, std::memory_order_release);
    recordingEnabled_.store(false, std::memory_order_release);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
            safe->updateDiagnostics();
        }
    });
}

void MainComponent::audioDeviceError(const juce::String& errorMessage)
{
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this), errorMessage]
    {
        if (safe == nullptr)
            return;
        safe->lastAudioError_ = errorMessage;
        safe->liveMonitorEnabled_.store(false, std::memory_order_release);
        safe->liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
        safe->statusLabel_.setText("Audio error · intentando recuperar", juce::dontSendNotification);
        safe->statusLabel_.setColour(juce::Label::textColourId, juce::Colour(danger));
        safe->scheduleReconnect();
        safe->updateDiagnostics();
    });
}

void MainComponent::scheduleReconnect()
{
    audioRunning_.store(false, std::memory_order_release);
    lastReconnectAttemptMs_ = 0;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;
    saveAudioState();
    refreshRoutingControls();
    updateDiagnostics();
}

void MainComponent::timerCallback()
{
    for (auto& strip : strips_)
        if (strip) strip->timerTick();

    const int beats = pendingBeatEvents_.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < beats; ++i)
        if (liveStarted_) liveEngine_.tickBeat();
    if (beats > 0 && liveStarted_)
        refreshLiveLabels();

    static int ticks = 0;
    ++ticks;
    if (ticks % 30 == 0)
    {
        updateDiagnostics();
        updateClickUi();
        updateRecordingUi();
    }
    if (ticks % 150 == 0)
        saveAppState();

    if (!audioRunning_.load(std::memory_order_acquire) && !shuttingDown_.load(std::memory_order_acquire)
        && deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        const auto now = static_cast<std::uint64_t>(juce::Time::getMillisecondCounterHiRes());
        const auto waitMs = static_cast<std::uint64_t>(std::min(10000, 2000 + reconnectAttempts_ * 1500));
        if (lastReconnectAttemptMs_ == 0 || now - lastReconnectAttemptMs_ >= waitMs)
        {
            lastReconnectAttemptMs_ = now;
            ++reconnectAttempts_;
            deviceManager_.restartLastAudioDevice();
        }
    }
}

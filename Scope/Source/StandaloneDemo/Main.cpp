#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../Core/ScopeDataCollector.h"
#include "../JUCE/JuceScopeComponent.h"

namespace abd::scope {

/**
 * Main GUI Component hosting JuceScopeComponent and test signal controls.
 */
class MainComponent : public juce::AudioAppComponent,
                      private juce::Button::Listener,
                      private juce::Slider::Listener,
                      private juce::ComboBox::Listener
{
public:
    MainComponent()
    {
        // 1. Register Taps
        m_tapMaster = m_collector.registerTap("Master Out", ScopeTapType::StereoAudio);
        m_tapOsc1   = m_collector.registerTap("Osc 1", ScopeTapType::StereoAudio);
        m_collector.selectTap("Master Out");

        // 2. Initialize Scope Component
        m_scopeComponent = std::make_unique<JuceScopeComponent>(m_tapMaster, 44100.0f);
        m_scopeComponent->setMode(NativeScopeMode::Oscilloscope);
        addAndMakeVisible(*m_scopeComponent);

        // 3. UI Controls: Mode Selector Buttons
        auto setupButton = [this](juce::TextButton& btn, const juce::String& text) {
            btn.setButtonText(text);
            btn.addListener(this);
            addAndMakeVisible(btn);
        };

        setupButton(m_btnOsc, "Oscilloscope");
        setupButton(m_btnSpec, "Spectrum");
        setupButton(m_btnLiss, "Lissajous");
        setupButton(m_btnPhase, "Phase Meter");

        // 4. Waveform Selector
        m_waveSelector.addItem("Sine", 1);
        m_waveSelector.addItem("Sawtooth", 2);
        m_waveSelector.addItem("Square", 3);
        m_waveSelector.addItem("Triangle", 4);
        m_waveSelector.addItem("Noise", 5);
        m_waveSelector.setSelectedId(1);
        m_waveSelector.addListener(this);
        addAndMakeVisible(m_waveSelector);

        // 5. Frequency Slider
        m_sliderFreq.setRange(20.0, 2000.0, 1.0);
        m_sliderFreq.setValue(440.0);
        m_sliderFreq.setTextValueSuffix(" Hz");
        m_sliderFreq.addListener(this);
        addAndMakeVisible(m_sliderFreq);

        m_labelFreq.setText("Frequency:", juce::dontSendNotification);
        m_labelFreq.attachToComponent(&m_sliderFreq, true);
        addAndMakeVisible(m_labelFreq);

        // 6. Stereo Phase Spread Slider
        m_sliderPhase.setRange(0.0, 180.0, 1.0);
        m_sliderPhase.setValue(0.0);
        m_sliderPhase.setTextValueSuffix(" deg");
        m_sliderPhase.addListener(this);
        addAndMakeVisible(m_sliderPhase);

        m_labelPhase.setText("Stereo Phase:", juce::dontSendNotification);
        m_labelPhase.attachToComponent(&m_sliderPhase, true);
        addAndMakeVisible(m_labelPhase);

        // 7. Theme Selector
        m_themeSelector.addItem("MS2000 (Cyan)", 1);
        m_themeSelector.addItem("CZ-101 (Red)", 2);
        m_themeSelector.addItem("DeepMind (Amber)", 3);
        m_themeSelector.addItem("AudioLab (Green)", 4);
        m_themeSelector.setSelectedId(1);
        m_themeSelector.addListener(this);
        addAndMakeVisible(m_themeSelector);

        // Set Audio Channels (0 input, 2 output)
        setAudioChannels(0, 2);
        setSize(780, 520);
    }

    ~MainComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        m_currentSampleRate = static_cast<float>(sampleRate);
        m_scopeComponent->setSampleRate(m_currentSampleRate);
        m_leftBuffer.assign(static_cast<size_t>(samplesPerBlockExpected), 0.0f);
        m_rightBuffer.assign(static_cast<size_t>(samplesPerBlockExpected), 0.0f);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        const int numSamples = bufferToFill.numSamples;
        auto* leftOut = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
        auto* rightOut = bufferToFill.buffer->getNumChannels() > 1
                       ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
                       : leftOut;

        const float phaseDelta = (m_freq * 2.0f * 3.1415926535f) / m_currentSampleRate;
        const float spreadRadians = m_stereoPhaseDeg * (3.1415926535f / 180.0f);

        for (int i = 0; i < numSamples; ++i) {
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            switch (m_waveType) {
                case 1: // Sine
                    sampleL = std::sin(m_phase);
                    sampleR = std::sin(m_phase + spreadRadians);
                    break;
                case 2: // Saw
                    sampleL = (m_phase / 3.1415926535f) - 1.0f;
                    sampleR = (std::fmod(m_phase + spreadRadians, 2.0f * 3.1415926535f) / 3.1415926535f) - 1.0f;
                    break;
                case 3: // Square
                    sampleL = m_phase < 3.1415926535f ? 0.8f : -0.8f;
                    sampleR = std::fmod(m_phase + spreadRadians, 2.0f * 3.1415926535f) < 3.1415926535f ? 0.8f : -0.8f;
                    break;
                case 4: // Triangle
                    sampleL = 2.0f * std::abs(2.0f * (m_phase / (2.0f * 3.1415926535f)) - 1.0f) - 1.0f;
                    sampleR = 2.0f * std::abs(2.0f * (std::fmod(m_phase + spreadRadians, 2.0f * 3.1415926535f) / (2.0f * 3.1415926535f)) - 1.0f) - 1.0f;
                    break;
                case 5: // Noise
                    sampleL = m_random.nextFloat() * 2.0f - 1.0f;
                    sampleR = m_random.nextFloat() * 2.0f - 1.0f;
                    break;
            }

            sampleL *= 0.35f;
            sampleR *= 0.35f;

            leftOut[i] = sampleL;
            rightOut[i] = sampleR;

            m_phase += phaseDelta;
            if (m_phase >= 2.0f * 3.1415926535f) {
                m_phase -= 2.0f * 3.1415926535f;
            }
        }

        // Tap Audio Stream for Native Scope
        if (m_tapMaster) {
            m_tapMaster->writeStereo(leftOut, rightOut, static_cast<size_t>(numSamples));
        }
    }

    void releaseResources() override {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff060a10));

        // Header Title
        g.setColour(m_currentAccent);
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawText("ABDScope — Native C++ Standalone Visualizer", 14, 10, 400, 24, juce::Justification::left);
    }

    void resized() override
    {
        const int margin = 12;
        const int topBarH = 40;
        const int controlPanelH = 100;
        auto bounds = getLocalBounds().reduced(margin);

        // Top Visual Mode Buttons
        auto topArea = bounds.removeFromTop(topBarH);
        const int btnW = 110;
        m_btnOsc.setBounds(topArea.removeFromLeft(btnW).reduced(2));
        m_btnSpec.setBounds(topArea.removeFromLeft(btnW).reduced(2));
        m_btnLiss.setBounds(topArea.removeFromLeft(btnW).reduced(2));
        m_btnPhase.setBounds(topArea.removeFromLeft(btnW).reduced(2));

        topArea.removeFromLeft(20);
        m_themeSelector.setBounds(topArea.removeFromRight(150).reduced(2));

        // Bottom Controls
        auto bottomArea = bounds.removeFromBottom(controlPanelH);
        bottomArea.removeFromTop(10);

        auto row1 = bottomArea.removeFromTop(36);
        m_waveSelector.setBounds(row1.removeFromLeft(140).reduced(2));
        row1.removeFromLeft(80);
        m_sliderFreq.setBounds(row1.reduced(2));

        auto row2 = bottomArea.removeFromTop(36);
        row2.removeFromLeft(220);
        m_sliderPhase.setBounds(row2.reduced(2));

        // Scope Viewport
        bounds = bounds.reduced(0, 4);
        m_scopeComponent->setBounds(bounds);
    }

    void buttonClicked(juce::Button* btn) override
    {
        if (btn == &m_btnOsc)   m_scopeComponent->setMode(NativeScopeMode::Oscilloscope);
        if (btn == &m_btnSpec)  m_scopeComponent->setMode(NativeScopeMode::Spectrum);
        if (btn == &m_btnLiss)  m_scopeComponent->setMode(NativeScopeMode::Lissajous);
        if (btn == &m_btnPhase) m_scopeComponent->setMode(NativeScopeMode::PhaseMeter);
    }

    void sliderValueChanged(juce::Slider* slider) override
    {
        if (slider == &m_sliderFreq)  m_freq = static_cast<float>(slider->getValue());
        if (slider == &m_sliderPhase) m_stereoPhaseDeg = static_cast<float>(slider->getValue());
    }

    void comboBoxChanged(juce::ComboBox* box) override
    {
        if (box == &m_waveSelector) {
            m_waveType = box->getSelectedId();
        }
        else if (box == &m_themeSelector) {
            switch (box->getSelectedId()) {
                case 1: // MS2000 Cyan
                    m_currentAccent = juce::Colour(0xff00c3ff);
                    m_scopeComponent->setTraceColour(m_currentAccent, juce::Colour(0xffff007f));
                    m_scopeComponent->setBackgroundColour(juce::Colour(0xff080c14));
                    break;
                case 2: // CZ101 Red
                    m_currentAccent = juce::Colour(0xffff3344);
                    m_scopeComponent->setTraceColour(m_currentAccent, juce::Colour(0xff00c3ff));
                    m_scopeComponent->setBackgroundColour(juce::Colour(0xff12080a));
                    break;
                case 3: // DeepMind Amber
                    m_currentAccent = juce::Colour(0xffffaa00);
                    m_scopeComponent->setTraceColour(m_currentAccent, juce::Colour(0xff00e676));
                    m_scopeComponent->setBackgroundColour(juce::Colour(0xff140e06));
                    break;
                case 4: // AudioLab Green
                    m_currentAccent = juce::Colour(0xff00e676);
                    m_scopeComponent->setTraceColour(m_currentAccent, juce::Colour(0xff00c3ff));
                    m_scopeComponent->setBackgroundColour(juce::Colour(0xff06120a));
                    break;
            }
            repaint();
        }
    }

private:
    ScopeDataCollector m_collector;
    ScopeTap* m_tapMaster { nullptr };
    ScopeTap* m_tapOsc1 { nullptr };

    std::unique_ptr<JuceScopeComponent> m_scopeComponent;

    juce::TextButton m_btnOsc, m_btnSpec, m_btnLiss, m_btnPhase;
    juce::ComboBox m_waveSelector;
    juce::ComboBox m_themeSelector;
    juce::Slider m_sliderFreq, m_sliderPhase;
    juce::Label m_labelFreq, m_labelPhase;

    float m_freq { 440.0f };
    float m_stereoPhaseDeg { 0.0f };
    float m_phase { 0.0f };
    float m_currentSampleRate { 44100.0f };
    int m_waveType { 1 };
    juce::Random m_random;
    juce::Colour m_currentAccent { 0xff00c3ff };

    std::vector<float> m_leftBuffer;
    std::vector<float> m_rightBuffer;
};

// Application Boilerplate
class StandaloneScopeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "ABDScope Native C++ Demo"; }
    const juce::String getApplicationVersion() override    { return "0.3.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String&) override
    {
        m_mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        m_mainWindow = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colour(0xff060a10),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(540, 380, 1920, 1080);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> m_mainWindow;
};

} // namespace abd::scope

START_JUCE_APPLICATION(abd::scope::StandaloneScopeApp)

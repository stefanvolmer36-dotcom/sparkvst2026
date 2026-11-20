#include "PluginEditor.h"
#include "BinaryData.h"

class SparkLEEditor::Browser : public juce::WebBrowserComponent
{
public:
    Browser(const Options& options, std::function<void()> readyCallback)
        : WebBrowserComponent(options), ready(std::move(readyCallback))
    {
    }

    void pageFinishedLoading(const juce::String&) override
    {
        if (ready)
            ready();
    }

private:
    std::function<void()> ready;
};

namespace
{
    std::vector<std::byte> bytesFrom(const char* data, int size)
    {
        const auto* raw = reinterpret_cast<const std::byte*>(data);
        return { raw, raw + size };
    }

    int macroFromName(const juce::String& name)
    {
        if (name == "volume") return 0;
        if (name == "drive") return 1;
        if (name == "decay") return 2;
        if (name == "tune") return 3;
        if (name == "x") return 4;
        if (name == "y") return 5;
        if (name == "bpm") return 6;
        return -1;
    }

    int transportFromName(const juce::String& name)
    {
        if (name == "play") return 0;
        if (name == "stop") return 1;
        if (name == "record") return 2;
        if (name == "rewind") return 3;
        if (name == "forward") return 4;
        if (name == "loop") return 5;
        if (name == "bank") return 6;
        return -1;
    }
}

SparkLEEditor::SparkLEEditor(SparkLEAudioProcessor& owner)
    : AudioProcessorEditor(owner), processor(owner)
{
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(1100, 640, 3840, 2160);
    setSize(1440, 860);

    auto options = juce::WebBrowserComponent::Options {}
                       .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withNativeIntegrationEnabled()
                       .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2 {}
                                                   .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                                            .getChildFile("SparkLEDrumEngine"))
                                                   .withStatusBarDisabled()
                                                   .withBackgroundColour(juce::Colour::fromRGB(7, 8, 10)))
                       .withResourceProvider([this](const auto& url) { return loadResource(url); },
                                             juce::URL(juce::WebBrowserComponent::getResourceProviderRoot()).getOrigin())
                       .withEventListener("control", [this](const juce::var& payload) { onControl(payload); });

    fallback.setJustificationType(juce::Justification::centred);
    fallback.setColour(juce::Label::textColourId, juce::Colours::white);
    fallback.setText("Die Oberflaeche braucht die Microsoft Edge WebView2 Runtime.\nDanach das Plugin neu laden.",
                     juce::dontSendNotification);

    if (juce::WebBrowserComponent::areOptionsSupported(options))
    {
        browser = std::make_unique<Browser>(options, [this] { pageReady = true; });
        addAndMakeVisible(*browser);
        browser->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }
    else
    {
        addAndMakeVisible(fallback);
    }

    startTimerHz(30);
}

SparkLEEditor::~SparkLEEditor()
{
    stopTimer();
}

void SparkLEEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(7, 8, 10));
}

void SparkLEEditor::resized()
{
    if (browser != nullptr)
        browser->setBounds(getLocalBounds());
    else
        fallback.setBounds(getLocalBounds().reduced(24));
}

std::optional<juce::WebBrowserComponent::Resource> SparkLEEditor::loadResource(const juce::String& url) const
{
    auto path = url.upToFirstOccurrenceOf("?", false, false);
    if (path.endsWithChar('/'))
        path += "index.html";
    if (path.startsWithChar('/'))
        path = path.substring(1);
    if (path.contains("/"))
        path = path.fromLastOccurrenceOf("/", false, false);

    const char* data = nullptr;
    int size = 0;
    const char* mime = "application/octet-stream";

    if (path.isEmpty() || path == "index.html")
    {
        data = BinaryData::index_html;
        size = BinaryData::index_htmlSize;
        mime = "text/html";
    }
    else if (path == "app.js")
    {
        data = BinaryData::app_js;
        size = BinaryData::app_jsSize;
        mime = "text/javascript";
    }
    else if (path == "three.module.min.js")
    {
        data = BinaryData::three_module_min_js;
        size = BinaryData::three_module_min_jsSize;
        mime = "text/javascript";
    }
    else
    {
        return std::nullopt;
    }

    return juce::WebBrowserComponent::Resource { bytesFrom(data, size), mime };
}

void SparkLEEditor::onControl(const juce::var& payload)
{
    auto* object = payload.getDynamicObject();
    if (object == nullptr)
        return;

    const auto kind = object->getProperty("kind").toString();
    if (kind == "pad")
    {
        processor.pushUi({ SparkLEAudioProcessor::UiKind::note,
                           (int) object->getProperty("index"),
                           (float) object->getProperty("velocity") });
    }
    else if (kind == "macro")
    {
        const int id = macroFromName(object->getProperty("name").toString());
        if (id >= 0)
            processor.pushUi({ SparkLEAudioProcessor::UiKind::macro, id, (float) object->getProperty("value") });
    }
    else if (kind == "transport")
    {
        const int id = transportFromName(object->getProperty("action").toString());
        if (id >= 0)
            processor.pushUi({ SparkLEAudioProcessor::UiKind::transport, id, (float) object->getProperty("value") });
    }
    else if (kind == "clear")
    {
        processor.pushUi({ SparkLEAudioProcessor::UiKind::clear, 0, 0.0f });
    }
}

void SparkLEEditor::timerCallback()
{
    processor.publishParameters();
    if (browser == nullptr || !pageReady)
        return;

    const auto state = processor.view();
    auto* object = new juce::DynamicObject();
    juce::Array<juce::var> pads, steps;
    for (float pad : state.pads)
        pads.add(pad);
    for (int step : state.steps)
        steps.add(step);

    object->setProperty("pads", pads);
    object->setProperty("steps", steps);
    object->setProperty("playhead", state.playhead);
    object->setProperty("bank", state.bank);
    object->setProperty("drive", state.drive);
    object->setProperty("decay", state.decay);
    object->setProperty("tune", state.tune);
    object->setProperty("x", state.x);
    object->setProperty("y", state.y);
    object->setProperty("volume", state.volume);
    object->setProperty("bpm", state.bpm);
    object->setProperty("playing", state.playing);
    object->setProperty("recording", state.recording);
    object->setProperty("loop", state.loop);
    object->setProperty("meter", state.meter);
    browser->emitEventIfBrowserIsVisible("state", juce::var(object));
}

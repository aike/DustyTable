#include "DustyTableProcessor.h"
#include "DustyTableEditor.h"

namespace {

constexpr float dryOffDb = -60.0f;

juce::NormalisableRange<float> skewedRange(float lo, float hi, float step, float centre)
{
    juce::NormalisableRange<float> r(lo, hi, step);
    r.setSkewForCentre(centre);
    return r;
}

} // namespace

DustyTableProcessor::DustyTableProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "DustyTable", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DustyTableProcessor::createLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto pct = Attr().withLabel("%");

    ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "rpm", 1 }, "Speed", StringArray { "33", "45" }, 0));
    ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "disc", 1 }, "Disc", StringArray { "12\"", "10\"", "7\"" }, 0));
    ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "stylus", 1 }, "Stylus", StringArray { "Conical", "Elliptical" }, 0));
    ps.push_back(std::make_unique<P>(ParameterID { "vtf", 1 }, "Tracking Force", NormalisableRange<float>(0.75f, 4.0f, 0.05f), 2.0f, Attr().withLabel("g")));
    ps.push_back(std::make_unique<P>(ParameterID { "position", 1 }, "Position", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, pct));
    ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { "auto", 1 }, "Auto Advance", true));
    ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { "arm", 1 }, "Start (on) / Stop (off)", true));
    ps.push_back(std::make_unique<P>(ParameterID { "cue", 1 }, "Cue Damping", NormalisableRange<float>(0.5f, 4.0f, 0.05f), 2.0f, Attr().withLabel("s")));
    ps.push_back(std::make_unique<P>(ParameterID { "side", 1 }, "Side Length", NormalisableRange<float>(1.0f, 40.0f, 0.5f), 20.0f, Attr().withLabel("min")));

    ps.push_back(std::make_unique<P>(ParameterID { "dustDensity", 1 }, "Dust Density", skewedRange(0.0f, 60.0f, 0.1f, 8.0f), 6.0f, Attr().withLabel("/m")));
    ps.push_back(std::make_unique<P>(ParameterID { "dustSize", 1 }, "Dust Size", skewedRange(1.0f, 40.0f, 0.1f, 10.0f), 12.0f, Attr().withLabel("um")));

    ps.push_back(std::make_unique<AudioParameterInt>(ParameterID { "scratchCount", 1 }, "Scratches", 0, dustytable::maxScratches, 1));
    ps.push_back(std::make_unique<P>(ParameterID { "scratchDepth", 1 }, "Scratch Depth", skewedRange(2.0f, 60.0f, 0.1f, 15.0f), 15.0f, Attr().withLabel("um")));
    ps.push_back(std::make_unique<P>(ParameterID { "scratchLength", 1 }, "Scratch Length", NormalisableRange<float>(5.0f, 100.0f, 0.1f), 60.0f, pct));
    ps.push_back(std::make_unique<P>(ParameterID { "scratchPileUp", 1 }, "Scratch Pile-up", skewedRange(0.2f, 4.0f, 0.01f, 1.5f), 1.5f, Attr().withLabel("mm")));
    ps.push_back(std::make_unique<AudioParameterInt>(ParameterID { "scratchSeed", 1 }, "Scratch Layout", 1, 999, 1));

    ps.push_back(std::make_unique<P>(ParameterID { "static", 1 }, "Static Electricity", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, pct));

    ps.push_back(std::make_unique<P>(ParameterID { "noise", 1 }, "Noise Level", NormalisableRange<float>(-36.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel("dB")));
    ps.push_back(std::make_unique<P>(ParameterID { "mech", 1 }, "Mechanics Level", NormalisableRange<float>(-36.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel("dB")));
    ps.push_back(std::make_unique<P>(ParameterID { "dry", 1 }, "Dry Level", NormalisableRange<float>(dryOffDb, 6.0f, 0.1f), 0.0f,
        Attr().withLabel("dB").withStringFromValueFunction([](float v, int) {
            return v <= dryOffDb ? String("Off") : String(v, 1);
        })));

    return { ps.begin(), ps.end() };
}

dustytable::EngineParams DustyTableProcessor::readParams() const
{
    dustytable::EngineParams p;
    p.rpmIndex = static_cast<int>(param("rpm"));
    p.discIndex = static_cast<int>(param("disc"));
    p.stylusIndex = static_cast<int>(param("stylus"));
    p.trackingForceGrams = param("vtf");
    p.position = param("position") * 0.01;
    p.autoAdvance = param("auto") > 0.5f;
    p.sideMinutes = param("side");
    p.dustPerMetre = param("dustDensity");
    p.dustMaxMicrons = param("dustSize");
    p.scratchCount = static_cast<int>(param("scratchCount"));
    p.scratchDepthMicrons = param("scratchDepth");
    p.scratchLength = param("scratchLength") * 0.01;
    p.scratchPileUpMillimetres = param("scratchPileUp");
    p.scratchSeed = static_cast<int>(param("scratchSeed"));
    p.staticAmount = param("static") * 0.01;
    p.armDown = param("arm") > 0.5f;
    p.cueSeconds = param("cue");
    return p;
}

void DustyTableProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    const auto p = readParams();
    engine.setParams(p);
    engine.setArmImmediate(p.armDown);
    armImmediatePending = false;
    noise.setSize(5, samplesPerBlock);
    noiseGain.reset(sampleRate, 0.05);
    mechanicsGain.reset(sampleRate, 0.05);
    dryGain.reset(sampleRate, 0.05);
    noiseGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(param("noise")));
    mechanicsGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(param("mech")));
    dryGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(param("dry"), dryOffDb));
}

bool DustyTableProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void DustyTableProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (n == 0) return;

    // A loaded session starts in its saved arm state instead of replaying the
    // hand actions.
    const auto params = readParams();
    engine.setParams(params);
    if (armImmediatePending.exchange(false)) engine.setArmImmediate(params.armDown);

    // Follow the host transport while it plays, so scratches land on the same
    // beat every time a section is replayed; otherwise keep the disc spinning.
    double t = internalTime;
    if (auto* hostPlayHead = getPlayHead())
        if (auto pos = hostPlayHead->getPosition())
            if (pos->getIsPlaying())
                if (auto seconds = pos->getTimeInSeconds())
                    t = *seconds;
    internalTime = t + n / getSampleRate();

    noise.setSize(5, n, false, false, true);
    float* nl = noise.getWritePointer(0);
    float* nr = noise.getWritePointer(1);
    float* ml = noise.getWritePointer(2);
    float* mr = noise.getWritePointer(3);
    float* contact = noise.getWritePointer(4);
    engine.process(nl, nr, ml, mr, contact, n, t);

    uiPhase.store(engine.getPhase());
    uiPosition.store(engine.getPosition());
    uiRadius.store(engine.getRadius());
    uiArmTransit.store(engine.getArmTransit());
    uiArmHeight.store(engine.getArmHeight());
    uiLifterRaised.store(engine.isLifterRaised());
    uiMotorRunning.store(engine.isMotorRunning());
    uiClamped.store(engine.isClamped());

    noiseGain.setTargetValue(juce::Decibels::decibelsToGain(param("noise")));
    mechanicsGain.setTargetValue(juce::Decibels::decibelsToGain(param("mech")));
    dryGain.setTargetValue(juce::Decibels::decibelsToGain(param("dry"), dryOffDb));

    // The music is on the record: it only plays while the stylus sits in the groove.
    float* l = buffer.getWritePointer(0);
    float* r = channels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < n; ++i) {
        const float gn = noiseGain.getNextValue();
        const float gm = mechanicsGain.getNextValue();
        const float gd = dryGain.getNextValue() * contact[i];
        if (r != nullptr) {
            l[i] = l[i] * gd + nl[i] * gn + ml[i] * gm;
            r[i] = r[i] * gd + nr[i] * gn + mr[i] * gm;
        } else {
            // A mono cartridge only senses lateral motion.
            l[i] = l[i] * gd + 0.5f * (nl[i] + nr[i]) * gn + 0.5f * (ml[i] + mr[i]) * gm;
        }
    }
    for (int ch = 2; ch < channels; ++ch) buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* DustyTableProcessor::createEditor() { return new DustyTableEditor(*this); }

void DustyTableProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void DustyTableProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()) || xml->hasTagName("PhysVinyl")) { // also sessions saved under the old name
            auto state = juce::ValueTree::fromXml(*xml);
            if (!state.hasType(apvts.state.getType())) {
                juce::ValueTree renamed(apvts.state.getType());
                renamed.copyPropertiesAndChildrenFrom(state, nullptr);
                state = renamed;
            }
            apvts.replaceState(state);
            armImmediatePending = true;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DustyTableProcessor(); }

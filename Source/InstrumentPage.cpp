#include "InstrumentPage.h"
#include "PickedFile.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour coveredColour { 0xffe0a24f };
const juce::Colour metal { 0xff9aa3ad };

using whistle::kHoleNames;

} // namespace

//==============================================================================
WhistleView::WhistleView (AudioEngine& a) : audio (a) {}

float WhistleView::yFor (double metres) const
{
    // Drawn upright: the mouthpiece at the top, the foot at the bottom; room above for the mouthpiece, below for the
    // length.
    const auto area = getLocalBounds().toFloat().reduced (0.0f, 10.0f);
    return area.getY() + 70.0f + (float) (metres * scale());
}

float WhistleView::scale() const
{
    const auto area = getLocalBounds().toFloat().reduced (0.0f, 10.0f);
    return (float) ((area.getHeight() - 70.0f - 40.0f) / std::max (0.1, geometry.boreLength));  // px per metre
}

float WhistleView::axisX() const { return getWidth() * 0.42f; }

juce::Rectangle<float> WhistleView::holeBounds (int hole) const
{
    // Front holes on the tube's face; the thumb hole (on the back) beside the tube, to the left.
    const auto& h = geometry.holes[(size_t) hole];
    const float d = std::max (10.0f, (float) h.diameter * scale());
    const float x = hole == 0 ? axisX() - (float) geometry.radiusAt (h.position) * scale() - 26.0f : axisX();
    return juce::Rectangle<float> (d, d).withCentre ({ x, yFor (h.position) });
}

juce::Rectangle<float> WhistleView::footBounds() const
{
    const float halfWidth = (float) geometry.radiusAt (geometry.boreLength) * scale() + 8.0f;
    return { axisX() - halfWidth, yFor (geometry.boreLength) - 4.0f, 2.0f * halfWidth, 12.0f };
}

void WhistleView::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    const float px = scale(), centre = axisX();
    const float topHalf = (float) geometry.radiusAt (0.0) * px + 4.0f;

    // Body: the bore profile (including any head section), drawn 4 px wider than the bore for the wall.
    juce::Path body;
    const float y0 = yFor (0.0);
    constexpr int steps = 100;
    body.startNewSubPath (centre - topHalf, y0 - 55.0f);
    for (int i = 0; i <= steps; ++i)
    {
        const double y = geometry.boreLength * i / steps;
        body.lineTo (centre - (float) geometry.radiusAt (y) * px - 4.0f, yFor (y));
    }
    for (int i = steps; i >= 0; --i)
    {
        const double y = geometry.boreLength * i / steps;
        body.lineTo (centre + (float) geometry.radiusAt (y) * px + 4.0f, yFor (y));
    }
    body.lineTo (centre + topHalf, y0 - 55.0f);
    body.closeSubPath();
    g.setColour (metal.withAlpha (0.35f));
    g.fillPath (body);
    g.setColour (metal);
    g.strokePath (body, juce::PathStrokeType (1.5f));

    // The window, on the front face just above the bore's start
    const float windowLength = std::max (6.0f, (float) geometry.windowLength * px);
    const float windowWidth = std::min (2.0f * topHalf - 4.0f, std::max (8.0f, (float) geometry.windowWidth * px));
    g.setColour (panel.darker (0.4f));
    g.fillRect (juce::Rectangle<float> (centre - 0.5f * windowWidth, y0 - windowLength, windowWidth, windowLength));
    g.setColour (text.withAlpha (0.7f));
    g.setFont (13.0f);
    g.drawText ("window", juce::Rectangle<float> (centre + topHalf + 6.0f, y0 - windowLength - 4.0f, 80.0f, 16.0f),
                juce::Justification::centredLeft);

    // Holes in use, with live openness; names to the right (the thumb's to the left)
    g.setFont (14.0f);
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        if (! geometry.holes[(size_t) i].enabled)
            continue;
        const auto r = holeBounds (i);
        const float open = audio.shownOpenness[(size_t) i].load();
        g.setColour (panel.darker (0.6f));
        g.fillEllipse (r);
        g.setColour (coveredColour.withAlpha (1.0f - open));
        g.fillEllipse (r.reduced (1.0f));
        g.setColour (metal);
        if (i == 0)
        {
            juce::Path outline;
            outline.addEllipse (r);
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (outline, outline, dashes, 2);
            g.fillPath (outline);  // Dashed: it is on the back
        }
        else
        {
            g.drawEllipse (r, 1.2f);
        }
        g.setColour (text);
        const float halfWidth = (float) geometry.radiusAt (geometry.holes[(size_t) i].position) * px + 4.0f;
        if (i == 0)
            g.drawText ("LT (back)", juce::Rectangle<float> (r.getX() - 74.0f, r.getCentreY() - 8.0f, 70.0f, 16.0f), juce::Justification::centredRight);
        else
            g.drawText (kHoleNames[(size_t) i], juce::Rectangle<float> (centre + halfWidth + 8.0f, r.getCentreY() - 8.0f, 40.0f, 16.0f),
                        juce::Justification::centredLeft);
    }

    // Foot, if the bell sensor can close it
    if (geometry.bellClosesFoot)
    {
        const auto r = footBounds();
        g.setColour (coveredColour.withAlpha (1.0f - audio.shownFootOpenness.load()));
        g.fillRect (r);
        g.setColour (metal);
        g.drawRect (r, 1.2f);
        g.setColour (text);
        g.drawText ("bell", r.translated (r.getWidth() + 6.0f, 0.0f).withWidth (40.0f), juce::Justification::centredLeft);
    }

    g.setColour (text.withAlpha (0.7f));
    g.setFont (13.0f);
    g.drawText (juce::String (geometry.boreLength * 1000.0, 1) + " mm, window to foot", getLocalBounds().removeFromBottom (22).toFloat(),
                juce::Justification::centred);
}

void WhistleView::mouseDown (const juce::MouseEvent& e)
{
    if (! audio.manualPlay)
        return;
    auto toggle = [&] (std::atomic<float>& open)
    {
        if (e.mods.isPopupMenu())
            open = 0.5f;
        else
            open = open.load() > 0.5f ? 0.0f : 1.0f;
        repaint();
    };
    for (int i = 0; i < whistle::kNumHoles; ++i)
        if (geometry.holes[(size_t) i].enabled && holeBounds (i).expanded (6.0f).contains (e.position))
            toggle (audio.manualOpenness[(size_t) i]);
    if (geometry.bellClosesFoot && footBounds().expanded (6.0f).contains (e.position))
        toggle (audio.manualFootOpenness);
}

//==============================================================================
InstrumentPage::InstrumentPage (AudioEngine& a) : audio (a)
{
    geometry = whistle::presets().front();

    // Instrument choice: built-in presets, then the player's own.
    refreshPresetList();
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= kUserPresetId)
            loadUserPreset (id - kUserPresetId);
        else if (id > 0)
            loadPreset (id - 1);
        deleteButton.setEnabled (id >= kUserPresetId);
    };

    for (size_t i = 0; i < whistle::materials().size(); ++i)
        materialBox.addItem (whistle::materials()[i].name, (int) i + 1);
    materialBox.setSelectedId (1, juce::dontSendNotification);
    materialBox.onChange = [this]
    {
        const auto& m = whistle::materials()[(size_t) materialBox.getSelectedId() - 1];
        geometry.wallThickness = m.wallThickness;
        geometry.wallLoss = m.wallLoss;
        refreshSliders();
        geometryChanged();
    };

    static const char* keys[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        keyBox.addItem (keys[i], i + 1);
    keyBox.setSelectedId (3, juce::dontSendNotification);
    octaveBox.addItem ("high (C5-B5)", 1);
    octaveBox.addItem ("low (C4-B4)", 2);
    octaveBox.setSelectedId (1, juce::dontSendNotification);
    octaveBox.setTooltip ("The instrument's lowest note: in octave 5 (high whistles, recorders) or 4 (low whistles)");
    keyBox.onChange = octaveBox.onChange = [this]
    {
        audio.setScaleInstrument (geometry, lowestNoteHz());
        if (onInstrumentChanged != nullptr)
            onInstrumentChanged();
    };

    referenceBox.setSliderStyle (juce::Slider::IncDecButtons);
    referenceBox.setScrollWheelEnabled (false);
    referenceBox.setRange (380.0, 500.0, 0.5);
    referenceBox.setTextValueSuffix (" Hz");
    referenceBox.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 62, 20);
    referenceBox.setValue (geometry.referenceA, juce::dontSendNotification);
    referenceBox.setTooltip ("Pitch standard: the frequency of A4 that the tuner, the tuning table and the scale aim for "
                             "(440 Hz; e.g. 442 for many orchestras, 415 for baroque pitch). Tune holes retunes the instrument for it.");
    referenceBox.onValueChange = [this]
    {
        geometry.referenceA = referenceBox.getValue();
        audio.setScaleInstrument (geometry, lowestNoteHz());
        if (onInstrumentChanged != nullptr)
            onInstrumentChanged();
    };
    referenceLabel.setColour (juce::Label::textColourId, text);
    referenceLabel.setJustificationType (juce::Justification::centredRight);
    refreshers.push_back ([this] { referenceBox.setValue (geometry.referenceA, juce::dontSendNotification); });

    chartBox.addItem ("Whistle fingering", 1);
    chartBox.addItem ("Recorder fingering (baroque)", 2);
    chartBox.setTooltip ("Which fingering chart the Tuning workshop's table and tuner use. The sound never uses a chart.");
    chartBox.onChange = [this]
    {
        const int id = chartBox.getSelectedId();
        geometry.chart = id == 2 ? whistle::FingeringChart::baroqueRecorder : whistle::FingeringChart::whistle;
        geometryChanged();
    };
    bellButton.setColour (juce::ToggleButton::textColourId, text);
    bellButton.setTooltip ("Covering WARBL's bell sensor closes the instrument's open foot (a recorder technique for some high notes). "
                           "Off: the bell sensor is ignored.");
    bellButton.setWantsKeyboardFocus (false);
    bellButton.onClick = [this]
    {
        geometry.bellClosesFoot = bellButton.getToggleState();
        geometryChanged();
    };

    savePresetButton.onClick = [this] { savePreset(); };
    deleteButton.onClick = [this] { deletePreset(); };
    exportButton.onClick = [this] { saveToFile(); };
    importButton.onClick = [this] { loadFromFile(); };
    savePresetButton.setTooltip ("Save this whistle (geometry and voicing) as one of your presets, in the list above");
    deleteButton.setTooltip ("Delete the selected preset of yours");
    exportButton.setTooltip ("Save this whistle to a .whistle file anywhere, e.g. to share it");
    importButton.setTooltip ("Load a .whistle file");
    deleteButton.setEnabled (false);

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &materialBox, &tuneStatus,
                                                             &savePresetButton, &deleteButton, &exportButton, &importButton, &bellButton })
        addAndMakeVisible (c);
    tuneStatus.setColour (juce::Label::textColourId, text);

    auto mm = [] (double& v) { return std::make_pair ([&v] { return v * 1000.0; }, [&v] (double x) { v = x / 1000.0; }); };
    auto addLength = [&] (juce::String name, double& v, double min, double max, double step = 0.1, RowKind kind = RowKind::normal) -> juce::Slider&
    {
        auto [get, set] = mm (v);
        return addRow (std::move (name), min, max, step, "mm", get, set, true, kind);
    };

    addHeading ("Bore");
    addRow ("Tuning slide (pulled out)", whistle::kSlideMin * 1000.0, whistle::kSlideMax * 1000.0, 0.1, "mm", [this] { return geometry.slide * 1000.0; },
            [this] (double v)
            {
                whistle::setSlide (geometry, v / 1000.0);
                refreshSliders();  // The bore length and the holes moved with it
            })
        .setTooltip ("How far the head is pulled out of the body, as on a tunable whistle or a recorder's head joint (negative: "
                     "pushed in further than made). It adds tube between the head and the first hole, so the bore length and "
                     "every hole move with it. Pulling out lowers every note, the top notes most. The Tuning workshop tab's Slide tuned "
                     "column has it set for the reference A; its Tune slide moves it there.");
    addLength ("Length (window to foot)", geometry.boreLength, 100.0, 700.0)
        .setTooltip ("The tube's length from the window's edge to the open foot. Longer plays lower. Tune holes sets it from the key you choose.");
    addLength ("Diameter at top", geometry.boreDiameterTop, 6.0, 30.0)
        .setTooltip ("The inside diameter of the tube at the window end. The bore changes in a straight line from here to the foot: the same at both ends is a cylinder, narrower at the foot is a recorder's reverse cone.");
    addLength ("Diameter at foot", geometry.boreDiameterFoot, 6.0, 30.0)
        .setTooltip ("The inside diameter of the tube at the foot end. Together with the diameter at the top it sets the taper, which mostly changes how the octaves line up.");
    addLength ("Head section length (0 = none)", geometry.headLength, 0.0, 150.0, 0.5)
        .setTooltip ("An optional section just below the window whose diameter differs from the rest of the bore, blending into it over this length. 0 = no head section.");
    addLength ("Head diameter at the window", geometry.headDiameter, 4.0, 30.0)
        .setTooltip ("The bore's diameter right at the window, when there is a head section. Narrower than the bore makes the second octave sharper against the first, wider makes it flatter.");
    addLength ("Head: straight part", geometry.headStraightLength, 0.0, 150.0, 0.5)
        .setTooltip ("How far the head keeps its diameter at the window before it starts blending into the bore. 0 = it blends all the way from the window. Must be shorter than the head section.");
    addLength ("Wall thickness", geometry.wallThickness, 0.2, 8.0)
        .setTooltip ("The tube wall's thickness, which is the height of the short chimney each tone hole makes. Thicker walls put more air in every hole, which lowers the notes it vents.");
    addRow ("Wall roughness (losses)", 1.0, 2.0, 0.01, "x", [this] { return geometry.wallLoss; }, [this] (double v) { geometry.wallLoss = v; })
        .setTooltip ("Sound lost at the bore's wall, as a multiple of a smooth metal tube (1). Rougher walls, such as wood, lose more: the resonances are weaker and the tone a little softer.");

    addHeading ("Tone holes (unticked = plugged): position from the window, diameter");
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        holeRow[(size_t) i] = rows.size();
        addLength (kHoleNames[(size_t) i], geometry.holes[(size_t) i].position, 10.0, 690.0, 0.1, RowKind::holePosition)
            .setTooltip (juce::String (i == 0 ? "The left thumb's hole, on the back of the tube. " : "A front finger hole. ")
                         + "The tick box uses it; unticked it is plugged and its sensor is ignored. The left slider is the distance from "
                           "the window's edge to the hole's centre, the right one its diameter. Nearer the window or larger: the notes it vents "
                           "are higher. Tune holes sets these.");
        addLength ({}, geometry.holes[(size_t) i].diameter, 2.0, 16.0, 0.1, RowKind::holeDiameter);

        auto toggle = std::make_unique<juce::ToggleButton>();
        toggle->setWantsKeyboardFocus (false);
        toggle->setTooltip (juce::String ("Use the ") + kHoleNames[(size_t) i] + " hole (" + (i == 0 ? "left thumb, on the back" : "front")
                            + "); unticked, it is plugged and its sensor is ignored");
        auto* t = toggle.get();
        toggle->onClick = [this, i, t] { setHoleEnabled (i, t->getToggleState()); };
        settingsContent.addAndMakeVisible (*toggle);
        holeToggles[(size_t) i] = std::move (toggle);
    }
    refreshers.push_back ([this]
    {
        for (int i = 0; i < whistle::kNumHoles; ++i)
        {
            const bool on = geometry.holes[(size_t) i].enabled;
            holeToggles[(size_t) i]->setToggleState (on, juce::dontSendNotification);
            for (size_t r : { holeRow[(size_t) i], holeRow[(size_t) i] + 1 })
            {
                rows[r].slider->setEnabled (on);
                rows[r].label->setEnabled (on);
            }
        }
        chartBox.setSelectedId (geometry.chart == whistle::FingeringChart::baroqueRecorder ? 2 : 1, juce::dontSendNotification);

        bellButton.setToggleState (geometry.bellClosesFoot, juce::dontSendNotification);
    });

    addHeading ("Fipple");
    addLength ("Windway height (jet thickness)", geometry.windwayHeight, 0.4, 3.0, 0.01)
        .setTooltip ("The height of the narrow channel the air leaves, which is the thickness of the jet. With the other settings unchanged, a thinner jet changes register at a lower breath.");
    addLength ("Windway length", geometry.windwayLength, 5.0, 80.0, 0.5)
        .setTooltip ("The channel's length from the mouth end to the exit. It sets how much air the breath has to accelerate and how much friction it meets: longer responds a little slower and loses more when blown softly.");
    addLength ("Window length (flue to edge)", geometry.windowLength, 2.0, 14.0, 0.05)
        .setTooltip ("The distance the jet crosses from the windway's exit to the edge. Shorter: the jet grows less on the way (a purer tone) and the registers change at a lower breath.");
    addLength ("Window width", geometry.windowWidth, 3.0, 25.0)
        .setTooltip ("The width of the window and of the jet. A wider jet carries more air and drives the tube harder.");
    addLength ("Edge offset (into the pipe)", geometry.edgeOffset, -0.5, 0.5, 0.01)
        .setTooltip ("Where the edge sits relative to the jet's centre line; positive is into the pipe. At 0 the jet splits evenly and the even harmonics nearly vanish. More offset gives a stronger second harmonic, and too much stops some notes speaking.");

    addHeading ("Voicing (jet behaviour)");
    auto& model = audio.whistleModel;
    addRow ("Jet response (Strouhal f h/U)", 0.008, 0.05, 0.001, "", [&model] { return (double) model.jetStrouhal.load(); },
            [&model] (double v) { model.jetStrouhal = (float) v; }, false)
        .setTooltip ("The frequency the jet amplifies most, as a Strouhal number (frequency x windway height / jet speed). Higher: the registers change at a lower breath, so the upper notes need less air. Lower: they need more.");
    addRow ("Jet wave speed (x jet speed)", 0.3, 0.9, 0.01, "", [&model] { return (double) model.jetConvection.load(); },
            [&model] (double v) { model.jetConvection = (float) v; }, false)
        .setTooltip ("How fast a disturbance travels along the jet, as a share of the jet's speed. It sets the delay across the window, which decides which register sounds at a given breath. Lower: a longer delay, so changing register takes more breath.");
    addRow ("Jet gain", 0.5, 2.5, 0.01, "", [&model] { return (double) model.jetGain.load(); },
            [&model] (double v) { model.jetGain = (float) v; }, false)
        .setTooltip ("How much the jet amplifies the sound's push on its way across the window (1 = the textbook growth). Higher: "
                     "the jet swings further across the edge, so the tone gets fuller and squarer, and notes speak more softly.");
    addRow ("Slow-jet growth", 0.0, 1.0, 0.01, "", [&model] { return (double) model.slowJetGrowth.load(); },
            [&model] (double v) { model.slowJetGrowth = (float) v; }, false)
        .setTooltip ("A slowly blown jet amplifies more (more wavelengths fit across the window), a fast one less: soft notes come "
                     "out rich, like a real whistle's bottom D, and the hard-blown second octave purer. 0 = the same at any "
                     "speed; 0.67 = thin-jet theory.");
    // Up to 4: the Golden style uses 3.
    addRow ("Jet spreading", 0.0, 4.0, 0.01, "", [&model] { return (double) model.jetSpreading.load(); },
            [&model] (double v) { model.jetSpreading = (float) v; }, false)
        .setTooltip ("The jet widens on its way across the window, much more once it's fast enough to be turbulent (1 = a "
                     "turbulent plane jet, ~0.1 x the window length; 0 = constant width). A wider jet at the edge switches "
                     "less abruptly: hard-blown notes, the second octave, come out louder and purer.");
    addRow ("Jet bend", -20.0, 40.0, 0.5, "Pa", [&model] { return (double) model.jetBend.load(); },
            [&model] (double v) { model.jetBend = (float) v; }, false)
        .setTooltip ("A steady sideways push on the jet, as the pressure difference across it; positive bends it out of the "
                     "pipe, like moving the edge further in. A slow jet is bent much more than a fast one, so the low "
                     "register gets a stronger second harmonic and the second octave comes out purer, as on real whistles. "
                     "0 = none: the edge offset alone, the same at every breath.");
    addRow ("Jet band-pass stages", 1.0, 2.0, 1.0, "", [&model] { return (double) model.jetStages.load(); },
            [&model] (double v) { model.jetStages = juce::roundToInt (v); }, false)
        .setTooltip ("How sharply the jet prefers frequencies near its Strouhal number. 2 = sharper, but its extra phase shift "
                     "narrows each register and leaves silent pressures between them; 1 = no silent gaps, notes speak more softly.");
    addRow ("Breath noise", 0.0, 1.0, 0.01, "", [&model] { return (double) model.breathNoise.load(); },
            [&model] (double v) { model.breathNoise = (float) v; }, false)
        .setTooltip ("Turbulence where the jet hits the edge. It grows with jet speed and gets brighter as you blow harder, and it "
                     "goes into the bore, so the bore's resonances colour it (an airy note below the playing threshold).");
    addRow ("Breath noise heard directly", 0.0, 1.0, 0.01, "", [&model] { return (double) model.directNoise.load(); },
            [&model] (double v) { model.directNoise = (float) v; }, false)
        .setTooltip ("How much of the edge noise escapes straight out of the window, rather than only through the bore.");
    addRow ("Windway air mass", 0.0, 1.0, 0.01, "", [&model] { return (double) model.windwayMass.load(); },
            [&model] (double v) { model.windwayMass = (float) v; }, false)
        .setTooltip ("The breath has to accelerate the air in the windway, so the jet reaches its speed in about a millisecond "
                     "(rounder attacks). 1 = as for the windway's length, 0 = off: WARBL's mouthpiece already smooths the breath a little.");
    addRow ("Windway friction", 0.0, 1.0, 0.01, "", [&model] { return (double) model.windwayFriction.load(); },
            [&model] (double v) { model.windwayFriction = (float) v; }, false)
        .setTooltip ("Boundary layers on the windway walls carry less air than the jet's core, most of all when blowing softly "
                     "(a long, narrow windway loses more). 1 = as computed, 0 = off.");
    addRow ("Edge vortex loss", 0.0, 1.5, 0.01, "", [&model] { return (double) model.vortexLoss.load(); },
            [&model] (double v) { model.vortexLoss = (float) v; }, false)
        .setTooltip ("Energy lost in the vortices shed where the sound flows round the edge (1 = the textbook value). It is what "
                     "limits how loud the low notes get: lower gives fuller, louder low notes with more harmonics.");
    addRow ("Attack chiff", 0.0, 10.0, 0.1, "", [&model] { return (double) model.attackChiff.load(); },
            [&model] (double v) { model.attackChiff = (float) v; }, false)
        .setTooltip ("A burst of breath noise at a quick (tongued) attack: the jet has just started, is turbulent, and isn't yet "
                     "steered by the sound in the pipe. It fades as the note takes over; slow swells and held notes are unchanged. "
                     "A multiple of the breath noise; 0 = none.");
    addRow ("Finger gap damping", 0.0, 1.0, 0.01, "", [&model] { return (double) model.fingerGapDamping.load(); },
            [&model] (double v) { model.fingerGapDamping = (float) v; }, false)
        .setTooltip ("Air leaving a hole under a finger just above it has to squeeze through a thin gap, which barely lets it "
                     "through: 1 = that physical resistance (a just-lifted finger changes little at first), 0 = off (the hole "
                     "acts as soon as the finger lifts).");

    addHeading ("Finger impact (works without breath)");
    addRow ("Finger tap (air)", 0.0, 2.0, 0.01, "", [&model] { return (double) model.fingerTap.load(); },
            [&model] (double v) { model.fingerTap = (float) v; }, false)
        .setTooltip ("A finger landing on a hole pushes a short puff of air into the tube, which rings at whatever note the holes make: "
                     "tapping different fingers gives different pitches, even without blowing. 1 = the physical amount for the "
                     "finger's speed, 0 = off. The presets use 0.2, a faint touch under the playing.");
    addRow ("Finger softness", 0.5, 15.0, 0.1, "ms", [&model] { return (double) model.fingerSoftness.load(); },
            [&model] (double v) { model.fingerSoftness = (float) v; }, false)
        .setTooltip ("How gradually a finger's push of air builds up as it comes down (at an everyday tap; a faster finger takes less time). "
                     "Then it stops in a third of that as the fingertip meets the rim. Short: a sharper tick. Long: a soft, rounded thump.");
    addRow ("Body knock", 0.0, 2.0, 0.01, "", [&model] { return (double) model.bodyKnock.load(); },
            [&model] (double v) { model.bodyKnock = (float) v; }, false)
        .setTooltip ("The same impact shakes the instrument's body: the short knock you hear from the wood or metal itself, not from "
                     "the air inside. 0 = off.");
    addRow ("Body pitch", 300.0, 6000.0, 10.0, "Hz", [&model] { return (double) model.bodyPitch.load(); },
            [&model] (double v) { model.bodyPitch = (float) v; }, false)
        .setTooltip ("The lowest of the body's three resonances (the others are about 2.3 and 3.9 times higher). Wood is low and dull, thin "
                     "metal high and bright.");
    addRow ("Body ring", 3.0, 150.0, 1.0, "", [&model] { return (double) model.bodyRing.load(); },
            [&model] (double v) { model.bodyRing = (float) v; }, false)
        .setTooltip ("How long the body rings after the impact (a Q factor). Wood damps it almost at once (10-20), thin metal keeps ringing (50 or more).");

    addHeading ("Output (breath and hole response: Sensors tab)");
    addRow ("Breath scale", 0.2, 2.0, 0.01, "x", [&model] { return (double) model.breathScale.load(); },
            [&model] (double v) { model.breathScale = (float) v; }, false)
        .setTooltip ("How hard this instrument is blown, as a share of WARBL's breath response (Sensors tab) at full breath. "
                     "Only WARBL's breath is scaled, not Play scale or the screen.");
    addRow ("Breath curve", 0.5, 2.5, 0.01, "", [&model] { return (double) model.breathCurve.load(); },
            [&model] (double v) { model.breathCurve = (float) v; }, false)
        .setTooltip ("How the breath is spread over this instrument's pressures: 1 = as WARBL's response; more gives gentle breath "
                     "less pressure while full breath gives the same. A recorder's low notes need a third of a whistle's pressure "
                     "and its top notes as much: 1.35 puts its low C at about a whistle's low-D breath, and top C within reach.");
    addRow ("Output level", -30.0, 12.0, 0.5, "dB", [&model] { return (double) model.outputGainDb.load(); },
            [&model] (double v) { model.outputGainDb = (float) v; }, false)
        .setTooltip ("The instrument's overall volume. If the Monitor tab says it is limiting, turn this down.");

    // The rows don't all fit the window: they scroll (the sliders leave the wheel to the view).
    settingsContent.addAndMakeVisible (bellButton);
    settingsContent.addAndMakeVisible (bellHelp);
    settingsView.setViewedComponent (&settingsContent, false);
    settingsView.setScrollBarsShown (true, false);
    addAndMakeVisible (settingsView);

    // Right side
    addAndMakeVisible (whistleView);
    for (auto* b : { &enableButton, &manualButton })
    {
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    enableButton.setToggleState (audio.whistleEnabled.load(), juce::dontSendNotification);
    enableButton.onClick = [this] { audio.whistleEnabled = enableButton.getToggleState(); };
    manualButton.onClick = [this] { audio.manualPlay = manualButton.getToggleState(); };
    scaleButton.setWantsKeyboardFocus (false);
    scaleButton.setTooltip ("Plays this instrument through its own fingerings from the lowest note up two octaves, with the breath "
                            "pressure the model needs for each note (found by playing the model first, which takes a few seconds).");
    scaleButton.onClick = [this] { audio.playScale(); };
    addAndMakeVisible (scaleButton);

    manualBreath.setRange (0.0, 9000.0, 1.0);
    manualBreath.setSkewFactorFromMidPoint (1500.0);  // Fine control in the low register, room for the third and fourth.
    manualBreath.setTextValueSuffix (" Pa");
    manualBreath.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    manualBreath.setScrollWheelEnabled (false);
    manualBreath.setValue (0.0, juce::dontSendNotification);
    manualBreath.onValueChange = [this] { audio.manualPressure = (float) manualBreath.getValue(); };
    manualBreath.setTooltip ("Breath pressure when playing from the screen. A D whistle's low register starts around 400 Pa; "
                             "it jumps to the octave above ~1000 Pa, the twelfth around 4000 Pa and the double octave around 7000 Pa.");

    for (auto* l : { &manualBreathLabel, &liveLabel, &tuningSummary })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }
    addAndMakeVisible (manualBreath);
    tuningSummary.setText ("Tuning: Tuning workshop tab", juce::dontSendNotification);
    tuningSummary.setTooltip ("How in tune this instrument plays: the Tuning workshop tab has the fingering table, the tuner and the tuning goal.");

    loadPreset (0);
    startTimerHz (30);
}

InstrumentPage::~InstrumentPage()
{
    for (auto& row : rows)
        row.slider->setLookAndFeel (nullptr);
    audio.manualPlay = false;
}

juce::Slider& InstrumentPage::addRow (juce::String name, double min, double max, double step, juce::String unit,
                                      std::function<double()> get, std::function<void (double)> set, bool changesGeometry,
                                      RowKind kind)
{
    Row row;
    row.kind = kind;
    row.label = std::make_unique<juce::Label> (juce::String(), name);
    row.label->setColour (juce::Label::textColourId, text);
    row.label->setFont (juce::FontOptions (15.5f));
    row.slider = std::make_unique<juce::Slider>();
    row.slider->setScrollWheelEnabled (false);  // Only dragging or typing changes a value; the wheel scrolls the page
    row.slider->setLookAndFeel (&valueLook);
    row.slider->setRange (min, max, step);
    row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 22);
    if (unit.isNotEmpty())
        row.slider->setTextValueSuffix (" " + unit);
    row.slider->setValue (get(), juce::dontSendNotification);

    auto* slider = row.slider.get();
    slider->onValueChange = [this, slider, set, changesGeometry]
    {
        set (slider->getValue());
        if (changesGeometry)
            geometryChanged();
        else
            voicingChanged();
    };
    // Remember how to read the value back when the geometry changes as a whole (preset, material, tuning).
    refreshers.push_back ([slider, get] { slider->setValue (get(), juce::dontSendNotification); });

    settingsContent.addAndMakeVisible (*row.label);
    settingsContent.addAndMakeVisible (*row.slider);
    if (kind != RowKind::holeDiameter)
    {
        row.help = std::make_unique<HelpDot> (kind == RowKind::holePosition ? name + " hole" : name, [slider] { return slider->getTooltip(); });
        settingsContent.addAndMakeVisible (*row.help);
    }
    leftColumn.push_back (row.label.get());
    rows.push_back (std::move (row));
    return *slider;
}

void InstrumentPage::addHeading (juce::String name)
{
    auto label = std::make_unique<juce::Label> (juce::String(), name);
    label->setColour (juce::Label::textColourId, accent);
    label->setFont (juce::FontOptions (16.5f, juce::Font::bold));
    settingsContent.addAndMakeVisible (*label);
    leftColumn.push_back (label.get());
    headings.push_back (std::move (label));
}

void InstrumentPage::setHoleEnabled (int index, bool enabled)
{
    auto& hole = geometry.holes[(size_t) index];
    hole.enabled = enabled;
    if (enabled && (hole.diameter <= 0.0 || hole.position <= 0.0 || hole.position >= geometry.boreLength))
    {
        // A hole that was never used: give it a size and put it between its neighbours.
        hole.diameter = 0.006;
        double above = 0.0, below = geometry.boreLength;
        for (int i = 1; i < index; ++i)
            if (geometry.holes[(size_t) i].enabled)
                above = std::max (above, geometry.holes[(size_t) i].position);
        for (int i = index + 1; i < whistle::kNumHoles; ++i)
            if (geometry.holes[(size_t) i].enabled)
                below = std::min (below, geometry.holes[(size_t) i].position);
        hole.position = index == 0 ? std::max (0.02, geometry.holes[1].position - 0.015) : 0.5 * (above + below);
    }
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::showKeyOfInstrument()
{
    // The key the instrument is in: its lowest note (all holes covered).
    const auto r = whistle::resonances (geometry, whistle::Openness::allClosed(), 100.0, 2000.0);
    if (! r.empty())
    {
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (r.front() / geometry.referenceA));
        octaveBox.setSelectedId (midi >= 72 ? 1 : 2, juce::dontSendNotification);
        keyBox.setSelectedId ((midi % 12 + 12) % 12 + 1, juce::dontSendNotification);
    }
}

void InstrumentPage::refreshSliders()
{
    for (auto& refresh : refreshers)
        refresh();
}

void InstrumentPage::loadPreset (int index)
{
    if (index < 0 || index >= (int) whistle::presets().size())
        return;
    geometry = whistle::presets()[(size_t) index];

    audio.whistleModel.setVoicing (whistle::presetVoicing ((size_t) index));

    for (size_t i = 0; i < whistle::materials().size(); ++i)
        if (std::abs (whistle::materials()[i].wallThickness - geometry.wallThickness) < 1e-6)
            materialBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

    showKeyOfInstrument();
    tuneStatus.setText ({}, juce::dontSendNotification);
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::geometryChanged()
{
    audio.whistleModel.setGeometry (geometry);
    audio.setScaleInstrument (geometry, lowestNoteHz());
    whistleView.setGeometry (geometry);
    voicingChanged();
}

void InstrumentPage::voicingChanged()
{
    if (onInstrumentChanged != nullptr)
        onInstrumentChanged();
}

void InstrumentPage::applyGeometry (const whistle::Geometry& g)
{
    geometry = g;
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::setTuningSummary (const juce::String& summary)
{
    tuningSummary.setText (summary, juce::dontSendNotification);
}

void InstrumentPage::timerCallback()
{
    const float pressure = audio.shownPressure.load();
    const float jet = audio.whistleModel.jetVelocity.load();
    const float level = audio.whistleModel.level.load();
    liveLabel.setText ("Breath " + juce::String (juce::roundToInt (pressure)) + " Pa   jet " + juce::String (jet, 1) + " m/s   level "
                           + juce::String (juce::roundToInt (juce::Decibels::gainToDecibels (level, -90.0f))) + " dBFS   "
                           + audio.getScaleStatus(),
                       juce::dontSendNotification);
    scaleButton.setButtonText (audio.isScaleActive() ? "Stop scale" : "Play scale");
    whistleView.repaint();
}

whistle::Instrument InstrumentPage::getInstrument() const
{
    whistle::Instrument instrument;
    instrument.geometry = geometry;
    instrument.voicing = audio.whistleModel.getVoicing();
    return instrument;
}

void InstrumentPage::setInstrument (const whistle::Instrument& instrument)
{
    geometry = instrument.geometry;
    audio.whistleModel.setVoicing (instrument.voicing);

    selectPresetNamed (geometry.name);
    materialBox.setSelectedId (0, juce::dontSendNotification);
    for (size_t i = 0; i < whistle::materials().size(); ++i)
        if (std::abs (whistle::materials()[i].wallThickness - geometry.wallThickness) < 1e-6
            && std::abs (whistle::materials()[i].wallLoss - geometry.wallLoss) < 1e-6)
            materialBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

    showKeyOfInstrument();

    tuneStatus.setText ({}, juce::dontSendNotification);
    refreshSliders();
    geometryChanged();
}

juce::File InstrumentPage::userPresetFolder()
{
    // macOS: ~/Library/Application Support/WhistleSim/Presets; Linux: ~/.config/...; Windows: AppData\Roaming\...
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    folder = folder.getChildFile ("Application Support");
   #endif
    return folder.getChildFile ("WhistleSim").getChildFile ("Presets");
}

void InstrumentPage::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < whistle::presets().size(); ++i)
        presetBox.addItem (whistle::presets()[i].name, (int) i + 1);

    userPresets.clear();
    for (const auto& file : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.whistle"))
        userPresets.push_back (file);
    std::sort (userPresets.begin(), userPresets.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    if (! userPresets.empty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (size_t i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), kUserPresetId + (int) i);
    }
}

void InstrumentPage::selectPresetNamed (const juce::String& name)
{
    // A preset of the player's with this name, else a built-in one, else just show the name.
    for (size_t i = 0; i < userPresets.size(); ++i)
        if (userPresets[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (name))
        {
            presetBox.setSelectedId (kUserPresetId + (int) i, juce::dontSendNotification);
            deleteButton.setEnabled (true);
            return;
        }
    for (size_t i = 0; i < whistle::presets().size(); ++i)
        if (whistle::presets()[i].name == name)
        {
            presetBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
            deleteButton.setEnabled (false);
            return;
        }
    presetBox.setText (name, juce::dontSendNotification);
    deleteButton.setEnabled (false);
}

void InstrumentPage::loadUserPreset (int index)
{
    if (index < 0 || index >= (int) userPresets.size())
        return;
    const auto file = userPresets[(size_t) index];
    juce::String error;
    if (auto instrument = whistle::fromJson (file.loadFileAsString(), error))
    {
        instrument->geometry.name = file.getFileNameWithoutExtension();
        setInstrument (*instrument);
    }
    else
    {
        tuneStatus.setText (file.getFileName() + ": " + error, juce::dontSendNotification);
    }
}

void InstrumentPage::savePreset()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name for this whistle (an existing preset of yours with the same name is "
                                                         "replaced):", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", geometry.name);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
    juce::Component::SafePointer<InstrumentPage> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, safeWindow] (int result)
    {
        if (result == 1 && safeThis != nullptr && safeWindow != nullptr)
            safeThis->savePresetNamed (safeWindow->getTextEditorContents ("name"));
    }), true);
}

void InstrumentPage::savePresetNamed (const juce::String& requestedName)
{
    const auto name = juce::File::createLegalFileName (requestedName.trim());
    if (name.isEmpty())
        return;
    const auto folder = userPresetFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".whistle");
    geometry.name = name;
    if (file.replaceWithText (whistle::toJson (getInstrument())))
    {
        refreshPresetList();
        selectPresetNamed (name);
        tuneStatus.setText ("Saved preset " + name, juce::dontSendNotification);
    }
    else
    {
        tuneStatus.setText ("Couldn't write " + file.getFullPathName(), juce::dontSendNotification);
    }
}

juce::File InstrumentPage::selectedUserPreset() const
{
    const int index = presetBox.getSelectedId() - kUserPresetId;
    return index >= 0 && index < (int) userPresets.size() ? userPresets[(size_t) index] : juce::File();
}

void InstrumentPage::deletePreset()
{
    const auto file = selectedUserPreset();
    if (file == juce::File())
        return;
    juce::Component::SafePointer<InstrumentPage> safeThis (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Delete preset")
                                      .withMessage ("Delete your preset \"" + file.getFileNameWithoutExtension() + "\"? The next preset in the list "
                                                    "is loaded.")
                                      .withButton ("Delete", 1)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [safeThis, file] (int result)
    {
        if (result == 1 && safeThis != nullptr)
            safeThis->deleteUserPreset (file);
    });
}

void InstrumentPage::deleteUserPreset (const juce::File& file)
{
    if (file.moveToTrash() || file.deleteFile())
    {
        // Go on to the next preset of the player's (or the one before, if it was the last), or the first
        // built-in one if none are left.
        const auto deletedName = file.getFileNameWithoutExtension();
        const int position = (int) std::distance (userPresets.begin(), std::find (userPresets.begin(), userPresets.end(), file));
        refreshPresetList();
        if (! userPresets.empty())
            presetBox.setSelectedId (kUserPresetId + std::min (position, (int) userPresets.size() - 1), juce::sendNotificationSync);
        else
            presetBox.setSelectedId (1, juce::sendNotificationSync);
        tuneStatus.setText ("Deleted preset " + deletedName, juce::dontSendNotification);
    }
    else
    {
        tuneStatus.setText ("Couldn't delete " + file.getFullPathName(), juce::dontSendNotification);
    }
}

void InstrumentPage::saveToFile()
{
   #if JUCE_IOS
    // iOS saves by exporting a file that already exists: write it into the app's temporary folder, then let the
    // user choose where the copy goes.
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (juce::File::createLegalFileName (geometry.name) + ".whistle");
    if (! temp.replaceWithText (whistle::toJson (getInstrument())))
    {
        tuneStatus.setText ("Couldn't write " + temp.getFileName(), juce::dontSendNotification);
        return;
    }
    fileChooser = std::make_unique<juce::FileChooser> ("Save whistle", temp, "*.whistle");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode, [this, temp] (const juce::FileChooser& chooser)
    {
        temp.deleteFile();
        if (chooser.getURLResult().isEmpty())
            return;
        tuneStatus.setText ("Saved " + chosenFileName (chooser) + ".whistle", juce::dontSendNotification);
    });
   #else
    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Save whistle", folder.getChildFile (juce::File::createLegalFileName (geometry.name) + ".whistle"),
                                                      "*.whistle");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;
        file = file.withFileExtension ("whistle");

        geometry.name = file.getFileNameWithoutExtension();
        if (file.replaceWithText (whistle::toJson (getInstrument())))
        {
            presetBox.setText (geometry.name, juce::dontSendNotification);
            tuneStatus.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
        }
        else
        {
            tuneStatus.setText ("Couldn't write " + file.getFileName(), juce::dontSendNotification);
        }
    });
   #endif
}

void InstrumentPage::loadFromFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load whistle", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.whistle");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
    {
        const auto file = readableChosenFile (chooser, juce::File::getSpecialLocation (juce::File::tempDirectory));
        if (file == juce::File())
            return;

        juce::String error;
        const auto json = file.loadFileAsString();
       #if JUCE_IOS
        file.deleteFile();  // Only a copy (see readableChosenFile).
       #endif
        if (auto instrument = whistle::fromJson (json, error))
        {
            setInstrument (*instrument);
            tuneStatus.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
        }
        else
        {
            tuneStatus.setText (file.getFileName() + ": " + error, juce::dontSendNotification);
        }
    });
}

void InstrumentPage::showTuningControlsIn (juce::Component& parent)
{
    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &octaveBox, &referenceLabel, &referenceBox, &chartBox })
        parent.addAndMakeVisible (c);
}

void InstrumentPage::layOutTuningControls (juce::Rectangle<int> row, juce::Rectangle<int> chartRow)
{
    keyBox.setBounds (row.removeFromLeft (70).reduced (2));
    octaveBox.setBounds (row.removeFromLeft (120).reduced (2));
    referenceLabel.setBounds (row.removeFromLeft (34));
    referenceBox.setBounds (row.removeFromLeft (122).reduced (2));
    chartBox.setBounds (chartRow.reduced (2));
}

double InstrumentPage::lowestNoteHz() const
{
    // Lowest note in octave 5 (high whistles: C5..B5) or 4 (low whistles).
    const int midi = (octaveBox.getSelectedId() == 1 ? 72 : 60) + keyBox.getSelectedId() - 1;
    return geometry.referenceA * std::pow (2.0, (midi - 69) / 12.0);
}

void InstrumentPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void InstrumentPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    // Right: playing from the screen, the live readings and the whistle, upright.
    auto right = area.removeFromRight (juce::jlimit (280, 380, area.getWidth() / 4));
    area.removeFromRight (10);
    auto playRow = right.removeFromTop (28);
    enableButton.setBounds (playRow.removeFromLeft (110));
    scaleButton.setBounds (playRow.removeFromLeft (110).reduced (2));
    manualButton.setBounds (right.removeFromTop (26));
    auto breathRow = right.removeFromTop (26);
    manualBreathLabel.setBounds (breathRow.removeFromLeft (55));
    manualBreath.setBounds (breathRow);
    liveLabel.setBounds (right.removeFromTop (40));
    tuningSummary.setBounds (right.removeFromTop (40));
    right.removeFromTop (4);
    whistleView.setBounds (right);

    // Left: instrument choice, then the settings.
    auto choose = area.removeFromTop (28);
    presetBox.setBounds (choose.removeFromLeft (300).reduced (2));
    savePresetButton.setBounds (choose.removeFromLeft (120).reduced (2));
    deleteButton.setBounds (choose.removeFromLeft (70).reduced (2));
    exportButton.setBounds (choose.removeFromLeft (80).reduced (2));
    importButton.setBounds (choose.removeFromLeft (80).reduced (2));
    auto tuneRow = area.removeFromTop (28);
    materialBox.setBounds (tuneRow.removeFromLeft (220).reduced (2));
    tuneStatus.setBounds (tuneRow);
    area.removeFromTop (6);

    // The settings, in two columns when there is room (Bore, Tone holes and Fipple; then Voicing onwards), else one;
    // inside the scrolling view, as tall as they need to be.
    settingsView.setBounds (area);
    const int width = settingsView.getWidth() - settingsView.getScrollBarThickness() - 2;
    const int columns = width >= 1000 ? 2 : 1;
    const int columnWidth = columns == 2 ? (width - 30) / 2 : width;
    size_t split = leftColumn.size();
    if (columns == 2)
        for (size_t i = 0; i < leftColumn.size(); ++i)
            for (const auto& h : headings)
                if (h.get() == leftColumn[i] && h->getText().startsWith ("Voicing"))
                    split = std::min (split, i);

    // Rows as tall as the space allows (24-38 px), the text in proportion: big screens get big text.
    auto linesIn = [this] (size_t from, size_t to)
    {
        int lines = 0;
        for (size_t i = from; i < to; ++i)
        {
            const auto row = std::find_if (rows.begin(), rows.end(), [&] (const auto& r) { return r.label.get() == leftColumn[i]; });
            lines += row != rows.end() && row->kind == RowKind::holeDiameter ? 0 : 1;
        }
        return lines + 2;  // The bell line, and the gaps before headings
    };
    const int mostLines = std::max (linesIn (0, split), columns == 2 ? linesIn (split, leftColumn.size()) : 0);
    const int rowHeight = juce::jlimit (24, 38, area.getHeight() / std::max (1, mostLines));
    const float fontSize = rowHeight * 0.6f;
    for (auto& row : rows)
    {
        row.label->setFont (juce::FontOptions (fontSize));
        row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, juce::roundToInt (rowHeight * 3.4f), rowHeight - 4);
    }
    for (auto& h : headings)
        h->setFont (juce::FontOptions (fontSize * 1.08f, juce::Font::bold));
    bellButton.setSize (bellButton.getWidth(), rowHeight);

    size_t rowIndex = 0;
    int tallest = 0;
    for (int column = 0; column < columns; ++column)
    {
        auto col = juce::Rectangle<int> (column * (columnWidth + 30), 0, columnWidth, 100000);
        const size_t from = column == 0 ? 0 : split, to = column == 0 ? split : leftColumn.size();
        for (size_t i = from; i < to; ++i)
        {
            auto* c = leftColumn[i];
            const bool isHeading = std::any_of (headings.begin(), headings.end(), [c] (const auto& h) { return h.get() == c; });
            if (isHeading)
            {
                if (i != from)
                    col.removeFromTop (6);
                c->setBounds (col.removeFromTop (rowHeight));
                continue;
            }

            auto& row = rows[rowIndex];
            if (row.kind == RowKind::holeDiameter)
            {
                ++rowIndex;
                continue;  // Laid out together with its position row.
            }

            auto line = col.removeFromTop (rowHeight);
            row.help->setBounds (line.removeFromRight (24).withSizeKeepingCentre (20, 20));
            if (row.kind == RowKind::holePosition)
            {
                auto& diameter = rows[rowIndex + 1];
                for (int h = 0; h < whistle::kNumHoles; ++h)
                    if (holeRow[(size_t) h] == rowIndex)
                        holeToggles[(size_t) h]->setBounds (line.removeFromLeft (28));
                row.label->setBounds (line.removeFromLeft (36));
                row.slider->setBounds (line.removeFromLeft (line.getWidth() / 2).reduced (0, 1));
                diameter.label->setBounds ({});
                diameter.slider->setBounds (line.reduced (0, 1));

                // The bell isn't a hole (no position or size) but is switched on and off with them, after R4.
                if (rowIndex == holeRow[whistle::kNumHoles - 1])
                {
                    auto bellLine = col.removeFromTop (rowHeight);
                    bellHelp.setBounds (bellLine.removeFromRight (24).withSizeKeepingCentre (20, 20));
                    bellButton.setBounds (bellLine);
                }
            }
            else
            {
                row.label->setBounds (line.removeFromLeft (juce::jmin (280, columnWidth / 2)));
                row.slider->setBounds (line.reduced (0, 1));
            }
            ++rowIndex;
        }
        tallest = std::max (tallest, 100000 - col.getHeight());
    }
    settingsContent.setSize (width, tallest + 4);
}

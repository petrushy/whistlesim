#include "WorkshopPage.h"

#include <map>

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour highlight { 0xffe0a24f };

const char* const kPitchNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

int midiOf (double hz, double referenceA) { return (int) std::lround (69.0 + 12.0 * std::log2 (hz / referenceA)); }

juce::String noteName (double hz, double referenceA)
{
    const int midi = midiOf (hz, referenceA);
    return juce::String (kPitchNames[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
}

juce::String signedCents (double cents)
{
    const int c = (int) std::lround (cents);
    return (c >= 0 ? "+" : "") + juce::String (c) + " c";
}

juce::String mm (double metres) { return juce::String (std::abs (metres) < 0.00005 ? 0.0 : metres * 1000.0, 1) + " mm"; }

// Columns: Use, Note, Fingering, Target, Weight, Breath, Tube, Played (from the target and from equal; with the slide as it
// is), and Slide tuned. The header has two lines: the names, then what the cents are from.
constexpr int kColumns[] = { 36, 52, 92, 70, 52, 64, 66, 76, 84 };
constexpr int kHeaderHeight = 34;
} // namespace

//==============================================================================
int TuningTable::rowHeight() const
{
    // Rows shrink if the chart is long, so every note shows (a recorder's chart has 22 notes).
    return entries.empty() ? 19 : juce::jlimit (13, 19, (getHeight() - kHeaderHeight - 12) / (int) entries.size());
}

void TuningTable::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (10, 6);
    g.setFont (13.0f);

    auto header = area.removeFromTop (kHeaderHeight);
    auto units = header.removeFromBottom (14);
    const juce::String headers[] = { "Use", "Note", "Fingering", "Target", "Weight", "Breath", "Tube", "Played", "Played", "Slide tuned" };
    const juce::String from[] = { {}, {}, {}, "from equal", {}, {}, "from target", "from target", "from equal", "from target" };
    for (int i = 0; i < 10; ++i)
    {
        const int w = i < 9 ? kColumns[i] : header.getWidth();
        g.setColour (text.withAlpha (0.75f));
        g.setFont (13.0f);
        g.drawText (headers[i], header.removeFromLeft (w), juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.5f));
        g.setFont (11.0f);
        g.drawText (from[i], units.removeFromLeft (w), juce::Justification::centredLeft);
    }
    g.setFont (13.0f);

    const int height = rowHeight();
    for (const auto& e : entries)
    {
        auto line = area.removeFromTop (height);
        // Filled, open and half circles; plugged or unused holes left out.
        juce::String pattern;
        for (auto ch : e.pattern)
        {
            if (ch == 'x')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f"));
            else if (ch == 'o')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b"));
            else if (ch == 'h' || ch == 'p')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x90"));
            else if (ch == '|')
                pattern += " ";
        }

        // The Use box: a fingering left out of the tuning is shown dimmed and never highlighted.
        auto box = line.removeFromLeft (kColumns[0]).withSizeKeepingCentre (12, 12).toFloat();
        g.setColour (text.withAlpha (0.8f));
        g.drawRoundedRectangle (box, 2.0f, 1.0f);
        if (e.counts)
            g.fillRoundedRectangle (box.reduced (3.0f), 1.0f);

        const auto rowText = e.counts ? text : text.withAlpha (0.45f);
        g.setColour (rowText);
        g.drawText (e.note, line.removeFromLeft (kColumns[1]), juce::Justification::centredLeft);
        g.drawText (pattern, line.removeFromLeft (kColumns[2]), juce::Justification::centredLeft);
        g.drawText (std::abs (e.target) < 0.5 ? juce::String ("0") : signedCents (e.target), line.removeFromLeft (kColumns[3]),
                    juce::Justification::centredLeft);
        g.drawText (e.counts ? juce::String (e.weight, 2) : juce::String ("-"), line.removeFromLeft (kColumns[4]), juce::Justification::centredLeft);
        // The breath it's tuned at; a learned one in the accent colour, an estimated one (from the notes learned
        // around it) paler.
        g.setColour (e.learnedBreath ? accent : e.estimatedBreath ? accent.withAlpha (0.55f) : rowText);
        g.drawText (e.breath > 0.0 ? juce::String (juce::roundToInt (e.breath)) + " Pa" : juce::String(), line.removeFromLeft (kColumns[5]),
                    juce::Justification::centredLeft);
        // The tube's resonance, dimmer: what the holes set, not what is heard.
        g.setColour (text.withAlpha (0.55f));
        g.drawText (signedCents (e.cents), line.removeFromLeft (kColumns[6]), juce::Justification::centredLeft);
        const double values[] = { e.played, e.fromEqual, e.slideTuned };
        for (int v = 0; v < 3; ++v)
        {
            const double value = values[v];
            auto cell = line.removeFromLeft (v < 2 ? kColumns[7 + v] : line.getWidth());
            if (! std::isfinite (value))
            {
                g.setColour (text.withAlpha (0.55f));
                g.drawText (e.measuring ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) : juce::String ("doesn't sound"), cell,
                            juce::Justification::centredLeft);
                continue;
            }
            // Highlighted: more than 10 c from the target (not for the from-equal column, which says where it is).
            const bool fromTarget = v != 1;
            g.setColour (fromTarget && e.counts && std::abs (value) > 10.0 ? highlight : rowText);
            g.drawText (signedCents (value), cell, juce::Justification::centredLeft);
        }
    }
}

void TuningTable::mouseDown (const juce::MouseEvent& e)
{
    const int x = e.x - 10, y = e.y - 6 - kHeaderHeight;
    if (x < 0 || x >= kColumns[0] || y < 0)
        return;
    const auto row = (size_t) (y / rowHeight());
    if (row < entries.size() && onToggle != nullptr)
        onToggle (row);
}

//==============================================================================
WorkshopPage::WorkshopPage (AudioEngine& a, InstrumentPage& i) : audio (a), instrument (i)
{
    for (auto* l : { &title, &goalHeading, &keysHeading, &tunerHeading, &breathHeading, &chordsHeading })
    {
        l->setColour (juce::Label::textColourId, l == &title ? text : accent);
        l->setFont (juce::FontOptions (l == &title ? 15.0f : 14.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }
    goalHeading.setText ("Target pitches", juce::dontSendNotification);
    keysHeading.setText ("Keys you play (how much each counts)", juce::dontSendNotification);
    tunerHeading.setText ("Tuner", juce::dontSendNotification);
    breathHeading.setText ("Breath the notes are tuned at", juce::dontSendNotification);
    chordsHeading.setText ("Chords as played: third, fifth from pure (5:4, 6:5, 3:2)", juce::dontSendNotification);
    instrument.showTuningControlsIn (*this);
    for (auto* l : { &chords, &status, &tableTitle, &towardEqualLabel, &learnStatus, &lowPositionLabel, &highPositionLabel })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }
    chords.setJustificationType (juce::Justification::topLeft);
    status.setJustificationType (juce::Justification::topLeft);

    // Target pitches: the temperament, and how far back toward equal.
    temperamentBox.addItem ("Equal temperament", 1);
    temperamentBox.addItem ("Quarter-comma meantone", 2);
    temperamentBox.addItem ("Just intonation (major)", 3);
    temperamentBox.addItem ("Best for the keys below", 4);
    temperamentBox.addSeparator();
    temperamentBox.addItem (juce::String::fromUTF8 ("Härjedalspipa: II -20, III +35, VI -20 (Swedish Wikipedia)"), 5);
    temperamentBox.addItem (juce::String::fromUTF8 ("Härjedalspipa: III, VI, VII -22 (English Wikipedia)"), 6);
    temperamentBox.addSeparator();
    temperamentBox.addItem ("Custom (cents by degree, below)", 7);
    temperamentBox.setTooltip ("What the tuner aims each note at. Equal: every semitone 100 c. Quarter-comma meantone: pure major "
                               "thirds, fifths 5 c narrow, on the lowest note. Just: the major scale's pure intervals (its "
                               "minor chord on the second degree is out by 22 c). Best for the keys: the chords of the keys "
                               "below as pure as can be (each key's tonic chord, and half as much its fourth and fifth). Härjedalspipa: its "
                               "traditional scale, as two sources give it (no primary measurement was found). Custom: your own "
                               "cents from equal for each degree of the scale, in the boxes below (starting from the tuning chosen before).");
    temperamentBox.onChange = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        const auto chosen = (whistle::Temperament) (temperamentBox.getSelectedId() - 1);
        if (chosen == whistle::Temperament::custom && goal.temperament != whistle::Temperament::custom)
        {
            // Start from the tuning chosen before, as it stands (toward equal included).
            goal.custom = whistle::temperamentCents (goal);
            goal.towardEqual = 0.0;
        }
        goal.temperament = chosen;
        setGoal (goal);
    };
    addAndMakeVisible (temperamentBox);

    // Custom: one editable box per degree, cents from equal.
    for (size_t d = 0; d < 12; ++d)
    {
        auto& cell = customCells[d];
        cell.setEditable (true);
        cell.setJustificationType (juce::Justification::centred);
        cell.setColour (juce::Label::textColourId, text);
        cell.setColour (juce::Label::outlineColourId, text.withAlpha (0.3f));
        cell.setTooltip ("Cents from equal temperament for this degree of the scale, in every octave (click to type)");
        cell.onTextChange = [this, d]
        {
            auto goal = instrument.getGeometry().tuning;
            goal.custom[d] = juce::jlimit (-100.0, 100.0, customCells[d].getText().retainCharacters ("-+.0123456789").getDoubleValue());
            setGoal (goal);
        };
        customNames[d].setJustificationType (juce::Justification::centred);
        customNames[d].setColour (juce::Label::textColourId, text.withAlpha (0.7f));
        customNames[d].setFont (juce::FontOptions (12.0f));
        addChildComponent (cell);
        addChildComponent (customNames[d]);
    }

    towardEqual.setRange (0.0, 100.0, 1.0);
    towardEqual.setTextValueSuffix (" %");
    towardEqual.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    towardEqual.setScrollWheelEnabled (false);
    towardEqual.setTooltip ("Pulls the temperament back toward equal: 0 % as chosen, 100 % equal. Halfway keeps melodies close to "
                            "equal temperament while the chords get a little purer.");
    towardEqual.onValueChange = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.towardEqual = towardEqual.getValue() / 100.0;
        setGoal (goal);
    };
    addAndMakeVisible (towardEqual);

    // The keys: how much each counts (its scale's notes in the tuner, its chords in Best for the keys).
    for (size_t k = 0; k < keyRows.size(); ++k)
    {
        auto& row = keyRows[k];
        row.name.setColour (juce::Label::textColourId, text);
        row.weight.setRange (0.0, 1.0, 0.05);
        row.weight.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        row.weight.setScrollWheelEnabled (false);
        row.weight.setTooltip ("How much this key counts: its scale's notes count more in the tuner (from 0.3 for a note in no key "
                               "that counts to 1 for one in all), and with Best for the keys its chords set the target pitches. 0 = not at all.");
        row.weight.onValueChange = [this, k]
        {
            auto goal = instrument.getGeometry().tuning;
            goal.keys[(size_t) kKeys[k]] = keyRows[k].weight.getValue();
            setGoal (goal);
        };
        addAndMakeVisible (row.name);
        addAndMakeVisible (row.weight);
    }

    // The tuner.
    tuneButton.setTooltip ("Places the holes and sets the bore length so the notes play in tune (at the target pitches, for the key "
                           "and reference A on the Instrument tab): it plays every note through the model, each at the middle "
                           "of the breath it sounds at, aims the resonances to make up for how far the jet pulls each note, "
                           "and repeats. Takes a few seconds.");
    tuneButton.onClick = [this] { tuneHoles(); };
    bestSlideButton.setTooltip ("Moves the tuning slide (Instrument tab, Bore) to where the table's Slide tuned column has it");
    bestSlideButton.onClick = [this]
    {
        auto g = instrument.getGeometry();
        whistle::setSlide (g, bestSlide);
        instrument.applyGeometry (g);
    };
    crossButton.setToggleState (true, juce::dontSendNotification);
    crossButton.setTooltip ("Lets the tuner set the sizes the chart marks: on a whistle L1's (below: for C natural or high D), with C# "
                            "still set by L1's position; on a recorder the thumb hole's, L3's, R1's and R3's (the forks and upper notes).");
    topHoleBox.addItem ("L1's size tunes C natural (oxxooo)", 1);
    topHoleBox.addItem ("L1's size tunes high D (oxxxxx); half-hole C natural", 2);
    topHoleBox.setTooltip ("On a whistle L1's size moves the cross-fingered C natural a lot (a smaller hole flattens it) and high D "
                           "with L1 open a little (a larger hole flattens it). High D: C natural then counts only a tenth and comes "
                           "out sharp, as on many whistles, where it is half-holed.");
    topHoleBox.onChange = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.topHoleForHighD = topHoleBox.getSelectedId() == 2;
        setGoal (goal);
    };
    addAndMakeVisible (topHoleBox);
    headButton.setToggleState (true, juce::dontSendNotification);
    headButton.setTooltip ("The holes can't move a fingering's second octave against its first: the bore near the window does. "
                           "This lets the tuner widen or narrow the head section (Instrument tab, Bore) so both octaves play in "
                           "tune; an instrument without one gets a 30 mm head section. Off: holes and bore length only.");
    for (auto* b : { &crossButton, &headButton })
        b->setColour (juce::ToggleButton::textColourId, text);
    for (auto* c : std::initializer_list<juce::Component*> { &tuneButton, &bestSlideButton, &crossButton, &headButton, &table })
        addAndMakeVisible (c);

    // The breath: learned from the player, or where in each note's range.
    learnButton.setClickingTogglesState (true);
    learnButton.setTooltip ("Play on WARBL while this is on: each note you hold steady is heard, with the breath you give it (as "
                            "the model gets it, through the Sensors tab's breath response). Stop keeps each fingering's median "
                            "breath and tunes at it. Play every note of both octaves, at the breath you'd use in a tune.");
    learnButton.onClick = [this]
    {
        if (learnButton.getToggleState())
        {
            learning = true;
            breathSamples.clear();
            lastFingering.clear();
            learnButton.setButtonText ("Stop learning");
            learnStatus.setText ("Listening: play each note, holding it steady for a moment", juce::dontSendNotification);
        }
        else
        {
            stopLearning();
        }
    };
    forgetButton.setTooltip ("Forgets the learned breath: the notes are tuned where in their range the sliders below say");
    forgetButton.onClick = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.learnedBreath.clear();
        goal.useLearnedBreath = false;
        setGoal (goal);
    };
    useLearnedButton.setTooltip ("Tune each note at the breath you were heard playing it with (Learn my breath, blue in the Breath "
                                 "column); notes not heard get theirs estimated from the notes heard around them, by pitch (paler "
                                 "blue). With fewer than two notes heard, or off: every note uses the sliders below.");
    useLearnedButton.onClick = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.useLearnedBreath = useLearnedButton.getToggleState();
        setGoal (goal);
    };
    for (auto* slider : { &lowPosition, &highPosition })
    {
        slider->setRange (0.0, 100.0, 1.0);
        slider->setTextValueSuffix (" %");
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        slider->setScrollWheelEnabled (false);
        slider->setTooltip ("Where in the pressures each note sounds at it is tuned: 0 % its softest breath, 100 % its strongest, 50 % "
                            "the middle (a real whistle is blown about there). Lower: the notes play in tune at a softer breath "
                            "(and go sharp blown harder). For notes without a learned breath.");
        slider->onValueChange = [this, slider]
        {
            auto goal = instrument.getGeometry().tuning;
            goal.breathPosition[slider == &highPosition ? 1 : 0] = slider->getValue() / 100.0;
            setGoal (goal);
        };
    }
    for (auto* c : std::initializer_list<juce::Component*> { &learnButton, &forgetButton, &useLearnedButton, &lowPosition, &highPosition })
        addAndMakeVisible (c);
    useLearnedButton.setColour (juce::ToggleButton::textColourId, text);

    table.onToggle = [this] (size_t row)
    {
        auto g = instrument.getGeometry();
        const auto& c = whistle::chartFor (g.chart)[row];
        const bool now = ! whistle::fingeringCounts (g, c);
        if (now == c.counts)
            g.tuning.fingerings.erase (whistle::fingeringId (c));  // Back to the chart's default
        else
            g.tuning.fingerings[whistle::fingeringId (c)] = now;
        instrument.applyGeometry (g);
    };
    tableTitle.setTooltip ("Target: the pitch the tuner aims at, from equal temperament. Weight: how much the tuner counts the note. "
                           "Tube: the tube's resonance, from the target. Played: where the model plays the note, blown at the "
                           "middle of the breath it sounds at (where a player blows a real whistle), with the slide where it is. "
                           "Slide tuned: the same with the slide where the played notes are in tune on the whole, as a player "
                           "tunes to the others' A. Click a Use box to take a fingering in or out of the tuning.");

    instrument.onInstrumentChanged = [this] { instrumentChanged(); };
    instrumentChanged();
    startTimerHz (30);
}

WorkshopPage::~WorkshopPage()
{
    instrument.onInstrumentChanged = nullptr;
}

void WorkshopPage::setLearning (bool on)
{
    if (on != learnButton.getToggleState())
        learnButton.setToggleState (on, juce::sendNotificationSync);
}

void WorkshopPage::setGoal (const whistle::TuningGoal& goal)
{
    auto g = instrument.getGeometry();
    g.tuning = goal;
    instrument.applyGeometry (g);
}

juce::String WorkshopPage::keyName (int key) const
{
    const int lowest = midiOf (instrument.lowestNoteHz(), instrument.getGeometry().referenceA);
    const bool minor = key >= whistle::TuningGoal::kMinor;
    return juce::String (kPitchNames[((lowest + key % 12) % 12 + 12) % 12]) + (minor ? "m" : "");
}

void WorkshopPage::refreshGoalControls()
{
    const auto& g = instrument.getGeometry();
    temperamentBox.setSelectedId ((int) g.tuning.temperament + 1, juce::dontSendNotification);
    const bool custom = g.tuning.temperament == whistle::Temperament::custom;
    const int lowestMidi = midiOf (instrument.lowestNoteHz(), g.referenceA);
    for (size_t d = 0; d < 12; ++d)
    {
        customCells[d].setVisible (custom);
        customNames[d].setVisible (custom);
        customCells[d].setText (d == 0 ? juce::String ("0") : juce::String (g.tuning.custom[d], 1), juce::dontSendNotification);
        customCells[d].setEditable (d != 0);  // The lowest note is the reference: the slide sets the overall pitch
        customNames[d].setText (kPitchNames[((lowestMidi + (int) d) % 12 + 12) % 12], juce::dontSendNotification);
    }
    topHoleBox.setSelectedId (g.tuning.topHoleForHighD ? 2 : 1, juce::dontSendNotification);
    lowPosition.setValue (g.tuning.breathPosition[0] * 100.0, juce::dontSendNotification);
    highPosition.setValue (g.tuning.breathPosition[1] * 100.0, juce::dontSendNotification);
    useLearnedButton.setToggleState (g.tuning.useLearnedBreath, juce::dontSendNotification);
    useLearnedButton.setEnabled (! g.tuning.learnedBreath.empty());
    forgetButton.setEnabled (! g.tuning.learnedBreath.empty());
    if (! learning)
        learnStatus.setText (g.tuning.learnedBreath.empty() ? juce::String ("No breath learned yet")
                                                           : "Learned: " + juce::String ((int) g.tuning.learnedBreath.size()) + " fingerings",
                             juce::dontSendNotification);
    topHoleBox.setEnabled (g.chart == whistle::FingeringChart::whistle);
    towardEqual.setValue (g.tuning.towardEqual * 100.0, juce::dontSendNotification);
    towardEqual.setEnabled (g.tuning.temperament != whistle::Temperament::equal);
    for (size_t k = 0; k < keyRows.size(); ++k)
    {
        keyRows[k].name.setText (keyName (kKeys[k]) + (kKeys[k] >= whistle::TuningGoal::kMinor ? " (minor)" : " (major)"), juce::dontSendNotification);
        keyRows[k].weight.setValue (g.tuning.keys[(size_t) kKeys[k]], juce::dontSendNotification);
    }
    title.setText (g.name + ", in " + noteName (instrument.lowestNoteHz(), g.referenceA) + ", A = " + juce::String (g.referenceA, 1) + " Hz",
                   juce::dontSendNotification);
    resized();  // The custom boxes come and go
}

void WorkshopPage::instrumentChanged()
{
    playedValid = false;
    dirty = true;
    countdown = 9;  // Once the sliders have been still for a moment (0.3 s)
    refreshGoalControls();
}

void WorkshopPage::visibilityChanged()
{
    if (isShowing() && ! playedValid && ! tuning)
        countdown = 0;
}

void WorkshopPage::timerCallback()
{
    if (learning)
        sampleBreath();
    // Only while the tab is shown: the table is costly to build, and the measurement keeps every core busy for about a
    // second. While hidden it stays dirty, and visibilityChanged builds it when the tab is shown.
    if (! dirty || tuning || ! isShowing() || --countdown > 0)
        return;
    dirty = false;
    rebuildTable();
    measurePlayedInBackground();
}

void WorkshopPage::rebuildTable()
{
    const auto& geometry = instrument.getGeometry();
    const double lowest = instrument.lowestNoteHz();
    const auto& chart = whistle::chartFor (geometry.chart);

    // Each note's pull from its resonance, as last measured; the slide is tuned on the played notes.
    std::vector<double> pull (chart.size(), std::nan (""));
    if (playedValid)
        for (const auto& n : playedNotes)
            if (n.sounds && n.chartIndex < pull.size())
                pull[n.chartIndex] = n.cents - n.resonance;
    bestSlide = playedValid ? whistle::bestSlide (geometry, lowest, &pull) : whistle::bestSlide (geometry, lowest);
    auto atBest = geometry;
    whistle::setSlide (atBest, bestSlide);

    std::vector<TuningTable::Entry> entries;
    double playedSum = 0.0, slideSum = 0.0, weights = 0.0, largestWeight = 0.0;
    for (const auto& c : chart)
        largestWeight = std::max (largestWeight, whistle::tuningWeight (geometry, c));
    for (size_t i = 0; i < chart.size(); ++i)
    {
        const auto& c = chart[i];
        // The pattern for the holes in use, grouped as thumb | left hand | right hand.
        const auto holes = c.holePattern (geometry);
        juce::String pattern;
        for (int h = 0; h < whistle::kNumHoles; ++h)
        {
            if (! geometry.holes[(size_t) h].enabled)
                continue;
            if ((h == 1 && geometry.holes[0].enabled) || h == 4)
                pattern << '|';
            pattern << (holes[(size_t) h] == '-' ? 'o' : holes[(size_t) h]);
        }
        TuningTable::Entry e;
        e.note = noteName (lowest * std::pow (2.0, c.semitones / 12.0), geometry.referenceA);
        e.pattern = pattern;
        e.cents = whistle::predictedCents (geometry, c, lowest);
        e.target = whistle::targetCents (geometry, c);
        const double weight = whistle::tuningWeight (geometry, c);
        e.weight = largestWeight > 0.0 ? weight / largestWeight : 0.0;
        e.measuring = ! playedValid;
        e.counts = whistle::fingeringCounts (geometry, c);
        if (playedValid)
            for (const auto& n : playedNotes)
                if (n.chartIndex == i && n.sounds)
                {
                    e.breath = n.pressure;
                    e.learnedBreath = n.learned;
                    e.estimatedBreath = n.estimated;
                }
        if (std::isfinite (pull[i]))
        {
            e.played = e.cents + pull[i];
            e.fromEqual = e.target + e.played;
            e.slideTuned = whistle::predictedCents (atBest, c, lowest) + pull[i];
            playedSum += weight * e.played * e.played;
            slideSum += weight * e.slideTuned * e.slideTuned;
            weights += weight;
        }
        entries.push_back (e);
    }
    table.setEntries (std::move (entries));
    chords.setText (chordReport(), juce::dontSendNotification);

    if (! playedValid)
    {
        tableTitle.setText ("Measuring the played pitch...", juce::dontSendNotification);
        return;
    }
    const double change = bestSlide - geometry.slide;
    const juce::String move = std::abs (change) < 0.00005 ? juce::String ("where it is")
                                                          : (change > 0.0 ? "pull out " : "push in ") + mm (std::abs (change));
    const auto rms = [weights] (double sum) { return juce::String (weights > 0.0 ? std::sqrt (sum / weights) : 0.0, 1); };
    tableTitle.setText ("From the targets: played " + rms (playedSum) + " c rms (slide at " + mm (geometry.slide) + "). Slide tuned: " + move
                            + (std::abs (change) < 0.00005 || std::abs (geometry.slide) < 0.00005 ? juce::String() : " (to " + mm (bestSlide) + ")")
                            + ", " + rms (slideSum) + " c rms",
                        juce::dontSendNotification);
    instrument.setTuningSummary ("Tuning (Tuning workshop tab): played " + rms (playedSum) + " c rms, slide tuned " + rms (slideSum) + " c rms");
}

juce::String WorkshopPage::chordReport() const
{
    if (! playedValid)
        return "(once the played pitch is measured)";
    const auto& geometry = instrument.getGeometry();
    const auto& chart = whistle::chartFor (geometry.chart);

    // Each note's played pitch from equal temperament, by its semitones above the lowest note (the first fingering
    // for it that counts): a chord is built upward from its root in the low octave, so G major's fifth is the high D.
    std::map<int, double> played;
    for (const auto& n : playedNotes)
    {
        const auto& c = chart[n.chartIndex];
        const int semitones = (int) std::lround (c.semitones);
        if (n.sounds && whistle::fingeringCounts (geometry, c) && played.count (semitones) == 0)
            played[semitones] = whistle::targetCents (geometry, c) + n.cents;
    }

    juce::StringArray lines;
    for (int key = 0; key < 24; ++key)
    {
        if (geometry.tuning.keys[(size_t) key] <= 0.0)
            continue;
        const bool minor = key >= whistle::TuningGoal::kMinor;
        const int root = key % 12, third = root + (minor ? 3 : 4), fifth = root + 7;
        auto at = [&] (int semitones)
        {
            const auto found = played.find (semitones);
            return found != played.end() ? found->second : std::nan ("");
        };
        juce::String line = keyName (key).paddedRight (' ', 4) + "  ";
        if (! std::isfinite (at (root)) || ! std::isfinite (at (third)) || ! std::isfinite (at (fifth)))
        {
            lines.add (line + "(a note of its chord isn't on this instrument)");
            continue;
        }
        const double thirdError = (minor ? 300.0 : 400.0) + at (third) - at (root) - (minor ? 315.641 : 386.314);
        const double fifthError = 700.0 + at (fifth) - at (root) - 701.955;
        lines.add (line + "third " + signedCents (thirdError) + ", fifth " + signedCents (fifthError));
    }
    return lines.isEmpty() ? juce::String ("(no key counts)") : lines.joinIntoString ("\n");
}

void WorkshopPage::sampleBreath()
{
    // A note is heard when it sounds (the model's output above -54 dBFS), the holes the chart uses match a fingering
    // (each hole taken as open or covered, whichever it is nearer: a finger hovering over a lifted hole reads part
    // covered), and the breath stays within 10 % from one look to the next (30 a second): steady notes, not attacks
    // or changes. Which register it is comes from where the breath lies among the pressures the model plays each
    // of that fingering's notes at from rest (measured for the table): the nearest, up to 1.6 x beyond its ends,
    // since a note already sounding holds on past them.
    const float pressure = audio.shownPressure.load(), level = audio.whistleModel.level.load();
    const auto& g = instrument.getGeometry();
    const auto& chart = whistle::chartFor (g.chart);
    const double lowest = instrument.lowestNoteHz();
    auto nameOf = [&] (const whistle::ChartFingering& c) { return noteName (lowest * std::pow (2.0, c.semitones / 12.0), g.referenceA); };

    juce::String now;
    int best = -1;
    if (! playedValid)
        now = "waiting for the played pitch to be measured";
    else if (pressure < 20.0f || level < 0.002f)
        now = "no note";
    else
    {
        double bestDistance = std::log (1.6);
        bool fingeringFound = false;
        for (const auto& n : playedNotes)
        {
            const auto& c = chart[n.chartIndex];
            const auto holes = c.holePattern (g);
            bool matches = n.sounds;
            for (int h = 0; h < whistle::kNumHoles && matches; ++h)
            {
                if (! g.holes[(size_t) h].enabled || holes[(size_t) h] == '-')
                    continue;
                const float open = audio.shownOpenness[(size_t) h].load();
                const char want = holes[(size_t) h];
                matches = want == 'x' ? open < 0.5f : want == 'o' ? open >= 0.5f : std::abs (open - (want == 'h' ? 0.5f : 0.25f)) < 0.25f;
            }
            if (! matches)
                continue;
            fingeringFound = true;
            // How far the breath is outside the pressures this note sounds at from rest (0 inside).
            const double distance = pressure < n.lowPa ? std::log (n.lowPa / pressure) : pressure > n.highPa ? std::log (pressure / n.highPa) : 0.0;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = (int) n.chartIndex;
            }
        }
        now = best >= 0 ? nameOf (chart[(size_t) best]) + " at " + juce::String (juce::roundToInt (pressure)) + " Pa"
            : fingeringFound ? "breath " + juce::String (juce::roundToInt (pressure)) + " Pa is far from that fingering's notes"
                             : juce::String ("the holes match no fingering of the chart");
    }

    if (best >= 0)
    {
        const auto id = whistle::fingeringId (chart[(size_t) best]);
        if (id == lastFingering && std::abs (pressure / lastPressure - 1.0f) < 0.1f)
        {
            auto& samples = breathSamples[id];
            if (samples.size() < 5000)
                samples.push_back (pressure);
        }
        lastFingering = id;
        lastPressure = pressure;
    }
    else
    {
        lastFingering.clear();
    }

    // Which notes have been heard long enough (a third of a second), and what is heard now.
    juce::StringArray heard;
    for (const auto& c : chart)
        if (const auto found = breathSamples.find (whistle::fingeringId (c)); found != breathSamples.end() && found->second.size() >= 10)
            heard.addIfNotAlreadyThere (nameOf (c));
    learnStatus.setText ("Now: " + now + ". Heard: " + (heard.isEmpty() ? juce::String ("nothing yet") : heard.joinIntoString (" ")),
                         juce::dontSendNotification);
    learnStatus.setTooltip (learnStatus.getText());
}

void WorkshopPage::stopLearning()
{
    learning = false;
    learnButton.setToggleState (false, juce::dontSendNotification);
    learnButton.setButtonText ("Learn my breath");
    auto goal = instrument.getGeometry().tuning;
    int kept = 0;
    for (auto& [id, samples] : breathSamples)
    {
        if (samples.size() < 10)
            continue;  // Under a third of a second: not a held note
        std::nth_element (samples.begin(), samples.begin() + (long) samples.size() / 2, samples.end());
        goal.learnedBreath[id] = samples[samples.size() / 2];
        ++kept;
    }
    breathSamples.clear();
    if (kept == 0)
    {
        learnStatus.setText ("Nothing heard long enough: hold each note steady for a moment", juce::dontSendNotification);
        return;
    }
    goal.useLearnedBreath = true;
    setGoal (goal);
    learnStatus.setText ("Learned " + juce::String (kept) + " fingerings (" + juce::String ((int) goal.learnedBreath.size()) + " in all)",
                         juce::dontSendNotification);
}

void WorkshopPage::measurePlayedInBackground()
{
    const int generation = ++playedGeneration;
    worker.addJob ([this, generation, g = instrument.getGeometry(), voicing = audio.whistleModel.getVoicing(), lowest = instrument.lowestNoteHz(),
                    weakAlive = std::weak_ptr<bool> (alive)]
    {
        auto notes = whistle::measurePlayed (g, voicing, lowest);
        juce::MessageManager::callAsync ([this, generation, weakAlive, notes = std::move (notes)]
        {
            if (weakAlive.lock() == nullptr || generation != playedGeneration)
                return;  // Gone, or the instrument changed since
            playedNotes = notes;
            playedValid = true;
            rebuildTable();
        });
    });
}

void WorkshopPage::tuneHoles()
{
    if (tuning)
        return;
    tuning = true;
    tuneButton.setEnabled (false);
    status.setText ("Tuning to the played pitch (playing every note)...", juce::dontSendNotification);
    ++playedGeneration;  // A measurement still running is for the old holes
    const double hz = instrument.lowestNoteHz();
    worker.addJob ([this, hz, g = instrument.getGeometry(), voicing = audio.whistleModel.getVoicing(), cross = crossButton.getToggleState(),
                    head = headButton.getToggleState(), weakAlive = std::weak_ptr<bool> (alive)]
    {
        auto tuned = g;
        std::vector<whistle::PlayedNote> notes;
        const bool ok = whistle::tuneToPlayed (tuned, voicing, hz, cross, head, 3, &notes);
        juce::MessageManager::callAsync ([this, hz, before = g, tuned, notes = std::move (notes), ok, weakAlive]
        {
            if (weakAlive.lock() == nullptr)
                return;
            tuning = false;
            tuneButton.setEnabled (true);
            if (! ok)
            {
                status.setText ("Couldn't tune, even with other hole sizes: try another key or bore length", juce::dontSendNotification);
                instrumentChanged();
                return;
            }
            // Say what changed besides the positions: hole sizes the tuner had to change, and the head.
            juce::String changed;
            for (int i = 0; i < whistle::kNumHoles; ++i)
            {
                const double was = before.holes[(size_t) i].diameter, now = tuned.holes[(size_t) i].diameter;
                if (tuned.holes[(size_t) i].enabled && std::abs (now - was) > 0.00005)
                    changed << (changed.isEmpty() ? "; " : ", ") << whistle::kHoleNames[(size_t) i] << " " << juce::String (was * 1000.0, 1)
                            << " -> " << juce::String (now * 1000.0, 1) << " mm";
            }
            if (std::abs (tuned.headDiameter - before.headDiameter) > 0.00005 || tuned.headLength != before.headLength)
                changed << (changed.isEmpty() ? "; " : ", ") << "head " << juce::String (tuned.headDiameter * 1000.0, 1) << " mm over "
                        << juce::String (tuned.headLength * 1000.0, 0) << " mm";
            auto result = tuned;
            result.slide = 0.0;  // A newly made instrument, tuned with its slide pushed in
            status.setText ("Tuned for " + noteName (hz, result.referenceA) + ": played " + juce::String (whistle::playedRms (result, notes), 1)
                                + " c rms" + changed,
                            juce::dontSendNotification);
            status.setTooltip (status.getText());
            instrument.applyGeometry (result);
            // The tuner's last measurement is this geometry's: show it at once.
            dirty = false;
            playedNotes = notes;
            playedValid = true;
            ++playedGeneration;
            rebuildTable();
        });
    });
}

void WorkshopPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void WorkshopPage::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto left = area.removeFromLeft (470);
    area.removeFromLeft (10);

    title.setBounds (left.removeFromTop (24));
    auto pitchRow = left.removeFromTop (28);
    instrument.layOutTuningControls (pitchRow, left.removeFromTop (28).withWidth (260));
    left.removeFromTop (6);
    goalHeading.setBounds (left.removeFromTop (22));
    temperamentBox.setBounds (left.removeFromTop (28).reduced (2).withWidth (460));
    if (customCells[0].isVisible())
    {
        auto names = left.removeFromTop (16), cells = left.removeFromTop (24);
        const int cellWidth = left.getWidth() / 12;
        for (size_t d = 0; d < 12; ++d)
        {
            customNames[d].setBounds (names.removeFromLeft (cellWidth));
            customCells[d].setBounds (cells.removeFromLeft (cellWidth).reduced (1));
        }
    }
    auto equalRow = left.removeFromTop (26);
    towardEqualLabel.setBounds (equalRow.removeFromLeft (110));
    towardEqual.setBounds (equalRow.withWidth (300));
    left.removeFromTop (8);

    keysHeading.setBounds (left.removeFromTop (22));
    const int half = left.getWidth() / 2;
    for (int r = 0; r < 4; ++r)
    {
        auto line = left.removeFromTop (24);
        for (int column = 0; column < 2; ++column)
        {
            auto cell = line.removeFromLeft (half);
            auto& row = keyRows[(size_t) (column * 4 + r)];
            row.name.setBounds (cell.removeFromLeft (90));
            row.weight.setBounds (cell.reduced (2, 1).withTrimmedRight (8));
        }
    }
    left.removeFromTop (8);

    tunerHeading.setBounds (left.removeFromTop (22));
    auto buttons = left.removeFromTop (30);
    tuneButton.setBounds (buttons.removeFromLeft (120).reduced (2));
    bestSlideButton.setBounds (buttons.removeFromLeft (110).reduced (2));
    crossButton.setBounds (left.removeFromTop (24));
    topHoleBox.setBounds (left.removeFromTop (28).reduced (2).withTrimmedLeft (24).withWidth (380));
    headButton.setBounds (left.removeFromTop (24));
    status.setBounds (left.removeFromTop (40));
    left.removeFromTop (4);

    breathHeading.setBounds (left.removeFromTop (22));
    auto learnRow = left.removeFromTop (30);
    learnButton.setBounds (learnRow.removeFromLeft (140).reduced (2));
    forgetButton.setBounds (learnRow.removeFromLeft (80).reduced (2));
    useLearnedButton.setBounds (learnRow.reduced (4, 0));
    learnStatus.setBounds (left.removeFromTop (22));
    for (auto [label, slider] : { std::pair { &lowPositionLabel, &lowPosition }, std::pair { &highPositionLabel, &highPosition } })
    {
        auto line = left.removeFromTop (24);
        label->setBounds (line.removeFromLeft (110));
        slider->setBounds (line.withWidth (300));
    }
    left.removeFromTop (6);

    chordsHeading.setBounds (left.removeFromTop (22));
    chords.setBounds (left);

    tableTitle.setBounds (area.removeFromTop (24));
    table.setBounds (area);
}

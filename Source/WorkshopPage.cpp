#include "WorkshopPage.h"

#include "Player/Workshop.h"

#include <map>

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour highlight { 0xffe0a24f };

const char* const kPitchNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// The workshop's wording is shared with the iPhone app (Player/Workshop.h).
using workshop::midiOf;
juce::String noteName (double hz, double referenceA) { return juce::String (workshop::noteName (hz, referenceA)); }
juce::String signedCents (double cents) { return juce::String (workshop::signedCents (cents)); }

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
        g.setColour (e.counts && e.weightSet ? accent : rowText);  // The player's own weight in the accent colour
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

TuningTable::TuningTable()
{
    editor.setJustification (juce::Justification::centredLeft);
    editor.setInputRestrictions (6, "0123456789.");
    editor.setColour (juce::TextEditor::backgroundColourId, background);
    editor.setColour (juce::TextEditor::textColourId, accent);
    editor.setColour (juce::TextEditor::outlineColourId, accent);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    editor.onReturnKey = [this]
    {
        editor.setVisible (false);
        if (editingRow >= entries.size())
            return;
        const auto typed = editor.getText().trim();
        if (editingColumn == 5 && onBreathEdited != nullptr)
            onBreathEdited (editingRow, typed.getDoubleValue());
        if (editingColumn == 4 && onWeightEdited != nullptr)
            onWeightEdited (editingRow, typed.isEmpty() ? std::nan ("") : typed.getDoubleValue());
    };
    editor.onEscapeKey = editor.onFocusLost = [this] { editor.setVisible (false); };
    addChildComponent (editor);
}

juce::Rectangle<int> TuningTable::cellBounds (size_t row, int column) const
{
    int x = 10;
    for (int i = 0; i < column; ++i)
        x += kColumns[i];
    return { x, 6 + kHeaderHeight + (int) row * rowHeight(), kColumns[column], rowHeight() };
}

int TuningTable::rowAt (juce::Point<int> p, int column) const
{
    for (size_t row = 0; row < entries.size(); ++row)
        if (cellBounds (row, column).contains (p))
            return (int) row;
    return -1;
}

void TuningTable::edit (size_t row, int column)
{
    editingRow = row;
    editingColumn = column;
    const auto& e = entries[row];
    editor.setFont (juce::FontOptions (13.0f));
    editor.setBounds (cellBounds (row, column).withTrimmedRight (2).expanded (0, 1));
    editor.setText (column == 5 ? (e.breath > 0.0 ? juce::String (juce::roundToInt (e.breath)) : juce::String())
                                : juce::String (e.weight, 2),
                    false);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
}

void TuningTable::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.getPosition(), 0); row >= 0 && onToggle != nullptr)
        onToggle ((size_t) row);
    else if (const int weightRow = rowAt (e.getPosition(), 4); weightRow >= 0 && entries[(size_t) weightRow].counts)
        edit ((size_t) weightRow, 4);
    else if (const int breathRow = rowAt (e.getPosition(), 5); breathRow >= 0)
        edit ((size_t) breathRow, 5);
}

void TuningTable::mouseMove (const juce::MouseEvent& e)
{
    const bool overBreath = rowAt (e.getPosition(), 5) >= 0;
    const int weightRow = rowAt (e.getPosition(), 4);
    const bool overWeight = weightRow >= 0 && entries[(size_t) weightRow].counts;
    setMouseCursor (overBreath || overWeight || rowAt (e.getPosition(), 0) >= 0 ? juce::MouseCursor::PointingHandCursor
                                                                                : juce::MouseCursor::NormalCursor);
    setTooltip (overBreath   ? "Click to type the breath this note is tuned at (Pa); it then counts as learned. "
                               "Clear it (empty, Enter) to go back to the estimate."
                : overWeight ? "Click to type how much the tuner counts this note (0-1), in place of the weight the keys give. "
                               "Clear it (empty, Enter) to go back to the keys' weight."
                             : juce::String());
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
                           "and reference A above): it plays every note through the model, each at the breath set below (or "
                           "your learned breath), aims the resonances to make up for how far the jet pulls each note, and "
                           "repeats. Takes a few seconds.");
    tuneButton.onClick = [this] { tuneHoles(); };
    bestSlideButton.setTooltip ("Moves the tuning slide (Instrument tab, Bore) to where the table's Slide tuned column has it");
    bestSlideButton.onClick = [this]
    {
        auto g = instrument.getGeometry();
        whistle::setSlide (g, bestSlide);
        instrument.applyGeometry (g);
    };
    crossButton.setToggleState (true, juce::dontSendNotification);
    crossButton.onClick = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.tuneHoleSizes = crossButton.getToggleState();  // Saved with the instrument
        setGoal (goal);
    };
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
    headButton.onClick = [this]
    {
        auto goal = instrument.getGeometry().tuning;
        goal.shapeHead = headButton.getToggleState();
        setGoal (goal);
    };
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
            learner.start();
            learnButton.setButtonText ("Stop learning");
            learnStatus.setText (learner.status(), juce::dontSendNotification);
        }
        else
        {
            stopLearning();
        }
    };
    smoothButton.setTooltip ("Smooths the learned breath onto one curve of the notes' frequency (pressure ~ frequency^n, fitted "
                             "through the notes heard): a note played a little harder or softer than the rest is brought onto it. "
                             "Physics puts n near 2 (the jet's speed suits the note's frequency).");
    smoothButton.onClick = [this]
    {
        auto g = instrument.getGeometry();
        const auto fit = whistle::smoothLearnedBreath (g);
        if (! fit)
        {
            learnStatus.setText ("Smoothing needs the breath of three different notes or more", juce::dontSendNotification);
            return;
        }
        instrument.applyGeometry (g);
        learnStatus.setText ("Smoothed: breath ~ frequency^" + juce::String (fit->exponent, 2) + "; the notes were "
                                 + juce::String (fit->spreadPercent, 0) + " % from the curve",
                             juce::dontSendNotification);
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
        slider->setTooltip ("Where in the pressures each note sounds at it is tuned: 0 % where it speaks (within 15 dB of its "
                            "loudest), 100 % its strongest. The defaults, 35 % and 10 %, are where a player blew the real Golden "
                            "(its upper notes near the soft end). Lower: the notes play in tune at a softer breath (and go sharp "
                            "blown harder). For notes without a learned breath.");
        slider->onValueChange = [this, slider]
        {
            auto goal = instrument.getGeometry().tuning;
            goal.breathPosition[slider == &highPosition ? 1 : 0] = slider->getValue() / 100.0;
            setGoal (goal);
        };
    }
    for (auto* c : std::initializer_list<juce::Component*> { &learnButton, &forgetButton, &smoothButton, &useLearnedButton, &lowPosition, &highPosition })
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
    table.onWeightEdited = [this] (size_t row, double weight)
    {
        auto g = instrument.getGeometry();
        const auto id = whistle::fingeringId (whistle::chartFor (g.chart)[row]);
        if (std::isfinite (weight))
            g.tuning.weights[id] = juce::jlimit (0.0, 1.0, weight);
        else
            g.tuning.weights.erase (id);
        instrument.applyGeometry (g);
    };
    table.onBreathEdited = [this] (size_t row, double pa)
    {
        auto g = instrument.getGeometry();
        const auto id = whistle::fingeringId (whistle::chartFor (g.chart)[row]);
        if (pa > 0.0)
        {
            g.tuning.learnedBreath[id] = juce::jlimit (1.0, 20000.0, pa);
            g.tuning.useLearnedBreath = true;  // Else a typed breath would change nothing
        }
        else
        {
            g.tuning.learnedBreath.erase (id);
        }
        instrument.applyGeometry (g);
    };
    tableTitle.setTooltip ("Target: the pitch the tuner aims at, from equal temperament. Weight: how much the tuner counts the note. "
                           "Tube: the tube's resonance, from the target. Played: where the model plays the note, blown at the "
                           "middle of the breath it sounds at (where a player blows a real whistle), with the slide where it is. "
                           "Slide tuned: the same with the slide where the played notes are in tune on the whole, as a player "
                           "tunes to the others' A. Click a Use box to take a fingering in or out of the tuning.");

    tunerButton.setColour (juce::ToggleButton::textColourId, text);
    tunerButton.setTooltip ("An old-fashioned tuner on the sound the model plays (from WARBL, the screen or Play scale), "
                            "before the room: the note nearest the pitch, and the needle at its cents from equal temperament "
                            "at the reference A. The green zone is the tuning's target for that note (Target pitches above).");
    tunerButton.onClick = [this]
    {
        tuner.clear();
        tunerView.setActive (tunerButton.getToggleState());
    };
    addAndMakeVisible (tunerButton);
    addAndMakeVisible (tunerView);

    instrument.onChartHolesChanged = [this] (const juce::String& changed)
    {
        status.setText ("For this chart: " + changed + ". Press Tune holes to place them.", juce::dontSendNotification);
    };
    instrument.onInstrumentChanged = [this] { instrumentChanged(); };
    instrument.onNameChanged = [this] { refreshGoalControls(); };
    instrumentChanged();
    startTimerHz (30);
}

void WorkshopPage::listen()
{
    const bool tunerOn = tunerButton.getToggleState() && active();
    audio.tunerListening = tunerOn || learning;
    heardHz = 0.0;
    if (! tunerOn && ! learning)
        return;

    const auto reading = tuner.update (audio, instrument.getGeometry(), instrument.lowestNoteHz());
    heardHz = tuner.heardHz();
    if (! tunerOn)
        return;
    tunerView.setReading (reading.hz, reading.note, reading.centsFromEqual, reading.targetCents);
}

void TunerView::setActive (bool on)
{
    if (on == active)
        return;  // Callers may repeat it: only a change resets the needle
    active = on;
    silentTicks = 1000;
    repaint();
}

void TunerView::setReading (double newHz, const juce::String& newNote, double centsFromEqual, double targetCents)
{
    if (newHz > 0.0)
    {
        hz = newHz;
        note = newNote;
        cents = centsFromEqual;
        target = targetCents;
        silentTicks = 0;
    }
    else
    {
        ++silentTicks;
    }
    // Held a moment after the sound stops (as the note fades), then the needle sinks back to the middle.
    const double aim = silentTicks < 10 ? cents : 0.0;
    needle += (silentTicks < 10 ? 0.35 : 0.12) * (aim - needle);
    repaint();
}

void TunerView::paint (juce::Graphics& g)
{
    const juce::Colour face { 0xffeee4c8 }, ink { 0xff3b2f22 }, red { 0xffc0392b }, green { 0xff4f9a4a };
    auto box = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff5a4632));
    g.fillRoundedRectangle (box, 10.0f);
    box = box.reduced (6.0f);
    g.setColour (face.withMultipliedBrightness (active ? 1.0f : 0.55f));
    g.fillRoundedRectangle (box, 6.0f);

    // The scale: +-50 cents over +-50 degrees, around a pivot below the face's middle.
    const float radius = juce::jmin (box.getWidth() * 0.3f, box.getHeight() * 0.78f);
    const juce::Point<float> pivot { box.getCentreX(), box.getBottom() - box.getHeight() * 0.08f };
    auto angleFor = [] (double c) { return (float) juce::degreesToRadians (juce::jlimit (-52.0, 52.0, c)); };
    auto at = [&] (double c, float r) { return pivot.getPointOnCircumference (r, angleFor (c)); };

    const bool shown = active && silentTicks < 10;
    // The target's zone (+-3 c), then the ticks: every 5 c, longer every 10, numbered every 10.
    {
        juce::Path zone;
        zone.addCentredArc (pivot.x, pivot.y, radius * 0.93f, radius * 0.93f, 0.0f, angleFor (target - 3.0), angleFor (target + 3.0), true);
        g.setColour (green.withAlpha (active ? 0.9f : 0.4f));
        g.strokePath (zone, juce::PathStrokeType (radius * 0.08f));
    }
    g.setColour (ink.withAlpha (active ? 1.0f : 0.5f));
    g.setFont (juce::FontOptions (juce::jmax (9.0f, radius * 0.085f)));
    for (int c = -50; c <= 50; c += 5)
    {
        const bool major = c % 10 == 0;
        g.drawLine ({ at (c, radius * (major ? 0.80f : 0.85f)), at (c, radius) }, major ? 2.0f : 1.0f);
        if (major)
        {
            const auto p = at (c, radius * 0.70f);
            g.drawText (c == 0 ? "0" : (c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (40.0f, 16.0f).withCentre (p),
                        juce::Justification::centred);
        }
    }
    {
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (-50), angleFor (50), true);
        g.strokePath (arc, juce::PathStrokeType (1.5f));
    }
    g.setFont (juce::FontOptions (juce::jmax (9.0f, radius * 0.075f)));
    g.drawText ("cents", juce::Rectangle<float> (80.0f, 16.0f).withCentre (at (0, radius * 0.52f)), juce::Justification::centred);

    // The note and the numbers.
    auto top = box.reduced (12.0f, 8.0f);
    g.setFont (juce::FontOptions (juce::jmax (16.0f, box.getHeight() * 0.22f), juce::Font::bold));
    g.drawText (! active ? "off" : shown ? note : juce::String ("-"), top.removeFromLeft (top.getWidth() * 0.3f).removeFromTop (box.getHeight() * 0.3f),
                juce::Justification::topLeft);
    g.setFont (juce::FontOptions (juce::jmax (11.0f, box.getHeight() * 0.085f)));
    if (shown)
    {
        auto right = top.removeFromRight (top.getWidth() * 0.45f);
        g.drawText (juce::String (hz, 1) + " Hz", right.removeFromTop (box.getHeight() * 0.1f), juce::Justification::topRight);
        g.drawText (signedCents (cents) + " from equal", right.removeFromTop (box.getHeight() * 0.1f), juce::Justification::topRight);
        g.drawText (signedCents (cents - target) + " from target", right.removeFromTop (box.getHeight() * 0.1f), juce::Justification::topRight);
    }

    // The needle, and its pivot.
    if (active)
    {
        g.setColour (red);
        g.drawLine ({ pivot, at (needle, radius * 1.02f) }, 2.5f);
    }
    g.setColour (ink);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (pivot));
}

WorkshopPage::~WorkshopPage()
{
    audio.tunerListening = false;
    instrument.onInstrumentChanged = nullptr;
    instrument.onChartHolesChanged = nullptr;
    instrument.onNameChanged = nullptr;
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
    return juce::String (workshop::keyName (key, instrument.lowestNoteHz(), instrument.getGeometry().referenceA));
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
    crossButton.setToggleState (g.tuning.tuneHoleSizes, juce::dontSendNotification);
    headButton.setToggleState (g.tuning.shapeHead, juce::dontSendNotification);
    useLearnedButton.setEnabled (! g.tuning.learnedBreath.empty());
    forgetButton.setEnabled (! g.tuning.learnedBreath.empty());
    smoothButton.setEnabled (g.tuning.learnedBreath.size() >= 3);
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
    title.setText (juce::String (g.name) + ", in " + noteName (instrument.lowestNoteHz(), g.referenceA) + ", A = " + juce::String (g.referenceA, 1) + " Hz",
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
    if (active() && ! playedValid && ! tuning)
        countdown = 0;
}

void WorkshopPage::timerCallback()
{
    listen();
    if (learning)
        sampleBreath();
    // Only while the tab is shown: the table is costly to build, and the measurement keeps every core busy for about a
    // second. While hidden it stays dirty, and visibilityChanged builds it when the tab is shown.
    if (! dirty || tuning || ! active() || --countdown > 0)
        return;
    dirty = false;
    rebuildTable();
    measurePlayedInBackground();
}

void WorkshopPage::rebuildTable()
{
    const auto built = workshop::buildTable (instrument.getGeometry(), instrument.lowestNoteHz(), playedValid ? &playedNotes : nullptr);
    bestSlide = built.bestSlide;
    std::vector<TuningTable::Entry> entries;
    for (const auto& row : built.rows)
    {
        TuningTable::Entry e;
        e.note = row.note;
        e.pattern = row.pattern;
        e.cents = row.cents;
        e.fromEqual = row.fromEqual;
        e.played = row.played;
        e.slideTuned = row.slideTuned;
        e.target = row.target;
        e.weight = row.weight;
        e.breath = row.breath;
        e.weightSet = row.weightSet;
        e.learnedBreath = row.learnedBreath;
        e.estimatedBreath = row.estimatedBreath;
        e.measuring = row.measuring;
        e.counts = row.counts;
        entries.push_back (e);
    }
    table.setEntries (std::move (entries));
    chords.setText (chordReport(), juce::dontSendNotification);
    tableTitle.setText (built.title, juce::dontSendNotification);
    if (! built.summary.empty())
        instrument.setTuningSummary (built.summary);
}

juce::String WorkshopPage::chordReport() const
{
    juce::StringArray lines;
    for (const auto& line : workshop::chordReport (instrument.getGeometry(), instrument.lowestNoteHz(), playedValid ? &playedNotes : nullptr))
        lines.add (line);
    return lines.joinIntoString ("\n");
}

void WorkshopPage::sampleBreath()
{
    learner.sample (audio, instrument.getGeometry(), instrument.lowestNoteHz(), playedValid ? &playedNotes : nullptr, heardHz);
    learnStatus.setText (learner.status(), juce::dontSendNotification);
    learnStatus.setTooltip (learnStatus.getText());
}

void WorkshopPage::stopLearning()
{
    learning = false;
    learnButton.setToggleState (false, juce::dontSendNotification);
    learnButton.setButtonText ("Learn my breath");
    auto goal = instrument.getGeometry().tuning;
    if (learner.finish (goal) > 0)
        setGoal (goal);
    learnStatus.setText (learner.status(), juce::dontSendNotification);
}

void WorkshopPage::measurePlayedInBackground()
{
    const int generation = ++playedGeneration;
    worker.addJob ([this, generation, g = instrument.getGeometry(), voicing = audio.whistleModel.getVoicing(), lowest = instrument.lowestNoteHz(),
                    weakAlive = std::weak_ptr<bool> (alive)]
    {
        auto notes = whistle::measurePlayed (g, voicing, lowest);
        juce::MessageManager::callAsync ([this, generation, weakAlive, measured = std::move (notes)]
        {
            if (weakAlive.lock() == nullptr || generation != playedGeneration)
                return;  // Gone, or the instrument changed since
            playedNotes = measured;
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
        juce::MessageManager::callAsync ([this, hz, before = g, tuned, measured = std::move (notes), ok, weakAlive]
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
            auto result = tuned;
            result.slide = 0.0;  // A newly made instrument, tuned with its slide pushed in
            status.setText (workshop::tunedReport (before, result, hz, measured), juce::dontSendNotification);
            status.setTooltip (status.getText());
            instrument.applyGeometry (result);
            // The tuner's last measurement is this geometry's: show it at once.
            dirty = false;
            playedNotes = measured;
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
    smoothButton.setBounds (learnRow.removeFromLeft (80).reduced (2));
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
    auto tunerArea = area.removeFromBottom (juce::jlimit (150, 240, area.getHeight() / 3));
    tunerButton.setBounds (area.removeFromBottom (28).reduced (0, 2));
    tunerView.setBounds (tunerArea.withSizeKeepingCentre (juce::jmin (tunerArea.getWidth(), tunerArea.getHeight() * 27 / 10), tunerArea.getHeight()));
    table.setBounds (area);
}

#include "PluginEditor.h"
#include <cmath>

namespace
{
const auto bg = juce::Colour(0xff0d1016);
const auto panel = juce::Colour(0xff151a23);
const auto panelRaised = juce::Colour(0xff1c2330);
const auto text = juce::Colour(0xffedf3ff);
const auto muted = juce::Colour(0xff8f9bad);
const auto accent = juce::Colour(0xff7de2ff);
const auto accent2 = juce::Colour(0xffff7edb);

juce::Colour zoneColour(const int index)
{
    static const std::array<juce::Colour, 4> colours {
        juce::Colour(0xff69d8ff), juce::Colour(0xffff84d8),
        juce::Colour(0xffffc96b), juce::Colour(0xff8cff9e)
    };
    return colours[static_cast<size_t>(juce::jlimit(0, 3, index))];
}

float distance(juce::Point<float> a, juce::Point<float> b)
{
    return a.getDistanceFrom(b);
}
}

//==============================================================================
MotionCanvas::MotionCanvas(MotionEngineAudioProcessor& p) : processor(p)
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

juce::Rectangle<float> MotionCanvas::worldBounds() const
{
    auto bounds = getLocalBounds().toFloat().reduced(14.0f, 14.0f);
    bounds.removeFromBottom(24.0f);
    return bounds;
}

juce::Point<float> MotionCanvas::worldToScreen(const float worldX, const float worldY) const
{
    const auto area = worldBounds();
    return { juce::jmap(worldX, -1.0f, 1.0f, area.getX(), area.getRight()),
             juce::jmap(worldY, -1.0f, 1.0f, area.getBottom(), area.getY()) };
}

juce::Point<float> MotionCanvas::screenToWorld(const juce::Point<float> point) const
{
    const auto area = worldBounds();
    return { juce::jlimit(-1.0f, 1.0f, juce::jmap(point.x, area.getX(), area.getRight(), -1.0f, 1.0f)),
             juce::jlimit(-1.0f, 1.0f, juce::jmap(point.y, area.getBottom(), area.getY(), -1.0f, 1.0f)) };
}

float MotionCanvas::worldRadiusToPixels(const float radius) const
{
    const auto area = worldBounds();
    return radius * 0.25f * (area.getWidth() + area.getHeight());
}

void MotionCanvas::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(area, 12.0f);

    const auto world = worldBounds();
    g.setColour(juce::Colour(0xff10151d));
    g.fillRoundedRectangle(world, 9.0f);

    g.saveState();
    g.reduceClipRegion(world.toNearestInt());

    g.setColour(juce::Colour(0xff202836));
    for (int i = 1; i < 8; ++i)
    {
        const float t = static_cast<float>(i) / 8.0f;
        const float gx = world.getX() + world.getWidth() * t;
        const float gy = world.getY() + world.getHeight() * t;
        g.drawVerticalLine(static_cast<int>(gx), world.getY(), world.getBottom());
        g.drawHorizontalLine(static_cast<int>(gy), world.getX(), world.getRight());
    }

    g.setColour(juce::Colour(0xff354052));
    const auto centre = worldToScreen(0.0f, 0.0f);
    g.drawLine(centre.x, world.getY(), centre.x, world.getBottom(), 1.4f);
    g.drawLine(world.getX(), centre.y, world.getRight(), centre.y, 1.4f);

    const auto snapshot = processor.getMotionCore().getSnapshot();

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto prefix = "zone" + juce::String(zone + 1);
        const float zx = processor.parameters.getRawParameterValue(prefix + "X")->load();
        const float zy = processor.parameters.getRawParameterValue(prefix + "Y")->load();
        const float radius = processor.parameters.getRawParameterValue(prefix + "Radius")->load();
        const float falloff = juce::jmax(0.1f, processor.parameters.getRawParameterValue(prefix + "Falloff")->load());
        const auto center = worldToScreen(zx, zy);
        const float pixels = worldRadiusToPixels(radius);
        const auto ellipse = juce::Rectangle<float>(center.x - pixels, center.y - pixels, pixels * 2.0f, pixels * 2.0f);
        const auto colour = zoneColour(zone);
        const float amount = snapshot.zones[static_cast<size_t>(zone)];

        g.setColour(colour.withAlpha(0.045f + amount * 0.13f));
        g.fillEllipse(ellipse);

        constexpr std::array<float, 3> responseLevels { 0.75f, 0.50f, 0.25f };
        for (size_t levelIndex = 0; levelIndex < responseLevels.size(); ++levelIndex)
        {
            const float response = responseLevels[levelIndex];
            const float ratio = 1.0f - std::pow(response, 1.0f / falloff);
            const float ringRadius = pixels * ratio;
            const auto ring = juce::Rectangle<float>(center.x - ringRadius, center.y - ringRadius,
                                                      ringRadius * 2.0f, ringRadius * 2.0f);
            g.setColour(colour.withAlpha(0.16f + static_cast<float>(levelIndex) * 0.035f));
            g.drawEllipse(ring, 1.0f);
        }

        g.setColour(colour.withAlpha(zone == selectedZone ? 0.95f : 0.46f));
        g.drawEllipse(ellipse, zone == selectedZone ? 2.0f : 1.0f);
        g.fillEllipse(juce::Rectangle<float>(center.x - 4.0f, center.y - 4.0f, 8.0f, 8.0f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("Z" + juce::String(zone + 1), static_cast<int>(center.x + 8.0f), static_cast<int>(center.y - 18.0f), 30, 18, juce::Justification::centredLeft);
    }

    const int model = static_cast<int>(processor.parameters.getRawParameterValue("model")->load());
    if (model == 1)
    {
        const float anchorX = (processor.parameters.getRawParameterValue("motionD")->load() - 0.5f) * 0.9f;
        const auto anchor = worldToScreen(anchorX, 0.0f);
        const auto body = worldToScreen(snapshot.x, snapshot.y);
        g.setColour(accent.withAlpha(0.42f));
        g.drawLine(anchor.x, anchor.y, body.x, body.y, 1.5f);
        g.drawEllipse(juce::Rectangle<float>(anchor.x - 5.0f, anchor.y - 5.0f, 10.0f, 10.0f), 1.5f);
    }
    else if (model == 2)
    {
        const auto pivot = worldToScreen(0.0f, 0.12f);
        const auto body = worldToScreen(snapshot.x, snapshot.y);
        g.setColour(accent.withAlpha(0.45f));
        g.drawLine(pivot.x, pivot.y, body.x, body.y, 1.7f);
        g.fillEllipse(juce::Rectangle<float>(pivot.x - 4.0f, pivot.y - 4.0f, 8.0f, 8.0f));
    }
    else if (model == 0 || model == 6)
    {
        g.setColour((model == 6 ? accent2 : accent).withAlpha(0.6f));
        g.drawEllipse(juce::Rectangle<float>(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f), 1.5f);
    }

    if (trail.size() > 1)
    {
        for (size_t i = 1; i < trail.size(); ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(trail.size() - 1);
            const float alpha = 0.035f + 0.72f * t * t;
            const float thickness = 0.8f + 2.0f * t;
            g.setColour(accent.withAlpha(alpha));
            g.drawLine(trail[i - 1].x, trail[i - 1].y, trail[i].x, trail[i].y, thickness);
        }
    }

    const auto body = worldToScreen(snapshot.x, snapshot.y);
    const auto velocityEnd = body + juce::Point<float>(snapshot.vx, -snapshot.vy) * 18.0f;
    g.setColour(juce::Colour(0xffffcf73).withAlpha(0.72f));
    g.drawLine(body.x, body.y, velocityEnd.x, velocityEnd.y, 1.6f);

    const float bodyRadius = 10.0f + snapshot.energy * 4.0f;
    g.setColour(accent.withAlpha(0.18f));
    g.fillEllipse(juce::Rectangle<float>(body.x - bodyRadius - 6.0f, body.y - bodyRadius - 6.0f,
                                         (bodyRadius + 6.0f) * 2.0f, (bodyRadius + 6.0f) * 2.0f));
    g.setColour(text);
    g.fillEllipse(juce::Rectangle<float>(body.x - bodyRadius, body.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f));
    g.setColour(accent);
    g.drawEllipse(juce::Rectangle<float>(body.x - bodyRadius, body.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f), 2.0f);

    g.restoreState();

    g.setColour(juce::Colour(0xff46536a));
    g.drawRoundedRectangle(world, 9.0f, 1.2f);

    auto help = getLocalBounds().reduced(18).removeFromBottom(18);
    g.setColour(muted);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("Drag body: throw | Drag Zone center: move | Drag Zone ring: radius | Double-click: HIT",
               help, juce::Justification::centredLeft);
}

void MotionCanvas::tick()
{
    const auto snapshot = processor.getMotionCore().getSnapshot();
    const auto point = worldToScreen(snapshot.x, snapshot.y);
    if (trail.empty() || trail.back().getDistanceFrom(point) > 0.75f)
        trail.push_back(point);
    constexpr size_t maxTrailPoints = 28;
    if (trail.size() > maxTrailPoints)
        trail.erase(trail.begin(), trail.begin() + static_cast<std::ptrdiff_t>(trail.size() - maxTrailPoints));
    repaint();
}

void MotionCanvas::setSelectedZone(const int zone)
{
    selectedZone = juce::jlimit(0, motion::kNumZones - 1, zone);
    repaint();
}

void MotionCanvas::setParameterPlain(const juce::String& id, const float value)
{
    if (auto* parameter = processor.parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void MotionCanvas::beginZoneGesture()
{
    if (dragZone < 0)
        return;
    const auto prefix = "zone" + juce::String(dragZone + 1);
    if (dragMode == DragMode::zoneMove)
    {
        processor.parameters.getParameter(prefix + "X")->beginChangeGesture();
        processor.parameters.getParameter(prefix + "Y")->beginChangeGesture();
    }
    else if (dragMode == DragMode::zoneRadius)
    {
        processor.parameters.getParameter(prefix + "Radius")->beginChangeGesture();
    }
}

void MotionCanvas::endZoneGesture()
{
    if (dragZone < 0)
        return;
    const auto prefix = "zone" + juce::String(dragZone + 1);
    if (dragMode == DragMode::zoneMove)
    {
        processor.parameters.getParameter(prefix + "X")->endChangeGesture();
        processor.parameters.getParameter(prefix + "Y")->endChangeGesture();
    }
    else if (dragMode == DragMode::zoneRadius)
    {
        processor.parameters.getParameter(prefix + "Radius")->endChangeGesture();
    }
}

void MotionCanvas::mouseDown(const juce::MouseEvent& event)
{
    const auto mouse = event.position;
    const auto snapshot = processor.getMotionCore().getSnapshot();
    const auto body = worldToScreen(snapshot.x, snapshot.y);
    lastDragWorld = screenToWorld(mouse);
    lastDragTimeMs = juce::Time::getMillisecondCounterHiRes();
    flickVelocity = {};

    // The visible body is small, but grabbing a moving modulation source should
    // not be an aim-training exercise. Keep a generous invisible hit target.
    if (distance(mouse, body) <= 38.0f)
    {
        dragMode = DragMode::body;
        processor.getMotionCore().beginDrag(lastDragWorld.x, lastDragWorld.y);
        return;
    }

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto prefix = "zone" + juce::String(zone + 1);
        const float zx = processor.parameters.getRawParameterValue(prefix + "X")->load();
        const float zy = processor.parameters.getRawParameterValue(prefix + "Y")->load();
        const float radius = processor.parameters.getRawParameterValue(prefix + "Radius")->load();
        const auto center = worldToScreen(zx, zy);
        const float pixels = worldRadiusToPixels(radius);
        const float dist = distance(mouse, center);

        if (dist <= 15.0f || std::abs(dist - pixels) <= 10.0f)
        {
            dragZone = zone;
            selectedZone = zone;
            dragMode = dist <= 15.0f ? DragMode::zoneMove : DragMode::zoneRadius;
            if (onZoneSelected)
                onZoneSelected(zone);
            beginZoneGesture();
            return;
        }
    }
}

void MotionCanvas::mouseDrag(const juce::MouseEvent& event)
{
    const auto world = screenToWorld(event.position);
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jmax(1.0, now - lastDragTimeMs) * 0.001;
    flickVelocity = { static_cast<float>((world.x - lastDragWorld.x) / dt),
                      static_cast<float>((world.y - lastDragWorld.y) / dt) };
    lastDragWorld = world;
    lastDragTimeMs = now;

    if (dragMode == DragMode::body)
    {
        processor.getMotionCore().dragTo(world.x, world.y);
    }
    else if (dragZone >= 0 && dragMode == DragMode::zoneMove)
    {
        const auto prefix = "zone" + juce::String(dragZone + 1);
        setParameterPlain(prefix + "X", world.x);
        setParameterPlain(prefix + "Y", world.y);
    }
    else if (dragZone >= 0 && dragMode == DragMode::zoneRadius)
    {
        const auto prefix = "zone" + juce::String(dragZone + 1);
        const float zx = processor.parameters.getRawParameterValue(prefix + "X")->load();
        const float zy = processor.parameters.getRawParameterValue(prefix + "Y")->load();
        setParameterPlain(prefix + "Radius", juce::jlimit(0.08f, 1.5f, std::hypot(world.x - zx, world.y - zy)));
    }
}

void MotionCanvas::mouseUp(const juce::MouseEvent&)
{
    if (dragMode == DragMode::body)
        processor.getMotionCore().endDrag(flickVelocity.x, flickVelocity.y);
    else
        endZoneGesture();

    dragMode = DragMode::none;
    dragZone = -1;
}

void MotionCanvas::mouseDoubleClick(const juce::MouseEvent&)
{
    processor.getMotionCore().triggerHit();
}

//==============================================================================
OutputStrip::OutputStrip(MotionEngineAudioProcessor& p, const int slot) : processor(p), index(slot)
{
    indexLabel.setText(juce::String(index + 1), juce::dontSendNotification);
    indexLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    indexLabel.setColour(juce::Label::textColourId, accent);
    indexLabel.setJustificationType(juce::Justification::centred);

    sourceBox.addItemList(motion::MotionEngineCore::sourceNames(), 1);
    curveBox.addItemList(motion::MotionEngineCore::curveNames(), 1);

    configureCompactSlider(smoothSlider);
    smoothSlider.setTextValueSuffix(" ms");

    for (auto* rangeSlider : { &minSlider, &maxSlider })
    {
        rangeSlider->setSliderStyle(juce::Slider::LinearBar);
        rangeSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 62, 18);
        rangeSlider->setNumDecimalPlacesToDisplay(3);
        rangeSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111722));
        rangeSlider->setColour(juce::Slider::trackColourId, accent.withAlpha(0.22f));
        rangeSlider->setColour(juce::Slider::textBoxTextColourId, text);
        rangeSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    minSlider.setTooltip("Minimum output value");
    maxSlider.setTooltip("Maximum output value");

    targetLabel.setColour(juce::Label::textColourId, muted);
    targetLabel.setFont(juce::FontOptions(11.5f));
    targetLabel.setJustificationType(juce::Justification::centredLeft);

    for (auto* component : { static_cast<juce::Component*>(&indexLabel), static_cast<juce::Component*>(&sourceBox),
                             static_cast<juce::Component*>(&minSlider), static_cast<juce::Component*>(&maxSlider),
                             static_cast<juce::Component*>(&curveBox), static_cast<juce::Component*>(&smoothSlider),
                             static_cast<juce::Component*>(&mapButton), static_cast<juce::Component*>(&clearButton),
                             static_cast<juce::Component*>(&targetLabel) })
        addAndMakeVisible(component);

    const auto prefix = "out" + juce::String(index + 1);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, prefix + "Source", sourceBox);
    minAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, prefix + "Min", minSlider);
    maxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, prefix + "Max", maxSlider);
    curveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, prefix + "Curve", curveBox);
    smoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, prefix + "Smooth", smoothSlider);

    mapButton.onClick = [this] { processor.getBridge().requestMap(index); };
    clearButton.onClick = [this] { processor.getBridge().requestUnmap(index); };
}

void OutputStrip::configureCompactSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
    slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.45f));
    slider.setColour(juce::Slider::thumbColourId, text);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void OutputStrip::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(panelRaised);
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(juce::Colour(0xff2a3444));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

    const auto meter = juce::Rectangle<float>(2.0f, 5.0f, 4.0f, bounds.getHeight() - 10.0f);
    g.setColour(juce::Colour(0xff2b3544));
    g.fillRoundedRectangle(meter, 2.0f);
    g.setColour(accent);
    const float fill = meter.getHeight() * juce::jlimit(0.0f, 1.0f, currentValue);
    g.fillRoundedRectangle(meter.withTrimmedTop(meter.getHeight() - fill), 2.0f);
}

void OutputStrip::resized()
{
    auto area = getLocalBounds().reduced(8, 5);
    indexLabel.setBounds(area.removeFromLeft(24));

    auto top = area.removeFromTop(28);
    sourceBox.setBounds(top.removeFromLeft(142).reduced(2, 1));
    minSlider.setBounds(top.removeFromLeft(66).reduced(2, 1));
    maxSlider.setBounds(top.removeFromLeft(66).reduced(2, 1));
    clearButton.setBounds(top.removeFromRight(28).reduced(2, 1));
    mapButton.setBounds(top.removeFromRight(56).reduced(2, 1));

    auto bottom = area.removeFromBottom(27);
    curveBox.setBounds(bottom.removeFromLeft(108).reduced(2, 1));
    smoothSlider.setBounds(bottom.removeFromLeft(122).reduced(2, 1));
    targetLabel.setBounds(bottom.reduced(5, 0));
}

void OutputStrip::update(const motion::MotionEngineCore::Snapshot& snapshot, const BridgeEngine::SlotStatus& status)
{
    currentValue = snapshot.outputs[static_cast<size_t>(index)];
    if (status.armed)
    {
        targetLabel.setText("move target parameter...", juce::dontSendNotification);
        targetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffcf73));
        mapButton.setButtonText("ARMED");
    }
    else if (status.mapped)
    {
        targetLabel.setText(status.targetName, juce::dontSendNotification);
        targetLabel.setColour(juce::Label::textColourId, text);
        mapButton.setButtonText("MAP");
    }
    else
    {
        targetLabel.setText("unmapped | aux: Motion " + juce::String(index + 1), juce::dontSendNotification);
        targetLabel.setColour(juce::Label::textColourId, muted);
        mapButton.setButtonText("MAP");
    }
    repaint();
}

//==============================================================================
MotionEngineAudioProcessorEditor::MotionEngineAudioProcessorEditor(MotionEngineAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), canvas(p)
{
    titleLabel.setText("MOTION ENGINE", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, text);
    subtitleLabel.setText("physics-driven modulation", juce::dontSendNotification);
    subtitleLabel.setFont(juce::FontOptions(13.0f));
    subtitleLabel.setColour(juce::Label::textColourId, muted);
    outputsTitleLabel.setText("MOTION OUTPUTS", juce::dontSendNotification);
    outputsTitleLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    outputsTitleLabel.setColour(juce::Label::textColourId, text);
    bridgeLabel.setFont(juce::FontOptions(11.5f));
    bridgeLabel.setColour(juce::Label::textColourId, muted);
    bridgeLabel.setMinimumHorizontalScale(0.72f);

    modelBox.addItemList(motion::MotionEngineCore::modelNames(), 1);
    constraintBox.addItemList(motion::MotionEngineCore::constraintNames(), 1);
    for (int zone = 0; zone < motion::kNumZones; ++zone)
        zoneBox.addItem("Zone " + juce::String(zone + 1), zone + 1);

    for (auto* component : { static_cast<juce::Component*>(&titleLabel), static_cast<juce::Component*>(&subtitleLabel),
                             static_cast<juce::Component*>(&outputsTitleLabel), static_cast<juce::Component*>(&bridgeLabel),
                             static_cast<juce::Component*>(&modelBox), static_cast<juce::Component*>(&constraintBox),
                             static_cast<juce::Component*>(&hitButton), static_cast<juce::Component*>(&resetButton),
                             static_cast<juce::Component*>(&canvas), static_cast<juce::Component*>(&zoneBox),
                             static_cast<juce::Component*>(&zoneRadiusSlider), static_cast<juce::Component*>(&zoneFalloffSlider) })
        addAndMakeVisible(component);

    configureSlider(timeSlider, " x");
    timeSlider.setNumDecimalPlacesToDisplay(3);
    configureSlider(energySlider);
    configureSlider(dampingSlider);
    configureSlider(audioKickSlider);
    configureLabel(timeLabel, "Time");
    configureLabel(energyLabel, "Energy");
    configureLabel(dampingLabel, "World Drag");
    configureLabel(audioKickLabel, "Audio Kick");

    for (int i = 0; i < 4; ++i)
    {
        configureSlider(motionSliders[static_cast<size_t>(i)]);
        configureLabel(motionLabels[static_cast<size_t>(i)], "Motion");
    }

    configureSlider(zoneRadiusSlider);
    configureSlider(zoneFalloffSlider);
    configureLabel(zoneLabel, "Zone edit");
    configureLabel(zoneRadiusLabel, "Radius");
    configureLabel(zoneFalloffLabel, "Falloff");

    for (auto* component : { static_cast<juce::Component*>(&timeSlider), static_cast<juce::Component*>(&energySlider),
                             static_cast<juce::Component*>(&dampingSlider), static_cast<juce::Component*>(&audioKickSlider),
                             static_cast<juce::Component*>(&timeLabel), static_cast<juce::Component*>(&energyLabel),
                             static_cast<juce::Component*>(&dampingLabel), static_cast<juce::Component*>(&audioKickLabel),
                             static_cast<juce::Component*>(&zoneLabel), static_cast<juce::Component*>(&zoneRadiusLabel),
                             static_cast<juce::Component*>(&zoneFalloffLabel) })
        addAndMakeVisible(component);

    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible(motionSliders[static_cast<size_t>(i)]);
        addAndMakeVisible(motionLabels[static_cast<size_t>(i)]);
    }

    modelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, "model", modelBox);
    constraintAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, "constraint", constraintBox);
    timeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "timeScale", timeSlider);
    energyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "energy", energySlider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "globalDamping", dampingSlider);
    audioKickAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "audioKick", audioKickSlider);

    for (int i = 0; i < 4; ++i)
        motionAttachments[static_cast<size_t>(i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters,
            "motion" + juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)),
            motionSliders[static_cast<size_t>(i)]);

    for (int i = 0; i < motion::kNumOutputs; ++i)
    {
        outputStrips[static_cast<size_t>(i)] = std::make_unique<OutputStrip>(processor, i);
        addAndMakeVisible(*outputStrips[static_cast<size_t>(i)]);
    }

    hitButton.onClick = [this] { processor.getMotionCore().triggerHit(); };
    resetButton.onClick = [this] { processor.getMotionCore().reset(); };
    modelBox.onChange = [this] { updateModelLabels(); };
    zoneBox.onChange = [this] { bindSelectedZone(zoneBox.getSelectedItemIndex()); };
    canvas.onZoneSelected = [this](const int zone)
    {
        zoneBox.setSelectedItemIndex(zone, juce::sendNotificationSync);
    };

    zoneBox.setSelectedItemIndex(0, juce::sendNotificationSync);
    updateModelLabels();

    setResizable(true, true);
    setResizeLimits(1120, 700, 1800, 1100);
    setSize(1320, 820);
    startTimerHz(60);
}

MotionEngineAudioProcessorEditor::~MotionEngineAudioProcessorEditor() = default;

void MotionEngineAudioProcessorEditor::configureSlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.5f));
    slider.setColour(juce::Slider::thumbColourId, text);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void MotionEngineAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& value)
{
    label.setText(value, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centredLeft);
}

void MotionEngineAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto content = getLocalBounds().toFloat().reduced(10.0f);
    auto right = content.removeFromRight(465.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(right, 12.0f);
}

void MotionEngineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(14);
    auto header = area.removeFromTop(46);
    titleLabel.setBounds(header.removeFromLeft(210));
    subtitleLabel.setBounds(header.removeFromLeft(250).withTrimmedTop(5));

    auto right = area.removeFromRight(455);
    area.removeFromRight(12);
    auto left = area;

    auto controls = left.removeFromTop(178);
    auto firstRow = controls.removeFromTop(36);
    modelBox.setBounds(firstRow.removeFromLeft(190).reduced(2));
    constraintBox.setBounds(firstRow.removeFromLeft(160).reduced(2));
    hitButton.setBounds(firstRow.removeFromLeft(78).reduced(3));
    resetButton.setBounds(firstRow.removeFromLeft(86).reduced(3));

    auto globalRow = controls.removeFromTop(46);
    const int globalWidth = globalRow.getWidth() / 4;
    std::array<juce::Slider*, 4> globalSliders { &timeSlider, &energySlider, &dampingSlider, &audioKickSlider };
    std::array<juce::Label*, 4> globalLabels { &timeLabel, &energyLabel, &dampingLabel, &audioKickLabel };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = globalRow.removeFromLeft(globalWidth).reduced(2);
        globalLabels[static_cast<size_t>(i)]->setBounds(cell.removeFromTop(17));
        globalSliders[static_cast<size_t>(i)]->setBounds(cell);
    }

    auto motionRow = controls.removeFromTop(50);
    const int motionWidth = motionRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = motionRow.removeFromLeft(motionWidth).reduced(2);
        motionLabels[static_cast<size_t>(i)].setBounds(cell.removeFromTop(17));
        motionSliders[static_cast<size_t>(i)].setBounds(cell);
    }

    auto zoneRow = controls.removeFromTop(46);
    auto zoneCell = zoneRow.removeFromLeft(140).reduced(2);
    zoneLabel.setBounds(zoneCell.removeFromTop(17));
    zoneBox.setBounds(zoneCell);
    auto radiusCell = zoneRow.removeFromLeft(220).reduced(2);
    zoneRadiusLabel.setBounds(radiusCell.removeFromTop(17));
    zoneRadiusSlider.setBounds(radiusCell);
    auto falloffCell = zoneRow.removeFromLeft(220).reduced(2);
    zoneFalloffLabel.setBounds(falloffCell.removeFromTop(17));
    zoneFalloffSlider.setBounds(falloffCell);

    canvas.setBounds(left.reduced(0, 2));

    auto rightInner = right.reduced(10);
    outputsTitleLabel.setBounds(rightInner.removeFromTop(28));
    auto bridgeArea = rightInner.removeFromBottom(44);
    bridgeLabel.setBounds(bridgeArea);
    const int stripHeight = juce::jmax(62, rightInner.getHeight() / motion::kNumOutputs);
    for (int i = 0; i < motion::kNumOutputs; ++i)
        if (auto* strip = outputStrips[static_cast<size_t>(i)].get())
            strip->setBounds(rightInner.removeFromTop(stripHeight).reduced(0, 3));
}

void MotionEngineAudioProcessorEditor::updateModelLabels()
{
    const int model = static_cast<int>(processor.parameters.getRawParameterValue("model")->load());
    displayedModel = model;
    const auto names = motion::MotionEngineCore::controlNamesForModel(model);
    for (int i = 0; i < 4; ++i)
        motionLabels[static_cast<size_t>(i)].setText(names[static_cast<size_t>(i)], juce::dontSendNotification);
}

void MotionEngineAudioProcessorEditor::bindSelectedZone(const int zone)
{
    const int safeZone = juce::jlimit(0, motion::kNumZones - 1, zone);
    canvas.setSelectedZone(safeZone);
    const auto prefix = "zone" + juce::String(safeZone + 1);
    zoneRadiusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, prefix + "Radius", zoneRadiusSlider);
    zoneFalloffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, prefix + "Falloff", zoneFalloffSlider);
}

void MotionEngineAudioProcessorEditor::timerCallback()
{
    canvas.tick();
    const auto snapshot = processor.getMotionCore().getSnapshot();
    const auto bridge = processor.getBridge().getStatus();

    int mappedCount = 0;
    for (int i = 0; i < motion::kNumOutputs; ++i)
    {
        if (auto* strip = outputStrips[static_cast<size_t>(i)].get())
            strip->update(snapshot, bridge.slots[static_cast<size_t>(i)]);
        if (bridge.slots[static_cast<size_t>(i)].mapped)
            ++mappedCount;
    }

    const int model = static_cast<int>(processor.parameters.getRawParameterValue("model")->load());
    if (model != displayedModel)
        updateModelLabels();

    if (!bridge.bridgeSeen)
    {
        bridgeLabel.setText("Bitwig bridge not seen | aux CV: Motion 1-8 available", juce::dontSendNotification);
        bridgeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffb08c));
    }
    else if (mappedCount == 0)
    {
        bridgeLabel.setText("Bitwig bridge online | no mapped outputs | aux CV: Motion 1-8", juce::dontSendNotification);
        bridgeLabel.setColour(juce::Label::textColourId, muted);
    }
    else
    {
        bridgeLabel.setText(juce::String::formatted("Bitwig bridge | sent %.0f Hz | applied %.0f Hz | worst %.1f ms | aux CV: Motion 1-8",
                                                    bridge.sentHz, bridge.appliedHz, bridge.worstGapMs),
                            juce::dontSendNotification);
        bridgeLabel.setColour(juce::Label::textColourId, muted);
    }
}

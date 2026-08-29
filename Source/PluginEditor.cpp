#include "PluginEditor.h"

#include <algorithm>
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

juce::String toJuce(std::string_view value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

juce::String toJuce(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

juce::Colour zoneColour(int index)
{
    static const std::array<juce::Colour, 4> colours {
        juce::Colour(0xff69d8ff), juce::Colour(0xffff84d8),
        juce::Colour(0xffffc96b), juce::Colour(0xff8cff9e)
    };
    return colours[static_cast<std::size_t>(std::clamp(index, 0, 3))];
}

float distance(juce::Point<float> a, juce::Point<float> b)
{
    return a.getDistanceFrom(b);
}
} // namespace

//==============================================================================
MotionCanvas::MotionCanvas(MotionEnginePlugin& plugin) : plugin_(plugin)
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

juce::Rectangle<float> MotionCanvas::worldBounds() const
{
    auto available = getLocalBounds().toFloat().reduced(14.0f, 14.0f);
    available.removeFromBottom(24.0f);
    const float side = std::max(1.0f, std::min(available.getWidth(), available.getHeight()));
    return { available.getCentreX() - side * 0.5f,
             available.getCentreY() - side * 0.5f,
             side, side };
}

juce::Point<float> MotionCanvas::worldToScreen(float worldX, float worldY) const
{
    const auto area = worldBounds();
    return { juce::jmap(worldX, -1.0f, 1.0f, area.getX(), area.getRight()),
             juce::jmap(worldY, -1.0f, 1.0f, area.getBottom(), area.getY()) };
}

juce::Point<float> MotionCanvas::screenToWorld(juce::Point<float> point) const
{
    const auto area = worldBounds();
    return { std::clamp(juce::jmap(point.x, area.getX(), area.getRight(), -1.0f, 1.0f), -1.0f, 1.0f),
             std::clamp(juce::jmap(point.y, area.getBottom(), area.getY(), -1.0f, 1.0f), -1.0f, 1.0f) };
}

float MotionCanvas::worldRadiusToPixels(float radius) const
{
    return radius * worldBounds().getWidth() * 0.5f;
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
        g.drawVerticalLine(static_cast<int>(world.getX() + world.getWidth() * t), world.getY(), world.getBottom());
        g.drawHorizontalLine(static_cast<int>(world.getY() + world.getHeight() * t), world.getX(), world.getRight());
    }

    g.setColour(juce::Colour(0xff354052));
    const auto centre = worldToScreen(0.0f, 0.0f);
    g.drawLine(centre.x, world.getY(), centre.x, world.getBottom(), 1.4f);
    g.drawLine(world.getX(), centre.y, world.getRight(), centre.y, 1.4f);

    const auto snapshot = plugin_.motionCore().getSnapshot();
    const int model = plugin_.parameterInt(motion::ids::model);

    if (model == 0)
    {
        const float radius = 0.22f + static_cast<float>(plugin_.parameterValue(motion::ids::motionA)) * 0.68f;
        const float aspect = 1.0f - static_cast<float>(plugin_.parameterValue(motion::ids::motionB)) * 0.65f;
        const float rotation = static_cast<float>(plugin_.parameterValue(motion::ids::motionC)) * juce::MathConstants<float>::pi;
        const float co = std::cos(rotation);
        const float so = std::sin(rotation);
        juce::Path orbitGuide;
        constexpr int samples = 96;
        for (int i = 0; i <= samples; ++i)
        {
            const float phase = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(samples);
            const float localX = radius * std::cos(phase);
            const float localY = radius * aspect * std::sin(phase);
            const auto point = worldToScreen(co * localX - so * localY, so * localX + co * localY);
            if (i == 0) orbitGuide.startNewSubPath(point); else orbitGuide.lineTo(point);
        }
        g.setColour(accent.withAlpha(0.18f));
        g.strokePath(orbitGuide, juce::PathStrokeType(1.15f));
    }

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(zone)];
        const float zx = static_cast<float>(plugin_.parameterValue(id.x));
        const float zy = static_cast<float>(plugin_.parameterValue(id.y));
        const float radius = static_cast<float>(plugin_.parameterValue(id.radius));
        const float falloff = std::max(0.1f, static_cast<float>(plugin_.parameterValue(id.falloff)));
        const auto center = worldToScreen(zx, zy);
        const float pixels = worldRadiusToPixels(radius);
        const auto ellipse = juce::Rectangle<float>(center.x - pixels, center.y - pixels, pixels * 2.0f, pixels * 2.0f);
        const auto colour = zoneColour(zone);
        const float amount = snapshot.zones[static_cast<std::size_t>(zone)];

        g.setColour(colour.withAlpha(0.045f + amount * 0.13f));
        g.fillEllipse(ellipse);
        constexpr std::array<float, 3> responseLevels { 0.75f, 0.50f, 0.25f };
        for (std::size_t level = 0; level < responseLevels.size(); ++level)
        {
            const float ratio = 1.0f - std::pow(responseLevels[level], 1.0f / falloff);
            const float ringRadius = pixels * ratio;
            const auto ring = juce::Rectangle<float>(center.x - ringRadius, center.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
            g.setColour(colour.withAlpha(0.16f + static_cast<float>(level) * 0.035f));
            g.drawEllipse(ring, 1.0f);
        }
        g.setColour(colour.withAlpha(zone == selectedZone_ ? 0.95f : 0.46f));
        g.drawEllipse(ellipse, zone == selectedZone_ ? 2.0f : 1.0f);
        g.fillEllipse(juce::Rectangle<float>(center.x - 4.0f, center.y - 4.0f, 8.0f, 8.0f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("Z" + juce::String(zone + 1), static_cast<int>(center.x + 8.0f), static_cast<int>(center.y - 18.0f), 30, 18,
                   juce::Justification::centredLeft);
    }

    if (model == 1)
    {
        const float anchorX = (static_cast<float>(plugin_.parameterValue(motion::ids::motionD)) - 0.5f) * 0.9f;
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

    if (trail_.size() > 1)
    {
        for (std::size_t i = 1; i < trail_.size(); ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(trail_.size() - 1);
            g.setColour(accent.withAlpha(0.035f + 0.72f * t * t));
            g.drawLine(trail_[i - 1].x, trail_[i - 1].y, trail_[i].x, trail_[i].y, 0.8f + 2.0f * t);
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
    const auto snapshot = plugin_.motionCore().getSnapshot();
    const auto point = worldToScreen(snapshot.x, snapshot.y);
    if (trail_.empty() || trail_.back().getDistanceFrom(point) > 0.75f)
        trail_.push_back(point);
    constexpr std::size_t maxTrailPoints = 28;
    if (trail_.size() > maxTrailPoints)
        trail_.erase(trail_.begin(), trail_.begin() + static_cast<std::ptrdiff_t>(trail_.size() - maxTrailPoints));
    repaint();
}

void MotionCanvas::setSelectedZone(int zone)
{
    selectedZone_ = std::clamp(zone, 0, motion::kNumZones - 1);
    repaint();
}

void MotionCanvas::setParameter(clap_id id, double value)
{
    plugin_.setParameterFromGui(id, value);
}

void MotionCanvas::beginZoneGesture()
{
    if (dragZone_ < 0)
        return;
    const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
    if (dragMode_ == DragMode::zoneMove)
    {
        plugin_.beginParameterGesture(id.x);
        plugin_.beginParameterGesture(id.y);
    }
    else if (dragMode_ == DragMode::zoneRadius)
    {
        plugin_.beginParameterGesture(id.radius);
    }
}

void MotionCanvas::endZoneGesture()
{
    if (dragZone_ < 0)
        return;
    const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
    if (dragMode_ == DragMode::zoneMove)
    {
        plugin_.endParameterGesture(id.x);
        plugin_.endParameterGesture(id.y);
    }
    else if (dragMode_ == DragMode::zoneRadius)
    {
        plugin_.endParameterGesture(id.radius);
    }
}

void MotionCanvas::mouseDown(const juce::MouseEvent& event)
{
    const auto mouse = event.position;
    const auto snapshot = plugin_.motionCore().getSnapshot();
    const auto body = worldToScreen(snapshot.x, snapshot.y);
    lastDragWorld_ = screenToWorld(mouse);
    lastDragTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    flickVelocity_ = {};

    if (distance(mouse, body) <= 38.0f)
    {
        dragMode_ = DragMode::body;
        plugin_.motionCore().beginDrag(lastDragWorld_.x, lastDragWorld_.y);
        return;
    }

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(zone)];
        const float zx = static_cast<float>(plugin_.parameterValue(id.x));
        const float zy = static_cast<float>(plugin_.parameterValue(id.y));
        const float radius = static_cast<float>(plugin_.parameterValue(id.radius));
        const auto center = worldToScreen(zx, zy);
        const float pixels = worldRadiusToPixels(radius);
        const float dist = distance(mouse, center);
        if (dist <= 15.0f || std::abs(dist - pixels) <= 10.0f)
        {
            dragZone_ = zone;
            selectedZone_ = zone;
            dragMode_ = dist <= 15.0f ? DragMode::zoneMove : DragMode::zoneRadius;
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
    const double dt = std::max(1.0, now - lastDragTimeMs_) * 0.001;
    flickVelocity_ = { static_cast<float>((world.x - lastDragWorld_.x) / dt),
                       static_cast<float>((world.y - lastDragWorld_.y) / dt) };
    lastDragWorld_ = world;
    lastDragTimeMs_ = now;

    if (dragMode_ == DragMode::body)
    {
        plugin_.motionCore().dragTo(world.x, world.y);
    }
    else if (dragZone_ >= 0 && dragMode_ == DragMode::zoneMove)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
        setParameter(id.x, world.x);
        setParameter(id.y, world.y);
    }
    else if (dragZone_ >= 0 && dragMode_ == DragMode::zoneRadius)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
        const float zx = static_cast<float>(plugin_.parameterValue(id.x));
        const float zy = static_cast<float>(plugin_.parameterValue(id.y));
        setParameter(id.radius, std::clamp(std::hypot(world.x - zx, world.y - zy), 0.08f, 1.5f));
    }
}

void MotionCanvas::mouseUp(const juce::MouseEvent&)
{
    if (dragMode_ == DragMode::body)
        plugin_.motionCore().endDrag(flickVelocity_.x, flickVelocity_.y);
    else
        endZoneGesture();
    dragMode_ = DragMode::none;
    dragZone_ = -1;
}

void MotionCanvas::mouseDoubleClick(const juce::MouseEvent&)
{
    plugin_.motionCore().triggerHit();
}

//==============================================================================
OutputStrip::OutputStrip(MotionEnginePlugin& plugin, int index) : plugin_(plugin), index_(index)
{
    indexLabel_.setText(juce::String(index_ + 1), juce::dontSendNotification);
    indexLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    indexLabel_.setColour(juce::Label::textColourId, accent);
    indexLabel_.setJustificationType(juce::Justification::centred);

    int item = 1;
    for (const auto name : motion::MotionEngineCore::sourceNames())
        sourceBox_.addItem(toJuce(name), item++);
    item = 1;
    for (const auto name : motion::MotionEngineCore::curveNames())
        curveBox_.addItem(toJuce(name), item++);

    minSlider_.setRange(0.0, 1.0, 0.001);
    maxSlider_.setRange(0.0, 1.0, 0.001);
    smoothSlider_.setRange(0.0, 500.0, 0.1);
    configureCompactSlider(smoothSlider_);
    smoothSlider_.setTextValueSuffix(" ms");

    for (auto* rangeSlider : { &minSlider_, &maxSlider_ })
    {
        rangeSlider->setSliderStyle(juce::Slider::LinearBar);
        rangeSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 62, 18);
        rangeSlider->setNumDecimalPlacesToDisplay(3);
        rangeSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111722));
        rangeSlider->setColour(juce::Slider::trackColourId, accent.withAlpha(0.22f));
        rangeSlider->setColour(juce::Slider::textBoxTextColourId, text);
        rangeSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    targetLabel_.setColour(juce::Label::textColourId, muted);
    targetLabel_.setFont(juce::FontOptions(11.5f));
    targetLabel_.setJustificationType(juce::Justification::centredLeft);

    for (auto* component : { static_cast<juce::Component*>(&indexLabel_), static_cast<juce::Component*>(&sourceBox_),
                             static_cast<juce::Component*>(&minSlider_), static_cast<juce::Component*>(&maxSlider_),
                             static_cast<juce::Component*>(&curveBox_), static_cast<juce::Component*>(&smoothSlider_),
                             static_cast<juce::Component*>(&mapButton_), static_cast<juce::Component*>(&clearButton_),
                             static_cast<juce::Component*>(&targetLabel_) })
        addAndMakeVisible(component);

    const auto& id = motion::ids::outputs[static_cast<std::size_t>(index_)];
    sourceBox_.onChange = [this, source = id.source]
    {
        if (!syncing_ && sourceBox_.getSelectedId() > 0)
            setOneShot(source, sourceBox_.getSelectedItemIndex());
    };
    curveBox_.onChange = [this, curve = id.curve]
    {
        if (!syncing_ && curveBox_.getSelectedId() > 0)
            setOneShot(curve, curveBox_.getSelectedItemIndex());
    };
    bindSlider(minSlider_, id.minimum);
    bindSlider(maxSlider_, id.maximum);
    bindSlider(smoothSlider_, id.smoothing);

    mapButton_.onClick = [this] { plugin_.bridge().requestMap(index_); };
    clearButton_.onClick = [this] { plugin_.bridge().requestUnmap(index_); };
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

void OutputStrip::setOneShot(clap_id id, double value)
{
    plugin_.beginParameterGesture(id);
    plugin_.setParameterFromGui(id, value);
    plugin_.endParameterGesture(id);
}

void OutputStrip::bindSlider(juce::Slider& slider, clap_id id)
{
    slider.onDragStart = [this, id] { plugin_.beginParameterGesture(id); };
    slider.onValueChange = [this, &slider, id]
    {
        if (!syncing_)
            plugin_.setParameterFromGui(id, slider.getValue());
    };
    slider.onDragEnd = [this, id] { plugin_.endParameterGesture(id); };
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
    const float fill = meter.getHeight() * std::clamp(currentValue_, 0.0f, 1.0f);
    g.fillRoundedRectangle(meter.withTrimmedTop(meter.getHeight() - fill), 2.0f);
}

void OutputStrip::resized()
{
    auto area = getLocalBounds().reduced(8, 5);
    indexLabel_.setBounds(area.removeFromLeft(24));
    auto top = area.removeFromTop(28);
    sourceBox_.setBounds(top.removeFromLeft(142).reduced(2, 1));
    minSlider_.setBounds(top.removeFromLeft(66).reduced(2, 1));
    maxSlider_.setBounds(top.removeFromLeft(66).reduced(2, 1));
    clearButton_.setBounds(top.removeFromRight(28).reduced(2, 1));
    mapButton_.setBounds(top.removeFromRight(56).reduced(2, 1));
    auto bottom = area.removeFromBottom(27);
    curveBox_.setBounds(bottom.removeFromLeft(108).reduced(2, 1));
    smoothSlider_.setBounds(bottom.removeFromLeft(122).reduced(2, 1));
    targetLabel_.setBounds(bottom.reduced(5, 0));
}

void OutputStrip::sync(const motion::MotionEngineCore::Snapshot& snapshot, const BridgeEngine::SlotStatus& status)
{
    const auto& id = motion::ids::outputs[static_cast<std::size_t>(index_)];
    syncing_ = true;
    sourceBox_.setSelectedItemIndex(plugin_.parameterInt(id.source), juce::dontSendNotification);
    minSlider_.setValue(plugin_.parameterValue(id.minimum), juce::dontSendNotification);
    maxSlider_.setValue(plugin_.parameterValue(id.maximum), juce::dontSendNotification);
    curveBox_.setSelectedItemIndex(plugin_.parameterInt(id.curve), juce::dontSendNotification);
    smoothSlider_.setValue(plugin_.parameterValue(id.smoothing), juce::dontSendNotification);
    syncing_ = false;

    currentValue_ = snapshot.outputs[static_cast<std::size_t>(index_)];
    if (status.armed)
    {
        targetLabel_.setText("move target parameter...", juce::dontSendNotification);
        targetLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffcf73));
        mapButton_.setButtonText("ARMED");
    }
    else if (status.mapped)
    {
        targetLabel_.setText(toJuce(status.targetName), juce::dontSendNotification);
        targetLabel_.setColour(juce::Label::textColourId, text);
        mapButton_.setButtonText("MAP");
    }
    else
    {
        targetLabel_.setText("unmapped | aux: Motion " + juce::String(index_ + 1), juce::dontSendNotification);
        targetLabel_.setColour(juce::Label::textColourId, muted);
        mapButton_.setButtonText("MAP");
    }
    repaint();
}

//==============================================================================
MotionEngineEditor::MotionEngineEditor(MotionEnginePlugin& plugin)
    : plugin_(plugin), canvas_(plugin)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    titleLabel_.setText("MOTION ENGINE", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, text);
    subtitleLabel_.setText("physics-driven modulation", juce::dontSendNotification);
    subtitleLabel_.setFont(juce::FontOptions(13.0f));
    subtitleLabel_.setColour(juce::Label::textColourId, muted);
    outputsTitleLabel_.setText("MOTION OUTPUTS", juce::dontSendNotification);
    outputsTitleLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    outputsTitleLabel_.setColour(juce::Label::textColourId, text);
    bridgeLabel_.setFont(juce::FontOptions(11.5f));
    bridgeLabel_.setColour(juce::Label::textColourId, muted);
    bridgeLabel_.setMinimumHorizontalScale(0.72f);

    int item = 1;
    for (const auto name : motion::MotionEngineCore::modelNames())
        modelBox_.addItem(toJuce(name), item++);
    item = 1;
    for (const auto name : motion::MotionEngineCore::constraintNames())
        constraintBox_.addItem(toJuce(name), item++);
    for (int zone = 0; zone < motion::kNumZones; ++zone)
        zoneBox_.addItem("Zone " + juce::String(zone + 1), zone + 1);

    for (auto* component : { static_cast<juce::Component*>(&titleLabel_), static_cast<juce::Component*>(&subtitleLabel_),
                             static_cast<juce::Component*>(&outputsTitleLabel_), static_cast<juce::Component*>(&bridgeLabel_),
                             static_cast<juce::Component*>(&modelBox_), static_cast<juce::Component*>(&constraintBox_),
                             static_cast<juce::Component*>(&hitButton_), static_cast<juce::Component*>(&resetButton_),
                             static_cast<juce::Component*>(&canvas_), static_cast<juce::Component*>(&zoneBox_),
                             static_cast<juce::Component*>(&zoneRadiusSlider_), static_cast<juce::Component*>(&zoneFalloffSlider_) })
        addAndMakeVisible(component);

    configureSlider(timeSlider_, " x");
    timeSlider_.setRange(0.1, 3.0, 0.001);
    timeSlider_.setNumDecimalPlacesToDisplay(3);
    configureSlider(energySlider_); energySlider_.setRange(0.0, 2.0, 0.001);
    configureSlider(dampingSlider_); dampingSlider_.setRange(0.0, 2.0, 0.001);
    configureSlider(audioKickSlider_); audioKickSlider_.setRange(0.0, 2.0, 0.001);
    configureLabel(timeLabel_, "Time");
    configureLabel(energyLabel_, "Energy");
    configureLabel(dampingLabel_, "World Drag");
    configureLabel(audioKickLabel_, "Audio Kick");

    for (int i = 0; i < 4; ++i)
    {
        configureSlider(motionSliders_[static_cast<std::size_t>(i)]);
        motionSliders_[static_cast<std::size_t>(i)].setRange(0.0, 1.0, 0.001);
        configureLabel(motionLabels_[static_cast<std::size_t>(i)], "Motion");
        addAndMakeVisible(motionSliders_[static_cast<std::size_t>(i)]);
        addAndMakeVisible(motionLabels_[static_cast<std::size_t>(i)]);
    }

    configureSlider(zoneRadiusSlider_); zoneRadiusSlider_.setRange(0.08, 1.5, 0.001);
    configureSlider(zoneFalloffSlider_); zoneFalloffSlider_.setRange(0.2, 4.0, 0.001);
    configureLabel(zoneLabel_, "Zone edit");
    configureLabel(zoneRadiusLabel_, "Radius");
    configureLabel(zoneFalloffLabel_, "Falloff");

    for (auto* component : { static_cast<juce::Component*>(&timeSlider_), static_cast<juce::Component*>(&energySlider_),
                             static_cast<juce::Component*>(&dampingSlider_), static_cast<juce::Component*>(&audioKickSlider_),
                             static_cast<juce::Component*>(&timeLabel_), static_cast<juce::Component*>(&energyLabel_),
                             static_cast<juce::Component*>(&dampingLabel_), static_cast<juce::Component*>(&audioKickLabel_),
                             static_cast<juce::Component*>(&zoneLabel_), static_cast<juce::Component*>(&zoneRadiusLabel_),
                             static_cast<juce::Component*>(&zoneFalloffLabel_) })
        addAndMakeVisible(component);

    bindSlider(timeSlider_, motion::ids::timeScale);
    bindSlider(energySlider_, motion::ids::energy);
    bindSlider(dampingSlider_, motion::ids::globalDamping);
    bindSlider(audioKickSlider_, motion::ids::audioKick);
    bindSlider(motionSliders_[0], motion::ids::motionA);
    bindSlider(motionSliders_[1], motion::ids::motionB);
    bindSlider(motionSliders_[2], motion::ids::motionC);
    bindSlider(motionSliders_[3], motion::ids::motionD);

    modelBox_.onChange = [this]
    {
        if (!syncing_ && modelBox_.getSelectedId() > 0)
        {
            setOneShot(motion::ids::model, modelBox_.getSelectedItemIndex());
            updateModelLabels();
        }
    };
    constraintBox_.onChange = [this]
    {
        if (!syncing_ && constraintBox_.getSelectedId() > 0)
            setOneShot(motion::ids::constraint, constraintBox_.getSelectedItemIndex());
    };
    zoneBox_.onChange = [this]
    {
        if (!syncing_ && zoneBox_.getSelectedId() > 0)
            selectZone(zoneBox_.getSelectedItemIndex());
    };

    zoneRadiusSlider_.onDragStart = [this]
    {
        plugin_.beginParameterGesture(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].radius);
    };
    zoneRadiusSlider_.onValueChange = [this]
    {
        if (!syncing_)
            plugin_.setParameterFromGui(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].radius, zoneRadiusSlider_.getValue());
    };
    zoneRadiusSlider_.onDragEnd = [this]
    {
        plugin_.endParameterGesture(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].radius);
    };
    zoneFalloffSlider_.onDragStart = [this]
    {
        plugin_.beginParameterGesture(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].falloff);
    };
    zoneFalloffSlider_.onValueChange = [this]
    {
        if (!syncing_)
            plugin_.setParameterFromGui(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].falloff, zoneFalloffSlider_.getValue());
    };
    zoneFalloffSlider_.onDragEnd = [this]
    {
        plugin_.endParameterGesture(motion::ids::zones[static_cast<std::size_t>(selectedZone_)].falloff);
    };

    hitButton_.onClick = [this] { plugin_.motionCore().triggerHit(); };
    resetButton_.onClick = [this] { plugin_.motionCore().requestReset(); };
    canvas_.onZoneSelected = [this](int zone)
    {
        selectZone(zone);
        syncing_ = true;
        zoneBox_.setSelectedItemIndex(zone, juce::dontSendNotification);
        syncing_ = false;
    };

    for (int i = 0; i < motion::kNumOutputs; ++i)
    {
        outputStrips_[static_cast<std::size_t>(i)] = std::make_unique<OutputStrip>(plugin_, i);
        addAndMakeVisible(*outputStrips_[static_cast<std::size_t>(i)]);
    }

    selectZone(0);
    syncControls();
    updateModelLabels();
    setSize(1320, 820);
    startTimerHz(60);
}

MotionEngineEditor::~MotionEngineEditor()
{
    stopTimer();
}

void MotionEngineEditor::configureSlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.5f));
    slider.setColour(juce::Slider::thumbColourId, text);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void MotionEngineEditor::configureLabel(juce::Label& label, const juce::String& value)
{
    label.setText(value, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centredLeft);
}

void MotionEngineEditor::bindSlider(juce::Slider& slider, clap_id id)
{
    slider.onDragStart = [this, id] { plugin_.beginParameterGesture(id); };
    slider.onValueChange = [this, &slider, id]
    {
        if (!syncing_)
            plugin_.setParameterFromGui(id, slider.getValue());
    };
    slider.onDragEnd = [this, id] { plugin_.endParameterGesture(id); };
}

void MotionEngineEditor::setOneShot(clap_id id, double value)
{
    plugin_.beginParameterGesture(id);
    plugin_.setParameterFromGui(id, value);
    plugin_.endParameterGesture(id);
}

void MotionEngineEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto content = getLocalBounds().toFloat().reduced(10.0f);
    auto right = content.removeFromRight(465.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(right, 12.0f);
}

void MotionEngineEditor::resized()
{
    auto area = getLocalBounds().reduced(14);
    auto header = area.removeFromTop(46);
    titleLabel_.setBounds(header.removeFromLeft(210));
    subtitleLabel_.setBounds(header.removeFromLeft(250).withTrimmedTop(5));

    auto right = area.removeFromRight(455);
    area.removeFromRight(12);
    auto left = area;

    auto controls = left.removeFromTop(178);
    auto firstRow = controls.removeFromTop(36);
    modelBox_.setBounds(firstRow.removeFromLeft(190).reduced(2));
    constraintBox_.setBounds(firstRow.removeFromLeft(160).reduced(2));
    hitButton_.setBounds(firstRow.removeFromLeft(78).reduced(3));
    resetButton_.setBounds(firstRow.removeFromLeft(86).reduced(3));

    auto globalRow = controls.removeFromTop(46);
    const int globalWidth = globalRow.getWidth() / 4;
    std::array<juce::Slider*, 4> globalSliders { &timeSlider_, &energySlider_, &dampingSlider_, &audioKickSlider_ };
    std::array<juce::Label*, 4> globalLabels { &timeLabel_, &energyLabel_, &dampingLabel_, &audioKickLabel_ };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = globalRow.removeFromLeft(globalWidth).reduced(2);
        globalLabels[static_cast<std::size_t>(i)]->setBounds(cell.removeFromTop(17));
        globalSliders[static_cast<std::size_t>(i)]->setBounds(cell);
    }

    auto motionRow = controls.removeFromTop(50);
    const int motionWidth = motionRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = motionRow.removeFromLeft(motionWidth).reduced(2);
        motionLabels_[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(17));
        motionSliders_[static_cast<std::size_t>(i)].setBounds(cell);
    }

    auto zoneRow = controls.removeFromTop(46);
    auto zoneCell = zoneRow.removeFromLeft(140).reduced(2);
    zoneLabel_.setBounds(zoneCell.removeFromTop(17));
    zoneBox_.setBounds(zoneCell);
    auto radiusCell = zoneRow.removeFromLeft(220).reduced(2);
    zoneRadiusLabel_.setBounds(radiusCell.removeFromTop(17));
    zoneRadiusSlider_.setBounds(radiusCell);
    auto falloffCell = zoneRow.removeFromLeft(220).reduced(2);
    zoneFalloffLabel_.setBounds(falloffCell.removeFromTop(17));
    zoneFalloffSlider_.setBounds(falloffCell);

    canvas_.setBounds(left.reduced(0, 2));

    auto rightInner = right.reduced(10);
    outputsTitleLabel_.setBounds(rightInner.removeFromTop(28));
    auto bridgeArea = rightInner.removeFromBottom(44);
    bridgeLabel_.setBounds(bridgeArea);
    const int stripHeight = std::max(62, rightInner.getHeight() / motion::kNumOutputs);
    for (int i = 0; i < motion::kNumOutputs; ++i)
        outputStrips_[static_cast<std::size_t>(i)]->setBounds(rightInner.removeFromTop(stripHeight).reduced(0, 3));
}

void MotionEngineEditor::updateModelLabels()
{
    const int model = plugin_.parameterInt(motion::ids::model);
    displayedModel_ = model;
    const auto names = motion::MotionEngineCore::controlNamesForModel(model);
    for (int i = 0; i < 4; ++i)
        motionLabels_[static_cast<std::size_t>(i)].setText(toJuce(names[static_cast<std::size_t>(i)]), juce::dontSendNotification);
}

void MotionEngineEditor::selectZone(int zone)
{
    selectedZone_ = std::clamp(zone, 0, motion::kNumZones - 1);
    canvas_.setSelectedZone(selectedZone_);
    const auto& id = motion::ids::zones[static_cast<std::size_t>(selectedZone_)];
    syncing_ = true;
    zoneRadiusSlider_.setValue(plugin_.parameterValue(id.radius), juce::dontSendNotification);
    zoneFalloffSlider_.setValue(plugin_.parameterValue(id.falloff), juce::dontSendNotification);
    syncing_ = false;
}

void MotionEngineEditor::syncControls()
{
    syncing_ = true;
    modelBox_.setSelectedItemIndex(plugin_.parameterInt(motion::ids::model), juce::dontSendNotification);
    constraintBox_.setSelectedItemIndex(plugin_.parameterInt(motion::ids::constraint), juce::dontSendNotification);
    timeSlider_.setValue(plugin_.parameterValue(motion::ids::timeScale), juce::dontSendNotification);
    energySlider_.setValue(plugin_.parameterValue(motion::ids::energy), juce::dontSendNotification);
    dampingSlider_.setValue(plugin_.parameterValue(motion::ids::globalDamping), juce::dontSendNotification);
    audioKickSlider_.setValue(plugin_.parameterValue(motion::ids::audioKick), juce::dontSendNotification);
    motionSliders_[0].setValue(plugin_.parameterValue(motion::ids::motionA), juce::dontSendNotification);
    motionSliders_[1].setValue(plugin_.parameterValue(motion::ids::motionB), juce::dontSendNotification);
    motionSliders_[2].setValue(plugin_.parameterValue(motion::ids::motionC), juce::dontSendNotification);
    motionSliders_[3].setValue(plugin_.parameterValue(motion::ids::motionD), juce::dontSendNotification);
    zoneBox_.setSelectedItemIndex(selectedZone_, juce::dontSendNotification);
    const auto& zone = motion::ids::zones[static_cast<std::size_t>(selectedZone_)];
    zoneRadiusSlider_.setValue(plugin_.parameterValue(zone.radius), juce::dontSendNotification);
    zoneFalloffSlider_.setValue(plugin_.parameterValue(zone.falloff), juce::dontSendNotification);
    syncing_ = false;
}

void MotionEngineEditor::timerCallback()
{
    syncControls();
    canvas_.tick();
    const auto snapshot = plugin_.motionCore().getSnapshot();
    const auto bridge = plugin_.bridge().getStatus();

    int mappedCount = 0;
    for (int i = 0; i < motion::kNumOutputs; ++i)
    {
        outputStrips_[static_cast<std::size_t>(i)]->sync(snapshot, bridge.slots[static_cast<std::size_t>(i)]);
        if (bridge.slots[static_cast<std::size_t>(i)].mapped)
            ++mappedCount;
    }

    const int model = plugin_.parameterInt(motion::ids::model);
    if (model != displayedModel_)
        updateModelLabels();

    if (!bridge.bridgeSeen)
    {
        bridgeLabel_.setText("Bitwig bridge not seen | aux CV: Motion 1-8 available", juce::dontSendNotification);
        bridgeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffb08c));
    }
    else if (mappedCount == 0)
    {
        bridgeLabel_.setText("Bitwig bridge online | no mapped outputs | aux CV: Motion 1-8", juce::dontSendNotification);
        bridgeLabel_.setColour(juce::Label::textColourId, muted);
    }
    else
    {
        bridgeLabel_.setText(juce::String::formatted("Bitwig bridge | sent %.0f Hz | applied %.0f Hz | worst %.1f ms | aux CV: Motion 1-8",
                                                     bridge.sentHz, bridge.appliedHz, bridge.worstGapMs),
                             juce::dontSendNotification);
        bridgeLabel_.setColour(juce::Label::textColourId, muted);
    }
}

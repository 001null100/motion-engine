#include "PluginEditor.h"
#include "EditorBindings.h"

#include <algorithm>
#include <cmath>

namespace
{
const auto bg = juce::Colour(0xff0b0f15);
const auto bgRaised = juce::Colour(0xff101621);
const auto panel = juce::Colour(0xff151c27);
const auto panelRaised = juce::Colour(0xff1b2432);
const auto panelEdge = juce::Colour(0xff2d394a);
const auto text = juce::Colour(0xffedf4ff);
const auto muted = juce::Colour(0xff91a0b5);
const auto accent = juce::Colour(0xff78ddff);
const auto accent2 = juce::Colour(0xffff79d4);
const auto warm = juce::Colour(0xffffcf73);

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

void styleCombo(juce::ComboBox& box)
{
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111925));
    box.setColour(juce::ComboBox::textColourId, text);
    box.setColour(juce::ComboBox::outlineColourId, panelEdge);
    box.setColour(juce::ComboBox::arrowColourId, accent.withAlpha(0.9f));
    box.setColour(juce::ComboBox::focusedOutlineColourId, accent.withAlpha(0.75f));
    box.setJustificationType(juce::Justification::centredLeft);
}

void styleButton(juce::TextButton& button, juce::Colour colour)
{
    button.setColour(juce::TextButton::buttonColourId, colour.withAlpha(0.16f));
    button.setColour(juce::TextButton::buttonOnColourId, colour.withAlpha(0.32f));
    button.setColour(juce::TextButton::textColourOffId, text);
    button.setColour(juce::TextButton::textColourOnId, text);
}
} // namespace

//==============================================================================
MotionCanvas::MotionCanvas(MotionEnginePlugin& plugin) : plugin_(plugin)
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus(true);
    setTitle("Motion canvas");
    trail_.reserve(37);
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
    g.setColour(panelEdge.withAlpha(0.72f));
    g.drawRoundedRectangle(area.reduced(0.5f), 12.0f, 1.0f);

    const auto world = worldBounds();
    juce::ColourGradient worldGradient(juce::Colour(0xff111925), world.getX(), world.getY(),
                                       juce::Colour(0xff0c1119), world.getRight(), world.getBottom(), false);
    g.setGradientFill(worldGradient);
    g.fillRoundedRectangle(world, 9.0f);

    g.saveState();
    g.reduceClipRegion(world.toNearestInt());

    g.setColour(juce::Colour(0xff202b39));
    for (int i = 1; i < 8; ++i)
    {
        const float t = static_cast<float>(i) / 8.0f;
        g.drawVerticalLine(static_cast<int>(world.getX() + world.getWidth() * t), world.getY(), world.getBottom());
        g.drawHorizontalLine(static_cast<int>(world.getY() + world.getHeight() * t), world.getX(), world.getRight());
    }

    const auto centre = worldToScreen(0.0f, 0.0f);
    g.setColour(juce::Colour(0xff3a485c).withAlpha(0.72f));
    g.drawLine(centre.x, world.getY(), centre.x, world.getBottom(), 1.2f);
    g.drawLine(world.getX(), centre.y, world.getRight(), centre.y, 1.2f);

    const int constraint = plugin_.effectiveParameterInt(motion::ids::constraint);
    g.setColour(accent.withAlpha(0.25f));
    if (constraint == 1)
    {
        g.drawLine(world.getX(), centre.y, world.getRight(), centre.y, 2.2f);
    }
    else if (constraint == 2)
    {
        g.drawLine(centre.x, world.getY(), centre.x, world.getBottom(), 2.2f);
    }
    else if (constraint == 3)
    {
        const auto a = worldToScreen(-1.0f, -1.0f);
        const auto b = worldToScreen(1.0f, 1.0f);
        g.drawLine(a.x, a.y, b.x, b.y, 2.2f);
    }
    else if (constraint == 4)
    {
        const float r = worldRadiusToPixels(0.72f);
        g.drawEllipse({ centre.x - r, centre.y - r, r * 2.0f, r * 2.0f }, 2.2f);
    }

    const auto snapshot = plugin_.motionCore().getSnapshot();
    const int model = plugin_.effectiveParameterInt(motion::ids::model);

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(zone)];
        const float zx = static_cast<float>(plugin_.effectiveParameterValue(id.x));
        const float zy = static_cast<float>(plugin_.effectiveParameterValue(id.y));
        const float radius = static_cast<float>(plugin_.effectiveParameterValue(id.radius));
        const float falloff = std::max(0.1f, static_cast<float>(plugin_.effectiveParameterValue(id.falloff)));
        const auto center = worldToScreen(zx, zy);
        const float pixels = worldRadiusToPixels(radius);
        const auto ellipse = juce::Rectangle<float>(center.x - pixels, center.y - pixels, pixels * 2.0f, pixels * 2.0f);
        const auto colour = zoneColour(zone);
        const float amount = snapshot.zones[static_cast<std::size_t>(zone)];

        g.setColour(colour.withAlpha(0.035f + amount * 0.14f));
        g.fillEllipse(ellipse);
        constexpr std::array<float, 3> responseLevels { 0.75f, 0.50f, 0.25f };
        for (std::size_t level = 0; level < responseLevels.size(); ++level)
        {
            const float ratio = 1.0f - std::pow(responseLevels[level], 1.0f / falloff);
            const float ringRadius = pixels * ratio;
            const auto ring = juce::Rectangle<float>(center.x - ringRadius, center.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
            g.setColour(colour.withAlpha(0.13f + static_cast<float>(level) * 0.035f));
            g.drawEllipse(ring, 1.0f);
        }
        g.setColour(colour.withAlpha(zone == selectedZone_ ? 0.95f : 0.42f));
        g.drawEllipse(ellipse, zone == selectedZone_ ? 2.0f : 1.0f);
        g.fillEllipse({ center.x - 4.0f, center.y - 4.0f, 8.0f, 8.0f });
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("Z" + juce::String(zone + 1), static_cast<int>(center.x + 8.0f), static_cast<int>(center.y - 18.0f), 30, 18,
                   juce::Justification::centredLeft);
    }

    if (model == 1)
    {
        const float anchorX = (static_cast<float>(plugin_.effectiveParameterValue(motion::ids::motionD)) - 0.5f) * 0.9f;
        const auto anchor = worldToScreen(anchorX, 0.0f);
        const auto body = worldToScreen(snapshot.x, snapshot.y);
        g.setColour(accent.withAlpha(0.42f));
        g.drawLine(anchor.x, anchor.y, body.x, body.y, 1.5f);
        g.drawEllipse({ anchor.x - 5.0f, anchor.y - 5.0f, 10.0f, 10.0f }, 1.5f);
    }
    else if (model == 2)
    {
        const auto pivot = worldToScreen(0.0f, 0.12f);
        const auto body = worldToScreen(snapshot.x, snapshot.y);
        g.setColour(accent.withAlpha(0.45f));
        g.drawLine(pivot.x, pivot.y, body.x, body.y, 1.7f);
        g.fillEllipse({ pivot.x - 4.0f, pivot.y - 4.0f, 8.0f, 8.0f });
    }

    if (trail_.size() > 1)
    {
        for (std::size_t i = 1; i < trail_.size(); ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(trail_.size() - 1);
            g.setColour(accent.withAlpha(0.025f + 0.62f * t * t));
            const auto previous=worldToScreen(trail_[i-1].x,trail_[i-1].y);
            const auto current=worldToScreen(trail_[i].x,trail_[i].y);
            g.drawLine(previous.x,previous.y,current.x,current.y,0.8f+1.8f*t);
        }
    }

    const auto body = worldToScreen(snapshot.x, snapshot.y);
    const auto velocityEnd = body + juce::Point<float>(snapshot.vx, -snapshot.vy) * 18.0f;
    g.setColour(warm.withAlpha(0.68f));
    g.drawLine(body.x, body.y, velocityEnd.x, velocityEnd.y, 1.5f);

    const auto bodyAccent = accent.interpolatedWith(accent2, std::clamp(snapshot.impact, 0.0f, 1.0f));
    const float bodyRadius = 10.0f + snapshot.energy * 4.0f;
    g.setColour(bodyAccent.withAlpha(0.13f + snapshot.impact * 0.16f));
    g.fillEllipse({ body.x - bodyRadius - 8.0f, body.y - bodyRadius - 8.0f,
                    (bodyRadius + 8.0f) * 2.0f, (bodyRadius + 8.0f) * 2.0f });
    g.setColour(text);
    g.fillEllipse({ body.x - bodyRadius, body.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f });
    g.setColour(bodyAccent);
    g.drawEllipse({ body.x - bodyRadius, body.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f }, 2.0f);

    g.restoreState();
    g.setColour(panelEdge);
    g.drawRoundedRectangle(world, 9.0f, 1.2f);

    auto help = getLocalBounds().reduced(18).removeFromBottom(18);
    g.setColour(muted);
    g.setFont(juce::FontOptions(11.5f));
    const juce::String hint = (model == 7 || model == 8)
        ? "Drag body: throw | Zones: drag center/ring | Double-click or HIT: retrigger model"
        : "Drag body: throw | Zones: drag center/ring | Double-click: HIT";
    g.drawText(hint, help, juce::Justification::centredLeft);
}

void MotionCanvas::tick()
{
    const auto snapshot = plugin_.motionCore().getSnapshot();
    const int model=plugin_.effectiveParameterInt(motion::ids::model);
    const int constraint=plugin_.effectiveParameterInt(motion::ids::constraint);
    if(model!=trailModel_||constraint!=trailConstraint_) { trail_.clear();trailModel_=model;trailConstraint_=constraint; }
    const juce::Point<float> point { snapshot.x, snapshot.y };
    if (trail_.empty() || trail_.back().getDistanceFrom(point) > 1.5f/worldBounds().getWidth())
        trail_.push_back(point);
    constexpr std::size_t maxTrailPoints = 36;
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
    plugin_.setUiValue(id, value);
}

void MotionCanvas::beginZoneGesture()
{
    if (dragZone_ < 0)
        return;
    const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
    if (dragMode_ == DragMode::zoneMove)
    {
        plugin_.beginUiEdit(id.x);
        plugin_.beginUiEdit(id.y);
    }
    else if (dragMode_ == DragMode::zoneRadius)
    {
        plugin_.beginUiEdit(id.radius);
    }
}

void MotionCanvas::endZoneGesture()
{
    if (dragZone_ < 0)
        return;
    const auto& id = motion::ids::zones[static_cast<std::size_t>(dragZone_)];
    if (dragMode_ == DragMode::zoneMove)
    {
        plugin_.endUiEdit(id.x);
        plugin_.endUiEdit(id.y);
    }
    else if (dragMode_ == DragMode::zoneRadius)
    {
        plugin_.endUiEdit(id.radius);
    }
}

void MotionCanvas::mouseDown(const juce::MouseEvent& event)
{
    if(!event.mods.isLeftButtonDown() || !worldBounds().contains(event.position)) return;
    cancelInteraction();
    grabKeyboardFocus();
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

void MotionCanvas::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::body)
    {
        const auto release=screenToWorld(event.position);
        plugin_.motionCore().dragTo(release.x,release.y);
        // A flick followed by a stationary hold is a placement, not a stale throw.
        const auto idle=juce::Time::getMillisecondCounterHiRes()-lastDragTimeMs_;
        const float decay=idle>150.0?0.0f:static_cast<float>(std::exp(-std::max(0.0,idle)/60.0));
        plugin_.motionCore().endDrag(flickVelocity_.x*decay,flickVelocity_.y*decay);
    }
    else
        endZoneGesture();
    dragMode_ = DragMode::none;
    dragZone_ = -1;
}

void MotionCanvas::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown()) return;
    cancelInteraction();
    plugin_.hitFromUi();
}

MotionCanvas::~MotionCanvas() { cancelInteraction(); }
void MotionCanvas::cancelInteraction()
{
    if(dragMode_==DragMode::body) plugin_.motionCore().endDrag(0,0);
    else endZoneGesture();
    dragMode_=DragMode::none; dragZone_=-1; flickVelocity_={};
}

//==============================================================================
OutputStrip::OutputStrip(MotionEnginePlugin& plugin, int index) : plugin_(plugin), index_(index)
{
    indexLabel_.setText(juce::String(index_ + 1), juce::dontSendNotification);
    indexLabel_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    indexLabel_.setColour(juce::Label::textColourId, accent);
    indexLabel_.setJustificationType(juce::Justification::centred);

    styleCombo(sourceBox_);
    styleCombo(curveBox_);
    auxLabel_.setText("AUX " + juce::String(index_ + 1),juce::dontSendNotification);
    auxLabel_.setJustificationType(juce::Justification::centred);
    auxLabel_.setColour(juce::Label::textColourId,accent);
    auxLabel_.setFont(juce::FontOptions(11.0f,juce::Font::bold));
    auxLabel_.setInterceptsMouseClicks(false,false);
    styleButton(invertButton_,accent2);
    invertButton_.setWantsKeyboardFocus(false);
    invertButton_.setTooltip("Swap minimum and maximum to invert this output's range.");

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
                             static_cast<juce::Component*>(&auxLabel_), static_cast<juce::Component*>(&invertButton_),
                             static_cast<juce::Component*>(&targetLabel_) })
        addAndMakeVisible(component);

    const auto& id = motion::ids::outputs[static_cast<std::size_t>(index_)];
    sourceBox_.onChange = [this, source = id.source]
    {
        sourceBox_.armSyncHold();
        if (!syncing_ && sourceBox_.getSelectedId() > 0)
            setOneShot(source, sourceBox_.getSelectedItemIndex());
    };
    curveBox_.onChange = [this, curve = id.curve]
    {
        curveBox_.armSyncHold();
        if (!syncing_ && curveBox_.getSelectedId() > 0)
            setOneShot(curve, curveBox_.getSelectedItemIndex());
    };
    bindSlider(minSlider_, id.minimum);
    bindSlider(maxSlider_, id.maximum);
    bindSlider(smoothSlider_, id.smoothing);

    const auto title="Output "+juce::String(index_+1);
    sourceBox_.setTitle(title+" source");curveBox_.setTitle(title+" curve");
    minSlider_.setTitle(title+" minimum");maxSlider_.setTitle(title+" maximum");smoothSlider_.setTitle(title+" smoothing");
    motion::ui::percent(minSlider_);motion::ui::percent(maxSlider_);
    minSlider_.setTooltip("Minimum output percentage. Minimum may exceed maximum for an inverted range.");
    maxSlider_.setTooltip("Maximum output percentage. CLAP values remain normalized 0..1.");
    smoothSlider_.setTooltip("Output smoothing in milliseconds, independent of the DAW buffer size. Double-click resets.");
    sourceBox_.setTooltip("Choose a motion or audio source for this auxiliary output.");
    curveBox_.setTooltip("Shape the source before range and smoothing are applied.");
    auxLabel_.setTooltip("Use Motion "+juce::String(index_+1)+" as the Bitwig Audio Rate source. Rectify off. Do not monitor CV as audio.");
    invertButton_.onClick=[this,id]{
        motion::ui::finish(minSlider_);motion::ui::finish(maxSlider_);
        const auto lo=plugin_.parameterValue(id.minimum),hi=plugin_.parameterValue(id.maximum);
        setOneShot(id.minimum,hi);setOneShot(id.maximum,lo);
    };
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
    plugin_.beginUiEdit(id);
    plugin_.setUiValue(id, value);
    plugin_.endUiEdit(id);
}

void OutputStrip::bindSlider(juce::Slider& slider, clap_id id)
{
    motion::ui::bind(plugin_,slider,[id]{return id;},syncing_);
}
OutputStrip::~OutputStrip()
{
    for(auto* slider:{&minSlider_,&maxSlider_,&smoothSlider_})motion::ui::detach(*slider);
}

void OutputStrip::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(panelRaised);
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(panelEdge.withAlpha(0.82f));
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
    auto top = area.removeFromTop(24);
    sourceBox_.setBounds(top.removeFromLeft(142).reduced(2, 1));
    minSlider_.setBounds(top.removeFromLeft(66).reduced(2, 1));
    maxSlider_.setBounds(top.removeFromLeft(66).reduced(2, 1));
    auxLabel_.setBounds(top.removeFromRight(55).reduced(1));
    invertButton_.setBounds(top.removeFromRight(40).reduced(1));
    auto bottom = area.removeFromBottom(23);
    curveBox_.setBounds(bottom.removeFromLeft(108).reduced(2, 1));
    smoothSlider_.setBounds(bottom.removeFromLeft(122).reduced(2, 1));
    targetLabel_.setBounds(bottom.reduced(5, 0));
}

void OutputStrip::sync(const motion::MotionEngineCore::Snapshot& snapshot)
{
    const auto& id = motion::ids::outputs[static_cast<std::size_t>(index_)];
    syncing_ = true;

    const int sourceIndex = plugin_.parameterInt(id.source);
    if (sourceBox_.canAcceptExternalSync() && sourceBox_.getSelectedItemIndex() != sourceIndex)
        sourceBox_.setSelectedItemIndex(sourceIndex, juce::dontSendNotification);

    motion::ui::syncSlider(minSlider_, plugin_.parameterValue(id.minimum));
    motion::ui::syncSlider(maxSlider_, plugin_.parameterValue(id.maximum));

    const int curveIndex = plugin_.parameterInt(id.curve);
    if (curveBox_.canAcceptExternalSync() && curveBox_.getSelectedItemIndex() != curveIndex)
        curveBox_.setSelectedItemIndex(curveIndex, juce::dontSendNotification);

    motion::ui::syncSlider(smoothSlider_, plugin_.parameterValue(id.smoothing));
    syncing_ = false;

    currentValue_ = snapshot.outputs[static_cast<std::size_t>(index_)];
    const auto bipolar=currentValue_*2.0f-1.0f;
    targetLabel_.setText("CV " + juce::String(bipolar>=0?"+":"") + juce::String(bipolar,3)
        + (plugin_.parameterValue(id.minimum)>plugin_.parameterValue(id.maximum)?"  INV":""),juce::dontSendNotification);
    targetLabel_.setColour(juce::Label::textColourId, muted);

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

    betaLabel_.setText("0.2.1", juce::dontSendNotification);
    betaLabel_.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    betaLabel_.setJustificationType(juce::Justification::centred);
    betaLabel_.setColour(juce::Label::textColourId, accent);
    betaLabel_.setColour(juce::Label::backgroundColourId, accent.withAlpha(0.11f));
    betaLabel_.setColour(juce::Label::outlineColourId, accent.withAlpha(0.35f));

    subtitleLabel_.setText("physics-driven modulation", juce::dontSendNotification);
    subtitleLabel_.setFont(juce::FontOptions(13.0f));
    subtitleLabel_.setColour(juce::Label::textColourId, muted);

    modelInfoLabel_.setFont(juce::FontOptions(11.5f));
    modelInfoLabel_.setColour(juce::Label::textColourId, muted.brighter(0.12f));
    modelInfoLabel_.setJustificationType(juce::Justification::centredRight);
    modelInfoLabel_.setMinimumHorizontalScale(0.72f);

    outputsTitleLabel_.setText("MOTION OUTPUTS  ·  SAFE AUX ROUTING", juce::dontSendNotification);
    outputsTitleLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    outputsTitleLabel_.setColour(juce::Label::textColourId, text);

    bridgeLabel_.setFont(juce::FontOptions(11.5f));
    bridgeLabel_.setColour(juce::Label::textColourId, muted);
    bridgeLabel_.setMinimumHorizontalScale(0.72f);

    styleCombo(modelBox_);
    styleCombo(constraintBox_);
    styleCombo(zoneBox_);
    styleButton(hitButton_, accent2);
    styleButton(resetButton_, accent);

    int item = 1;
    for (const auto name : motion::MotionEngineCore::modelNames())
        modelBox_.addItem(toJuce(name), item++);
    item = 1;
    for (const auto name : motion::MotionEngineCore::constraintNames())
        constraintBox_.addItem(toJuce(name), item++);
    for (int zone = 0; zone < motion::kNumZones; ++zone)
        zoneBox_.addItem("Zone " + juce::String(zone + 1), zone + 1);

    for (auto* component : { static_cast<juce::Component*>(&titleLabel_), static_cast<juce::Component*>(&betaLabel_),
                             static_cast<juce::Component*>(&subtitleLabel_), static_cast<juce::Component*>(&modelInfoLabel_),
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
        modelBox_.armSyncHold();
        if (!syncing_ && modelBox_.getSelectedId() > 0)
        {
            setOneShot(motion::ids::model, modelBox_.getSelectedItemIndex());
            updateModelLabels();
        }
    };
    constraintBox_.onChange = [this]
    {
        constraintBox_.armSyncHold();
        if (!syncing_ && constraintBox_.getSelectedId() > 0)
            setOneShot(motion::ids::constraint, constraintBox_.getSelectedItemIndex());
    };
    zoneBox_.onChange = [this]
    {
        zoneBox_.armSyncHold();
        if (!syncing_ && zoneBox_.getSelectedId() > 0)
            selectZone(zoneBox_.getSelectedItemIndex());
    };

    motion::ui::bind(plugin_,zoneRadiusSlider_,[this]{return motion::ids::zones[static_cast<std::size_t>(selectedZone_)].radius;},syncing_);
    motion::ui::bind(plugin_,zoneFalloffSlider_,[this]{return motion::ids::zones[static_cast<std::size_t>(selectedZone_)].falloff;},syncing_);
    hitButton_.onClick = [this] { plugin_.hitFromUi(); };
    resetButton_.onClick = [this] { canvas_.cancelInteraction();canvas_.clearTrail();plugin_.resetMotionFromUi(); };
    hitButton_.setWantsKeyboardFocus(false);resetButton_.setWantsKeyboardFocus(false);
    hitButton_.setTooltip("Disturb/retrigger the current model. MIDI note-ons also HIT. Shortcuts: H or Enter outside value editing.");
    resetButton_.setTooltip("Reset motion and audio analysis to a deterministic starting state. Keep all settings and output routes.");
    modelBox_.setTitle("Motion model");constraintBox_.setTitle("Motion constraint");zoneBox_.setTitle("Selected zone");
    modelBox_.setTooltip("Select a motion model. Embedded selectors also support keyboard navigation.");
    constraintBox_.setTooltip("Project the output position/velocity without destroying the underlying motion.");
    zoneRadiusSlider_.setTitle("Selected zone radius");zoneFalloffSlider_.setTitle("Selected zone falloff");
    zoneRadiusSlider_.setTooltip("Zone radius in world units. Drag the zone ring for direct editing.");
    zoneFalloffSlider_.setTooltip("Response falloff from the zone center to its boundary.");
    timeSlider_.setTitle("Time scale");energySlider_.setTitle("Energy");dampingSlider_.setTitle("World drag");audioKickSlider_.setTitle("Audio kick");
    timeSlider_.setTooltip("Simulation speed multiplier. The 240 Hz control clock remains fixed.");
    energySlider_.setTooltip("Model drive and hit strength. Double-click restores the default.");
    dampingSlider_.setTooltip("Global drag. Orbit/Lissajous retain their reference routes; drag damps physical deviations.");
    audioKickSlider_.setTooltip("Input transients disturb the body. Set to zero for undisturbed deterministic routes.");
    bridgeLabel_.setTooltip("Expose Motion 1-8 and select a lane as the source of Bitwig Audio Rate. Rectify off; then map the modulator. The bridge is optional and never maps targets.");
    for(std::size_t i=0;i<motionSliders_.size();++i){
        motionSliders_[i].setTitle("Model control "+juce::String(static_cast<int>(i+1)));
        motion::ui::percent(motionSliders_[i]);
        motionSliders_[i].setTooltip("Model-specific control percentage. Double-click restores the registered default.");
    }
    canvas_.onZoneSelected = [this](int zone)
    {
        selectZone(zone);
        syncing_ = true;
        zoneBox_.setSelectedItemIndex(zone, juce::dontSendNotification);
        zoneBox_.armSyncHold(120.0);
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
    stopTimer();canvas_.cancelInteraction();
    for(auto* slider:{&timeSlider_,&energySlider_,&dampingSlider_,&audioKickSlider_,&zoneRadiusSlider_,&zoneFalloffSlider_})motion::ui::detach(*slider);
    for(auto& slider:motionSliders_)motion::ui::detach(slider);
}
void MotionEngineEditor::visibilityChanged(){if(!isVisible())canvas_.cancelInteraction();}
void MotionEngineEditor::focusLost(FocusChangeType){if(!hasKeyboardFocus(true))canvas_.cancelInteraction();}
void MotionEngineEditor::focusOfChildComponentChanged(FocusChangeType){if(!hasKeyboardFocus(true))canvas_.cancelInteraction();}
bool MotionEngineEditor::keyPressed(const juce::KeyPress& key)
{
    if(motion::ui::hasTextEditor(*this))return false;
    if(key==juce::KeyPress::escapeKey){canvas_.cancelInteraction();return true;}
    if(key.getModifiers().isAnyModifierKeyDown())return false;
    if(key.getKeyCode()=='H'||key.getKeyCode()==juce::KeyPress::returnKey){plugin_.hitFromUi();return true;}
    return false;
}

void MotionEngineEditor::configureSlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff111823));
    slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.48f));
    slider.setColour(juce::Slider::thumbColourId, text);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff111823));
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
    motion::ui::bind(plugin_,slider,[id]{return id;},syncing_);
}

void MotionEngineEditor::setOneShot(clap_id id, double value)
{
    plugin_.beginUiEdit(id);
    plugin_.setUiValue(id, value);
    plugin_.endUiEdit(id);
}

void MotionEngineEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient background(bgRaised, 0.0f, 0.0f, bg, 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(background);
    g.fillRect(getLocalBounds());

    auto layout = getLocalBounds().toFloat().reduced(10.0f);
    auto right = layout.removeFromRight(465.0f);
    auto left = layout;

    g.setColour(panel.withAlpha(0.94f));
    g.fillRoundedRectangle(right, 12.0f);
    g.setColour(panelEdge.withAlpha(0.8f));
    g.drawRoundedRectangle(right.reduced(0.5f), 12.0f, 1.0f);

    left.removeFromTop(50.0f);
    auto controls = left.removeFromTop(182.0f);
    g.setColour(panel.withAlpha(0.72f));
    g.fillRoundedRectangle(controls, 10.0f);
    g.setColour(panelEdge.withAlpha(0.55f));
    g.drawRoundedRectangle(controls.reduced(0.5f), 10.0f, 1.0f);
}

void MotionEngineEditor::resized()
{
    auto area = getLocalBounds().reduced(14);
    auto header = area.removeFromTop(50);
    titleLabel_.setBounds(header.removeFromLeft(205));
    betaLabel_.setBounds(header.removeFromLeft(48).reduced(2, 11));
    subtitleLabel_.setBounds(header.removeFromLeft(175).withTrimmedTop(5));
    modelInfoLabel_.setBounds(header.reduced(4, 3));

    auto right = area.removeFromRight(455);
    area.removeFromRight(12);
    auto left = area;

    auto controls = left.removeFromTop(182).reduced(8, 3);
    auto firstRow = controls.removeFromTop(38);
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
    const int zoneWidth=zoneRow.getWidth()/2;
    auto radiusCell = zoneRow.removeFromLeft(zoneWidth).reduced(2);
    zoneRadiusLabel_.setBounds(radiusCell.removeFromTop(17));
    zoneRadiusSlider_.setBounds(radiusCell);
    auto falloffCell = zoneRow.reduced(2);
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

    const auto descriptions = motion::MotionEngineCore::modelDescriptions();
    const auto models = motion::MotionEngineCore::modelNames();
    const int safeModel = std::clamp(model, 0, static_cast<int>(models.size()) - 1);
    modelInfoLabel_.setText(toJuce(models[static_cast<std::size_t>(safeModel)]) + "  ·  "
                            + toJuce(descriptions[static_cast<std::size_t>(safeModel)]),
                            juce::dontSendNotification);
}

void MotionEngineEditor::selectZone(int zone)
{
    if(zone!=selectedZone_){motion::ui::finish(zoneRadiusSlider_);motion::ui::finish(zoneFalloffSlider_);}
    selectedZone_ = std::clamp(zone, 0, motion::kNumZones - 1);
    canvas_.setSelectedZone(selectedZone_);
    const auto& id = motion::ids::zones[static_cast<std::size_t>(selectedZone_)];
    syncing_ = true;
    motion::ui::syncSlider(zoneRadiusSlider_, plugin_.parameterValue(id.radius));
    motion::ui::syncSlider(zoneFalloffSlider_, plugin_.parameterValue(id.falloff));
    syncing_ = false;
}

void MotionEngineEditor::syncControls()
{
    syncing_ = true;

    const int modelIndex = plugin_.parameterInt(motion::ids::model);
    if (modelBox_.canAcceptExternalSync() && modelBox_.getSelectedItemIndex() != modelIndex)
        modelBox_.setSelectedItemIndex(modelIndex, juce::dontSendNotification);

    const int constraintIndex = plugin_.parameterInt(motion::ids::constraint);
    if (constraintBox_.canAcceptExternalSync() && constraintBox_.getSelectedItemIndex() != constraintIndex)
        constraintBox_.setSelectedItemIndex(constraintIndex, juce::dontSendNotification);

    motion::ui::syncSlider(timeSlider_, plugin_.parameterValue(motion::ids::timeScale));
    motion::ui::syncSlider(energySlider_, plugin_.parameterValue(motion::ids::energy));
    motion::ui::syncSlider(dampingSlider_, plugin_.parameterValue(motion::ids::globalDamping));
    motion::ui::syncSlider(audioKickSlider_, plugin_.parameterValue(motion::ids::audioKick));
    motion::ui::syncSlider(motionSliders_[0], plugin_.parameterValue(motion::ids::motionA));
    motion::ui::syncSlider(motionSliders_[1], plugin_.parameterValue(motion::ids::motionB));
    motion::ui::syncSlider(motionSliders_[2], plugin_.parameterValue(motion::ids::motionC));
    motion::ui::syncSlider(motionSliders_[3], plugin_.parameterValue(motion::ids::motionD));

    if (zoneBox_.canAcceptExternalSync() && zoneBox_.getSelectedItemIndex() != selectedZone_)
        zoneBox_.setSelectedItemIndex(selectedZone_, juce::dontSendNotification);

    const auto& zone = motion::ids::zones[static_cast<std::size_t>(selectedZone_)];
    motion::ui::syncSlider(zoneRadiusSlider_, plugin_.parameterValue(zone.radius));
    motion::ui::syncSlider(zoneFalloffSlider_, plugin_.parameterValue(zone.falloff));
    syncing_ = false;
}

void MotionEngineEditor::timerCallback() { refreshFromPlugin(); }
void MotionEngineEditor::refreshFromPlugin()
{
    plugin_.serviceUiEdits();
    syncControls();canvas_.tick();
    const auto snapshot=plugin_.motionCore().getSnapshot();
    for(auto& strip:outputStrips_)strip->sync(snapshot);
    if(plugin_.parameterInt(motion::ids::model)!=displayedModel_)updateModelLabels();
    bridgeLabel_.setText("Motion 1-8 -> Audio Rate / bridge optional\n"+toJuce(plugin_.midiActivityText()),juce::dontSendNotification);
    bridgeLabel_.setColour(juce::Label::textColourId,muted);
    if(!isShowing())canvas_.cancelInteraction();
}

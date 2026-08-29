#pragma once

#include "MotionEnginePlugin.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// JUCE's normal ComboBox popup may live in a separate popup window. That is a bad
// fit for some embedded plug-in hosts: the host/native focus transition can consume
// the first selection click before JUCE commits it. StableComboBox deliberately
// bypasses ComboBox::showPopup() and asks PopupMenu to render inside the plug-in's
// own top-level component instead. There is no desktop/native popup window involved.
class StableComboBox final : public juce::ComboBox
{
public:
    StableComboBox()
    {
        setInterceptsMouseClicks(true, false);
    }

    void armSyncHold(double milliseconds = 0.0) noexcept
    {
        syncHoldUntilMs_ = std::max(syncHoldUntilMs_, juce::Time::getMillisecondCounterHiRes() + milliseconds);
    }

    bool canAcceptExternalSync() const noexcept
    {
        return !popupOpen_ && juce::Time::getMillisecondCounterHiRes() >= syncHoldUntilMs_;
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        showEmbeddedPopup();
    }

    void mouseUp(const juce::MouseEvent&) override {}
    void mouseDoubleClick(const juce::MouseEvent&) override {}

private:
    void showEmbeddedPopup()
    {
        if (popupOpen_ || !isEnabled() || getNumItems() <= 0)
            return;

        popupOpen_ = true;
        juce::PopupMenu menu;
        const int selected = getSelectedId();

        for (int index = 0; index < getNumItems(); ++index)
        {
            const int id = getItemId(index);
            if (id > 0)
                menu.addItem(id, getItemText(index), true, id == selected);
        }

        auto* parent = getTopLevelComponent();
        auto options = juce::PopupMenu::Options()
            .withTargetComponent(this)
            .withParentComponent(parent)
            .withMinimumWidth(getWidth());

        juce::Component::SafePointer<StableComboBox> safeThis(this);
        menu.showMenuAsync(options, [safeThis](int result)
        {
            if (safeThis == nullptr)
                return;

            safeThis->popupOpen_ = false;
            if (result > 0 && result != safeThis->getSelectedId())
                safeThis->setSelectedId(result, juce::sendNotificationSync);
        });
    }

    bool popupOpen_ = false;
    double syncHoldUntilMs_ = 0.0;
};

// Deterministic route preview. Orbit and Lissajous are simulated using the same
// integration equations as the audio-thread models instead of drawing their ideal
// attractors. This matters because inertia, world drag and world-bound collisions
// can move the body noticeably away from the mathematical target curve.
class RoutePreviewOverlay final : public juce::Component,
                                  private juce::ComponentListener
{
public:
    RoutePreviewOverlay(juce::Component& parent, MotionEnginePlugin& plugin)
        : parent_(parent), plugin_(plugin)
    {
        setInterceptsMouseClicks(false, false);
        parent_.addAndMakeVisible(*this);
        parent_.addComponentListener(this);
        setBounds(parent_.getLocalBounds());
    }

    ~RoutePreviewOverlay() override
    {
        parent_.removeComponentListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        const int model = plugin_.parameterInt(motion::ids::model);
        if (model != 0 && model != 6 && model != 7)
            return;

        const auto world = worldBounds();
        if (world.isEmpty())
            return;

        g.saveState();
        g.reduceClipRegion(world.toNearestInt());

        if (model == 0)
            paintOrbit(g, world);
        else if (model == 6)
            paintLissajous(g, world);
        else
            paintImpulse(g, world);

        // The route component is a child of MotionCanvas, so JUCE paints it after
        // the parent. Redraw the live body here so the route always reads as being
        // underneath the body rather than slicing through it.
        paintLiveBody(g, world);
        g.restoreState();
    }

private:
    struct SimState
    {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
    };

    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override
    {
        if (&component == &parent_ && wasResized)
            setBounds(parent_.getLocalBounds());
    }

    juce::Rectangle<float> worldBounds() const
    {
        auto available = getLocalBounds().toFloat().reduced(14.0f, 14.0f);
        available.removeFromBottom(24.0f);
        const float side = std::max(1.0f, std::min(available.getWidth(), available.getHeight()));
        return { available.getCentreX() - side * 0.5f,
                 available.getCentreY() - side * 0.5f,
                 side, side };
    }

    juce::Point<float> project(float x, float y) const
    {
        switch (plugin_.parameterInt(motion::ids::constraint))
        {
            case 1:
                y = 0.0f;
                break;
            case 2:
                x = 0.0f;
                break;
            case 3:
            {
                const float p = (x + y) * 0.5f;
                x = y = p;
                break;
            }
            case 4:
            {
                constexpr float radius = 0.72f;
                const float r = std::hypot(x, y);
                if (r > 1.0e-5f)
                {
                    x = x / r * radius;
                    y = y / r * radius;
                }
                else
                {
                    x = radius;
                    y = 0.0f;
                }
                break;
            }
            default:
                break;
        }
        return { x, y };
    }

    juce::Point<float> toScreen(juce::Rectangle<float> world, float x, float y) const
    {
        const auto p = project(x, y);
        return { juce::jmap(p.x, -1.0f, 1.0f, world.getX(), world.getRight()),
                 juce::jmap(p.y, -1.0f, 1.0f, world.getBottom(), world.getY()) };
    }

    static juce::Path pathFrom(const std::vector<juce::Point<float>>& points)
    {
        juce::Path path;
        if (points.empty())
            return path;
        path.startNewSubPath(points.front());
        for (std::size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);
        return path;
    }

    static void contain(SimState& state, bool bounce, double restitution)
    {
        auto collide = [bounce, restitution](double& position, double& velocity)
        {
            if (position >= -1.0 && position <= 1.0)
                return;
            position = std::clamp(position, -1.0, 1.0);
            velocity *= bounce ? -restitution : -0.15;
        };
        collide(state.x, state.vx);
        collide(state.y, state.vy);
    }

    void applyWorldDrag(SimState& state, double dt) const
    {
        const double drag = std::clamp(plugin_.parameterValue(motion::ids::globalDamping), 0.0, 4.0);
        const double factor = std::exp(-drag * dt);
        state.vx *= factor;
        state.vy *= factor;
    }

    void drawArrow(juce::Graphics& g,
                   juce::Point<float> from,
                   juce::Point<float> to,
                   juce::Colour colour,
                   float size = 7.0f) const
    {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::hypot(dx, dy);
        if (length < 2.0f)
            return;

        const float ux = dx / length;
        const float uy = dy / length;
        const float px = -uy;
        const float py = ux;
        const juce::Point<float> base { to.x - ux * size, to.y - uy * size };
        const juce::Point<float> left { base.x + px * size * 0.52f, base.y + py * size * 0.52f };
        const juce::Point<float> right { base.x - px * size * 0.52f, base.y - py * size * 0.52f };

        g.setColour(colour);
        g.drawLine(to.x, to.y, left.x, left.y, 1.7f);
        g.drawLine(to.x, to.y, right.x, right.y, 1.7f);
    }

    void drawRoute(juce::Graphics& g,
                   const std::vector<juce::Point<float>>& points,
                   juce::Colour colour,
                   bool closed,
                   int arrowCount) const
    {
        if (points.size() < 3)
            return;

        auto route = pathFrom(points);
        if (closed)
            route.closeSubPath();

        g.setColour(colour.withAlpha(0.12f));
        g.strokePath(route, juce::PathStrokeType(7.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour.withAlpha(0.82f));
        g.strokePath(route, juce::PathStrokeType(2.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const int markerCount = std::max(4, arrowCount * 2);
        for (int marker = 0; marker < markerCount; ++marker)
        {
            const std::size_t index = static_cast<std::size_t>((marker + 1) * (points.size() - 1) / (markerCount + 1));
            const float radius = marker % 2 == 0 ? 2.7f : 1.8f;
            g.setColour(colour.withAlpha(marker % 2 == 0 ? 0.72f : 0.46f));
            g.fillEllipse(points[index].x - radius, points[index].y - radius, radius * 2.0f, radius * 2.0f);
        }

        for (int arrow = 0; arrow < arrowCount; ++arrow)
        {
            const std::size_t index = static_cast<std::size_t>((arrow + 1) * (points.size() - 2) / (arrowCount + 1)) + 1;
            drawArrow(g, points[index - 1], points[index], colour.withAlpha(0.96f));
        }
    }

    void drawLabel(juce::Graphics& g, juce::Rectangle<float> world, const juce::String& text, juce::Colour colour) const
    {
        auto label = world.reduced(10.0f).removeFromTop(20.0f);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.setColour(colour.withAlpha(0.82f));
        g.drawText("ROUTE  " + text, label, juce::Justification::topLeft, false);
    }

    void stepOrbit(SimState& state, double dt) const
    {
        const double a = std::clamp(plugin_.parameterValue(motion::ids::motionA), 0.0, 1.0);
        const double b = std::clamp(plugin_.parameterValue(motion::ids::motionB), 0.0, 1.0);
        const double c = std::clamp(plugin_.parameterValue(motion::ids::motionC), 0.0, 1.0);
        const double d = std::clamp(plugin_.parameterValue(motion::ids::motionD), 0.0, 1.0);
        const double targetRadius = 0.22 + a * 0.68;
        const double aspect = 1.0 - b * 0.65;
        const double orientation = c * juce::MathConstants<double>::pi;
        const double co = std::cos(orientation);
        const double so = std::sin(orientation);
        const double lx = co * state.x + so * state.y;
        const double ly = -so * state.x + co * state.y;
        const double lvx = co * state.vx + so * state.vy;
        const double lvy = -so * state.vx + co * state.vy;
        const double qx = lx;
        const double qy = ly / aspect;
        const double ellipseRadius = std::max(1.0e-5, std::hypot(qx, qy));
        double rx = qx / ellipseRadius;
        double ry = qy / (ellipseRadius * aspect);
        const double normalLength = std::max(1.0e-5, std::hypot(rx, ry));
        rx /= normalLength;
        ry /= normalLength;
        const double tx = -ry;
        const double ty = rx;
        const double radialVelocity = lvx * rx + lvy * ry;
        const double tangentialVelocity = lvx * tx + lvy * ty;
        const double radialError = ellipseRadius - targetRadius;
        constexpr double pull = 8.0;
        constexpr double radialDamping = 3.0;
        constexpr double tangentDrive = 2.0;
        const double targetSpeed = 0.18 + d * 1.7;
        const double radialAcceleration = -pull * radialError - radialDamping * radialVelocity;
        const double tangentAcceleration = (targetSpeed - tangentialVelocity) * tangentDrive;
        const double lax = rx * radialAcceleration + tx * tangentAcceleration;
        const double lay = ry * radialAcceleration + ty * tangentAcceleration;
        state.vx += (co * lax - so * lay) * dt;
        state.vy += (so * lax + co * lay) * dt;
        state.x += state.vx * dt;
        state.y += state.vy * dt;
        contain(state, true, 0.52);
        applyWorldDrag(state, dt);
    }

    void paintOrbit(juce::Graphics& g, juce::Rectangle<float> world)
    {
        const double a = std::clamp(plugin_.parameterValue(motion::ids::motionA), 0.0, 1.0);
        const double c = std::clamp(plugin_.parameterValue(motion::ids::motionC), 0.0, 1.0);
        const double d = std::clamp(plugin_.parameterValue(motion::ids::motionD), 0.0, 1.0);
        const double radius = 0.22 + a * 0.68;
        const double rotation = c * juce::MathConstants<double>::pi;
        const double speed = 0.18 + d * 1.7;

        SimState state;
        state.x = std::cos(rotation) * radius;
        state.y = std::sin(rotation) * radius;
        state.vx = -std::sin(rotation) * speed;
        state.vy = std::cos(rotation) * speed;

        constexpr double dt = 1.0 / 240.0;
        const int warmupSteps = static_cast<int>(3.0 / dt);
        for (int i = 0; i < warmupSteps; ++i)
            stepOrbit(state, dt);

        const double approximatePeriod = std::clamp(juce::MathConstants<double>::twoPi * std::max(0.2, radius) / std::max(0.08, speed), 1.25, 8.0);
        const int totalSteps = std::max(240, static_cast<int>(approximatePeriod / dt));
        const int stride = std::max(1, totalSteps / 220);
        std::vector<juce::Point<float>> points;
        points.reserve(static_cast<std::size_t>(totalSteps / stride + 2));
        points.push_back(toScreen(world, static_cast<float>(state.x), static_cast<float>(state.y)));
        for (int i = 0; i < totalSteps; ++i)
        {
            stepOrbit(state, dt);
            if (i % stride == 0)
                points.push_back(toScreen(world, static_cast<float>(state.x), static_cast<float>(state.y)));
        }

        const auto colour = juce::Colour(0xff78ddff);
        drawRoute(g, points, colour, true, 4);
        drawLabel(g, world, "ORBIT / BODY PATH", colour);
    }

    void stepLissajous(SimState& state, double& elapsed, double dt) const
    {
        const double a = std::clamp(plugin_.parameterValue(motion::ids::motionA), 0.0, 1.0);
        const double b = std::clamp(plugin_.parameterValue(motion::ids::motionB), 0.0, 1.0);
        const double c = std::clamp(plugin_.parameterValue(motion::ids::motionC), 0.0, 1.0);
        const double d = std::clamp(plugin_.parameterValue(motion::ids::motionD), 0.0, 1.0);
        const double rateHz = 0.08 + a * 1.35;
        const double ratio = 1.0 + b * 2.5;
        const double phase = c * juce::MathConstants<double>::twoPi;
        const double rotation = (d - 0.5) * juce::MathConstants<double>::pi;
        const double t = elapsed * juce::MathConstants<double>::twoPi * rateHz;
        const double rawX = 0.76 * std::sin(t);
        const double rawY = 0.76 * std::sin(t * ratio + phase);
        const double co = std::cos(rotation);
        const double so = std::sin(rotation);
        const double targetX = co * rawX - so * rawY;
        const double targetY = so * rawX + co * rawY;
        constexpr double pull = 25.0;
        constexpr double damping = 8.0;

        state.vx += ((targetX - state.x) * pull - state.vx * damping) * dt;
        state.vy += ((targetY - state.y) * pull - state.vy * damping) * dt;
        state.x += state.vx * dt;
        state.y += state.vy * dt;
        contain(state, false, 0.0);
        applyWorldDrag(state, dt);
        elapsed += dt;
    }

    void paintLissajous(juce::Graphics& g, juce::Rectangle<float> world)
    {
        const double a = std::clamp(plugin_.parameterValue(motion::ids::motionA), 0.0, 1.0);
        const double c = std::clamp(plugin_.parameterValue(motion::ids::motionC), 0.0, 1.0);
        const double d = std::clamp(plugin_.parameterValue(motion::ids::motionD), 0.0, 1.0);
        const double rateHz = 0.08 + a * 1.35;
        const double phase = c * juce::MathConstants<double>::twoPi;
        const double rotation = (d - 0.5) * juce::MathConstants<double>::pi;
        const double co = std::cos(rotation);
        const double so = std::sin(rotation);

        SimState state;
        const double initialRawY = 0.76 * std::sin(phase);
        state.x = -so * initialRawY;
        state.y = co * initialRawY;
        double elapsed = 0.0;

        constexpr double dt = 1.0 / 240.0;
        const int warmupSteps = static_cast<int>(2.5 / dt);
        for (int i = 0; i < warmupSteps; ++i)
            stepLissajous(state, elapsed, dt);

        const double previewSeconds = std::clamp(3.0 / std::max(0.08, rateHz), 3.0, 12.0);
        const int totalSteps = std::max(360, static_cast<int>(previewSeconds / dt));
        const int stride = std::max(1, totalSteps / 360);
        std::vector<juce::Point<float>> points;
        points.reserve(static_cast<std::size_t>(totalSteps / stride + 2));
        points.push_back(toScreen(world, static_cast<float>(state.x), static_cast<float>(state.y)));
        for (int i = 0; i < totalSteps; ++i)
        {
            stepLissajous(state, elapsed, dt);
            if (i % stride == 0)
                points.push_back(toScreen(world, static_cast<float>(state.x), static_cast<float>(state.y)));
        }

        const auto colour = juce::Colour(0xffff79d4);
        drawRoute(g, points, colour, false, 6);
        drawLabel(g, world, "LISSAJOUS / BODY PATH", colour);
    }

    void paintImpulse(juce::Graphics& g, juce::Rectangle<float> world)
    {
        const double forceControl = std::clamp(plugin_.parameterValue(motion::ids::motionA), 0.0, 1.0);
        const double reboundControl = std::clamp(plugin_.parameterValue(motion::ids::motionB), 0.0, 1.0);
        const double decayControl = std::clamp(plugin_.parameterValue(motion::ids::motionC), 0.0, 1.0);
        const double direction = std::clamp(plugin_.parameterValue(motion::ids::motionD), 0.0, 1.0) * juce::MathConstants<double>::twoPi;
        const double energy = std::clamp(plugin_.parameterValue(motion::ids::energy), 0.0, 2.5);
        const double force = (1.5 + 6.8 * forceControl) * (0.6 + 0.4 * energy);
        const double rebound = 2.0 + reboundControl * 28.0;
        const double decay = 0.35 + decayControl * 6.0;
        const double ux = std::cos(direction);
        const double uy = std::sin(direction);

        std::vector<juce::Point<float>> points;
        std::vector<juce::Point<float>> turns;
        constexpr double dt = 1.0 / 120.0;
        constexpr int samples = 420;
        points.reserve(samples + 1);

        double position = 0.0;
        double velocity = force;
        double previousVelocity = velocity;
        points.push_back(toScreen(world, 0.0f, 0.0f));

        for (int i = 0; i < samples; ++i)
        {
            velocity += -rebound * position * dt;
            velocity *= std::exp(-decay * dt);
            position += velocity * dt;
            position = std::clamp(position, -0.96, 0.96);

            const auto point = toScreen(world, static_cast<float>(ux * position), static_cast<float>(uy * position));
            points.push_back(point);
            if (previousVelocity * velocity < 0.0 && turns.size() < 5)
                turns.push_back(point);
            previousVelocity = velocity;
        }

        const auto colour = juce::Colour(0xffffcf73);
        drawRoute(g, points, colour, false, 5);

        for (std::size_t i = 0; i < turns.size(); ++i)
        {
            const float radius = std::max(3.0f, 6.0f - static_cast<float>(i) * 0.65f);
            g.setColour(colour.withAlpha(std::max(0.28f, 0.76f - static_cast<float>(i) * 0.10f)));
            g.drawEllipse(turns[i].x - radius, turns[i].y - radius, radius * 2.0f, radius * 2.0f, 1.4f);
            g.drawText(juce::String(static_cast<int>(i + 1)),
                       static_cast<int>(turns[i].x + radius + 2.0f), static_cast<int>(turns[i].y - 8.0f), 18, 16,
                       juce::Justification::centredLeft, false);
        }

        drawLabel(g, world, "IMPULSE / DECAYING REVERSALS", colour);
    }

    void paintLiveBody(juce::Graphics& g, juce::Rectangle<float> world) const
    {
        const auto snapshot = plugin_.motionCore().getSnapshot();
        const auto p = project(snapshot.x, snapshot.y);
        const juce::Point<float> body {
            juce::jmap(p.x, -1.0f, 1.0f, world.getX(), world.getRight()),
            juce::jmap(p.y, -1.0f, 1.0f, world.getBottom(), world.getY())
        };
        const auto velocityEnd = body + juce::Point<float>(snapshot.vx, -snapshot.vy) * 18.0f;
        const auto warm = juce::Colour(0xffffcf73);
        const auto accent = juce::Colour(0xff78ddff);
        const auto accent2 = juce::Colour(0xffff79d4);
        const auto text = juce::Colour(0xffedf4ff);

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
    }

    juce::Component& parent_;
    MotionEnginePlugin& plugin_;
};

class MotionCanvas final : public juce::Component
{
public:
    explicit MotionCanvas(MotionEnginePlugin& plugin);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void tick();
    void setSelectedZone(int zone);
    std::function<void(int)> onZoneSelected;

private:
    enum class DragMode { none, body, zoneMove, zoneRadius };

    juce::Rectangle<float> worldBounds() const;
    juce::Point<float> worldToScreen(float x, float y) const;
    juce::Point<float> screenToWorld(juce::Point<float> point) const;
    float worldRadiusToPixels(float radius) const;
    void setParameter(clap_id id, double value);
    void beginZoneGesture();
    void endZoneGesture();

    MotionEnginePlugin& plugin_;
    RoutePreviewOverlay routePreview_ { *this, plugin_ };
    DragMode dragMode_ = DragMode::none;
    int dragZone_ = -1;
    int selectedZone_ = 0;
    juce::Point<float> lastDragWorld_;
    double lastDragTimeMs_ = 0.0;
    juce::Point<float> flickVelocity_;
    std::vector<juce::Point<float>> trail_;
};

class OutputStrip final : public juce::Component
{
public:
    OutputStrip(MotionEnginePlugin& plugin, int index);

    void paint(juce::Graphics&) override;
    void resized() override;
    void sync(const motion::MotionEngineCore::Snapshot& snapshot, const BridgeEngine::SlotStatus& status);

private:
    void configureCompactSlider(juce::Slider& slider);
    void setOneShot(clap_id id, double value);
    void bindSlider(juce::Slider& slider, clap_id id);

    MotionEnginePlugin& plugin_;
    int index_ = 0;
    bool syncing_ = false;
    float currentValue_ = 0.5f;

    juce::Label indexLabel_;
    StableComboBox sourceBox_;
    juce::Slider minSlider_;
    juce::Slider maxSlider_;
    StableComboBox curveBox_;
    juce::Slider smoothSlider_;
    juce::TextButton mapButton_ { "MAP" };
    juce::TextButton clearButton_ { "X" };
    juce::Label targetLabel_;
};

class MotionEngineEditor final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit MotionEngineEditor(MotionEnginePlugin& plugin);
    ~MotionEngineEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureSlider(juce::Slider& slider, const juce::String& suffix = {});
    void configureLabel(juce::Label& label, const juce::String& text);
    void bindSlider(juce::Slider& slider, clap_id id);
    void setOneShot(clap_id id, double value);
    void updateModelLabels();
    void selectZone(int zone);
    void syncControls();

    MotionEnginePlugin& plugin_;
    MotionCanvas canvas_;
    bool syncing_ = false;
    int selectedZone_ = 0;
    int displayedModel_ = -1;

    juce::Label titleLabel_;
    juce::Label betaLabel_;
    juce::Label subtitleLabel_;
    juce::Label modelInfoLabel_;
    juce::Label bridgeLabel_;
    juce::Label outputsTitleLabel_;

    StableComboBox modelBox_;
    StableComboBox constraintBox_;
    juce::TextButton hitButton_ { "HIT" };
    juce::TextButton resetButton_ { "RESET" };

    juce::Slider timeSlider_;
    juce::Slider energySlider_;
    juce::Slider dampingSlider_;
    juce::Slider audioKickSlider_;
    juce::Label timeLabel_;
    juce::Label energyLabel_;
    juce::Label dampingLabel_;
    juce::Label audioKickLabel_;

    std::array<juce::Slider, 4> motionSliders_;
    std::array<juce::Label, 4> motionLabels_;

    StableComboBox zoneBox_;
    juce::Slider zoneRadiusSlider_;
    juce::Slider zoneFalloffSlider_;
    juce::Label zoneLabel_;
    juce::Label zoneRadiusLabel_;
    juce::Label zoneFalloffLabel_;

    std::array<std::unique_ptr<OutputStrip>, motion::kNumOutputs> outputStrips_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionEngineEditor)
};
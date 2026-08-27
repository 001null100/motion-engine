package dev.nullexo.motionengine;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import com.bitwig.extension.controller.ControllerExtension;
import com.bitwig.extension.controller.ControllerExtensionDefinition;
import com.bitwig.extension.controller.api.ControllerHost;
import com.bitwig.extension.controller.api.LastClickedParameter;
import com.bitwig.extension.controller.api.Parameter;

public final class MotionEngineBridgeExtension extends ControllerExtension
{
    private static final int PORT = 19782;
    private static final double MAP_CHANGE_EPSILON = 1.0 / 65536.0;
    private static final int MAP_CANDIDATE_SETTLE_TICKS = 2;

    private LastClickedParameter lastClicked;
    private Parameter target;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean mapRequested = new AtomicBoolean(false);
    private final AtomicBoolean unmapRequested = new AtomicBoolean(false);
    private final AtomicLong latestSequence = new AtomicLong(-1);
    private final AtomicLong receivedTotal = new AtomicLong(0);
    private final AtomicLong appliedTotal = new AtomicLong(0);
    private final AtomicInteger requestedHz = new AtomicInteger(120);
    private final AtomicLong latestValueBits = new AtomicLong(Double.doubleToRawLongBits(0.5));
    private final AtomicLong worstGapMicros = new AtomicLong(0);

    private volatile boolean mapped = false;
    private volatile boolean armed = false;
    private volatile String targetName = "None";
    private volatile InetSocketAddress peer;

    private Thread networkThread;
    private DatagramSocket socket;
    private long appliedSequence = -1;
    private long lastApplyNanos = 0;

    // LastClickedParameter is a hover tracker for native Bitwig controls. There is no
    // separate public "clicked/touched" event, so mapping is inferred from an actual
    // value change after a candidate has remained stable briefly. Merely hovering a
    // parameter therefore never maps it.
    private boolean mapCandidateActive = false;
    private String mapCandidateName = "";
    private double mapCandidateValue = 0.0;
    private int mapCandidateSettleTicks = 0;

    MotionEngineBridgeExtension(final ControllerExtensionDefinition definition, final ControllerHost host)
    {
        super(definition, host);
    }

    @Override
    public void init()
    {
        final ControllerHost host = getHost();
        lastClicked = host.createLastClickedParameter("MotionEngineTarget", "Motion Engine Target");
        target = lastClicked.parameter();
        target.exists().markInterested();
        target.name().markInterested();
        target.value().markInterested();

        startNetworkThread();
        host.println("Motion Engine Bridge listening on UDP 127.0.0.1:" + PORT);
        host.showPopupNotification("Motion Engine Bridge loaded");
        host.scheduleTask(this::controlTick, 10);
    }

    @Override
    public void exit()
    {
        restoreMappedAutomation();
        if (lastClicked != null)
            lastClicked.isLocked().set(false);

        running.set(false);
        if (socket != null)
            socket.close();
        if (networkThread != null)
        {
            try { networkThread.join(1000); }
            catch (final InterruptedException ignored) { Thread.currentThread().interrupt(); }
        }
    }

    @Override
    public void flush()
    {
        // All Bitwig API writes are performed by controlTick on the controller thread.
    }

    private void controlTick()
    {
        final ControllerHost host = getHost();

        if (unmapRequested.getAndSet(false))
        {
            restoreMappedAutomation();
            lastClicked.isLocked().set(false);
            mapped = false;
            armed = false;
            targetName = "None";
            resetMapCandidate();
            host.showPopupNotification("Motion Engine target cleared; automation restored");
        }

        if (mapRequested.getAndSet(false))
        {
            // If the previous target had automation, our controller writes put Bitwig
            // into its temporary automation-override state. Release that override before
            // looking for a new target.
            restoreMappedAutomation();
            lastClicked.isLocked().set(false);
            mapped = false;
            armed = true;
            targetName = "None";
            resetMapCandidate();
            host.showPopupNotification("Motion Engine: move/drag the target parameter");
        }

        if (armed)
            observeMapCandidate();

        if (mapped && target.exists().get())
        {
            final long seq = latestSequence.get();
            if (seq != appliedSequence)
            {
                final double value = Double.longBitsToDouble(latestValueBits.get());
                target.value().set(value);
                appliedSequence = seq;
                appliedTotal.incrementAndGet();

                final long now = System.nanoTime();
                if (lastApplyNanos != 0)
                {
                    final long gapMicros = (now - lastApplyNanos) / 1000L;
                    worstGapMicros.accumulateAndGet(gapMicros, Math::max);
                }
                lastApplyNanos = now;
            }
        }

        final int hz = Math.max(1, requestedHz.get());
        final long delayMs = Math.max(1L, Math.round(1000.0 / hz));
        host.scheduleTask(this::controlTick, delayMs);
    }

    private void observeMapCandidate()
    {
        if (!target.exists().get())
        {
            resetMapCandidate();
            return;
        }

        final String currentName = sanitize(target.name().get());
        final double currentValue = target.value().get();

        if (!mapCandidateActive || !currentName.equals(mapCandidateName))
        {
            mapCandidateActive = true;
            mapCandidateName = currentName;
            mapCandidateValue = currentValue;
            mapCandidateSettleTicks = 0;
            return;
        }

        // Absorb the initial values seen when the mouse merely enters a native Bitwig
        // parameter. Only movement after the same candidate has settled can map it.
        if (mapCandidateSettleTicks < MAP_CANDIDATE_SETTLE_TICKS)
        {
            mapCandidateValue = currentValue;
            ++mapCandidateSettleTicks;
            return;
        }

        if (Math.abs(currentValue - mapCandidateValue) >= MAP_CHANGE_EPSILON)
            lockCurrentTarget();
    }

    private void resetMapCandidate()
    {
        mapCandidateActive = false;
        mapCandidateName = "";
        mapCandidateValue = 0.0;
        mapCandidateSettleTicks = 0;
    }

    private void restoreMappedAutomation()
    {
        if (mapped && target != null && target.exists().get())
            target.restoreAutomationControl();
    }

    private void lockCurrentTarget()
    {
        lastClicked.isLocked().set(true);
        mapped = true;
        armed = false;
        targetName = sanitize(target.name().get());
        resetMapCandidate();
        getHost().showPopupNotification("Motion Engine mapped: " + targetName);
    }

    private void startNetworkThread()
    {
        running.set(true);
        networkThread = new Thread(this::networkLoop, "Motion Engine UDP Bridge");
        networkThread.setDaemon(true);
        networkThread.start();
    }

    private void networkLoop()
    {
        long lastTelemetryNanos = System.nanoTime();
        long lastReceived = 0;
        long lastApplied = 0;

        try (DatagramSocket localSocket = new DatagramSocket(PORT, java.net.InetAddress.getLoopbackAddress()))
        {
            socket = localSocket;
            localSocket.setSoTimeout(25);
            final byte[] buffer = new byte[1024];

            while (running.get())
            {
                try
                {
                    final DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    localSocket.receive(packet);
                    peer = new InetSocketAddress(packet.getAddress(), packet.getPort());
                    handlePacket(new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8).trim());
                }
                catch (final SocketTimeoutException ignored)
                {
                    // Timeout gives us a chance to emit telemetry and notice shutdown.
                }

                final long now = System.nanoTime();
                final double seconds = (now - lastTelemetryNanos) / 1_000_000_000.0;
                if (seconds >= 1.0)
                {
                    final long received = receivedTotal.get();
                    final long applied = appliedTotal.get();
                    final double rxHz = (received - lastReceived) / seconds;
                    final double applyHz = (applied - lastApplied) / seconds;
                    final double worstGapMs = worstGapMicros.getAndSet(0) / 1000.0;
                    sendStatus(localSocket, rxHz, applyHz, worstGapMs);
                    lastReceived = received;
                    lastApplied = applied;
                    lastTelemetryNanos = now;
                }
            }
        }
        catch (final Exception e)
        {
            if (running.get())
                getHost().errorln("Motion Engine UDP bridge failed: " + e.getMessage());
        }
        finally
        {
            socket = null;
        }
    }

    private void handlePacket(final String message)
    {
        final String[] fields = message.split("\\|", -1);
        if (fields.length < 2 || !"ME1".equals(fields[0]))
            return;

        switch (fields[1])
        {
            case "MAP" -> mapRequested.set(true);
            case "UNMAP" -> unmapRequested.set(true);
            case "VALUE" -> {
                if (fields.length < 5)
                    return;
                try
                {
                    latestSequence.set(Long.parseUnsignedLong(fields[2]));
                    latestValueBits.set(Double.doubleToRawLongBits(Math.max(0.0, Math.min(1.0, Double.parseDouble(fields[3])))));
                    requestedHz.set(Math.max(1, Math.min(1000, Integer.parseInt(fields[4]))));
                    receivedTotal.incrementAndGet();
                }
                catch (final NumberFormatException ignored) {}
            }
            default -> { }
        }
    }

    private void sendStatus(final DatagramSocket localSocket, final double rxHz,
                            final double appliedHz, final double worstGapMs)
    {
        final InetSocketAddress destination = peer;
        if (destination == null)
            return;

        final String message = String.format(Locale.ROOT,
            "ME1|STATUS|%s|%.3f|%.3f|%d|%.3f|%d|%d\n",
            sanitize(targetName), rxHz, appliedHz, requestedHz.get(), worstGapMs,
            mapped ? 1 : 0, armed ? 1 : 0);
        final byte[] bytes = message.getBytes(StandardCharsets.UTF_8);
        try
        {
            localSocket.send(new DatagramPacket(bytes, bytes.length, destination));
        }
        catch (final Exception ignored) {}
    }

    private static String sanitize(final String value)
    {
        if (value == null || value.isBlank())
            return "Unnamed parameter";
        return value.replace('|', '/').replace('\n', ' ').replace('\r', ' ');
    }
}

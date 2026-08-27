package dev.nullexo.motionengine;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import com.bitwig.extension.controller.ControllerExtension;
import com.bitwig.extension.controller.ControllerExtensionDefinition;
import com.bitwig.extension.controller.api.ControllerHost;
import com.bitwig.extension.controller.api.LastClickedParameter;
import com.bitwig.extension.controller.api.Parameter;

public final class MotionEngineBridgeExtension extends ControllerExtension
{
    private static final int PORT = 19782;
    private static final int OUTPUTS = 8;
    private static final double MAP_CHANGE_EPSILON = 1.0 / 65536.0;
    private static final int MAP_CANDIDATE_SETTLE_TICKS = 2;

    private final LastClickedParameter[] lastClicked = new LastClickedParameter[OUTPUTS];
    private final Parameter[] targets = new Parameter[OUTPUTS];
    private final boolean[] mapped = new boolean[OUTPUTS];
    private final boolean[] armed = new boolean[OUTPUTS];
    private final String[] targetNames = new String[OUTPUTS];

    private final boolean[] candidateActive = new boolean[OUTPUTS];
    private final String[] candidateName = new String[OUTPUTS];
    private final double[] candidateValue = new double[OUTPUTS];
    private final int[] candidateSettleTicks = new int[OUTPUTS];

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean[] mapRequested = new AtomicBoolean[OUTPUTS];
    private final AtomicBoolean[] unmapRequested = new AtomicBoolean[OUTPUTS];
    private final AtomicLong latestSequence = new AtomicLong(-1);
    private final AtomicLong receivedTotal = new AtomicLong(0);
    private final AtomicLong appliedTotal = new AtomicLong(0);
    private final AtomicLong[] latestValueBits = new AtomicLong[OUTPUTS];
    private final AtomicLong worstGapMicros = new AtomicLong(0);

    private volatile InetSocketAddress peer;
    private Thread networkThread;
    private DatagramSocket socket;
    private long appliedSequence = -1;
    private long lastApplyNanos = 0;

    MotionEngineBridgeExtension(final ControllerExtensionDefinition definition, final ControllerHost host)
    {
        super(definition, host);
        for (int i = 0; i < OUTPUTS; ++i)
        {
            mapRequested[i] = new AtomicBoolean(false);
            unmapRequested[i] = new AtomicBoolean(false);
            latestValueBits[i] = new AtomicLong(Double.doubleToRawLongBits(0.5));
            targetNames[i] = "None";
            candidateName[i] = "";
        }
    }

    @Override
    public void init()
    {
        final ControllerHost host = getHost();
        for (int i = 0; i < OUTPUTS; ++i)
        {
            lastClicked[i] = host.createLastClickedParameter("MotionEngineTarget" + (i + 1), "Motion Engine Target " + (i + 1));
            targets[i] = lastClicked[i].parameter();
            targets[i].exists().markInterested();
            targets[i].name().markInterested();
            targets[i].value().markInterested();
        }

        startNetworkThread();
        host.println("Motion Engine Bridge v1 listening on UDP 127.0.0.1:" + PORT);
        host.showPopupNotification("Motion Engine Bridge loaded");
        host.scheduleTask(this::controlTick, 1);
    }

    @Override
    public void exit()
    {
        for (int i = 0; i < OUTPUTS; ++i)
        {
            restoreMappedAutomation(i);
            if (lastClicked[i] != null)
                lastClicked[i].isLocked().set(false);
        }

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
        // All Bitwig API writes stay on the controller thread in controlTick().
    }

    private void controlTick()
    {
        final ControllerHost host = getHost();

        for (int slot = 0; slot < OUTPUTS; ++slot)
        {
            if (unmapRequested[slot].getAndSet(false))
            {
                restoreMappedAutomation(slot);
                lastClicked[slot].isLocked().set(false);
                mapped[slot] = false;
                armed[slot] = false;
                targetNames[slot] = "None";
                resetCandidate(slot);
                host.showPopupNotification("Motion " + (slot + 1) + " cleared; automation restored");
            }

            if (mapRequested[slot].getAndSet(false))
            {
                restoreMappedAutomation(slot);
                lastClicked[slot].isLocked().set(false);
                mapped[slot] = false;
                targetNames[slot] = "None";
                resetCandidate(slot);

                for (int other = 0; other < OUTPUTS; ++other)
                    armed[other] = other == slot;

                host.showPopupNotification("Motion " + (slot + 1) + ": move/drag the target parameter");
            }
        }

        for (int slot = 0; slot < OUTPUTS; ++slot)
            if (armed[slot])
                observeCandidate(slot);

        final long sequence = latestSequence.get();
        if (sequence != appliedSequence)
        {
            boolean appliedAny = false;
            for (int slot = 0; slot < OUTPUTS; ++slot)
            {
                if (!mapped[slot] || !targets[slot].exists().get())
                    continue;

                final double value = Double.longBitsToDouble(latestValueBits[slot].get());
                targets[slot].value().set(value);
                appliedAny = true;
            }

            if (appliedAny)
            {
                appliedTotal.incrementAndGet();
                final long now = System.nanoTime();
                if (lastApplyNanos != 0)
                {
                    final long gapMicros = (now - lastApplyNanos) / 1000L;
                    worstGapMicros.accumulateAndGet(gapMicros, Math::max);
                }
                lastApplyNanos = now;
            }
            appliedSequence = sequence;
        }

        host.scheduleTask(this::controlTick, 1);
    }

    private void observeCandidate(final int slot)
    {
        final Parameter target = targets[slot];
        if (!target.exists().get())
        {
            resetCandidate(slot);
            return;
        }

        final String currentName = sanitize(target.name().get());
        final double currentValue = target.value().get();

        if (!candidateActive[slot] || !currentName.equals(candidateName[slot]))
        {
            candidateActive[slot] = true;
            candidateName[slot] = currentName;
            candidateValue[slot] = currentValue;
            candidateSettleTicks[slot] = 0;
            return;
        }

        if (candidateSettleTicks[slot] < MAP_CANDIDATE_SETTLE_TICKS)
        {
            candidateValue[slot] = currentValue;
            ++candidateSettleTicks[slot];
            return;
        }

        if (Math.abs(currentValue - candidateValue[slot]) >= MAP_CHANGE_EPSILON)
            lockCurrentTarget(slot);
    }

    private void lockCurrentTarget(final int slot)
    {
        lastClicked[slot].isLocked().set(true);
        mapped[slot] = true;
        armed[slot] = false;
        targetNames[slot] = sanitize(targets[slot].name().get());
        resetCandidate(slot);
        getHost().showPopupNotification("Motion " + (slot + 1) + " mapped: " + targetNames[slot]);
    }

    private void resetCandidate(final int slot)
    {
        candidateActive[slot] = false;
        candidateName[slot] = "";
        candidateValue[slot] = 0.0;
        candidateSettleTicks[slot] = 0;
    }

    private void restoreMappedAutomation(final int slot)
    {
        if (mapped[slot] && targets[slot] != null && targets[slot].exists().get())
            targets[slot].restoreAutomationControl();
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
            final byte[] buffer = new byte[2048];

            while (running.get())
            {
                try
                {
                    final DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    localSocket.receive(packet);
                    peer = new InetSocketAddress(packet.getAddress(), packet.getPort());
                    handlePacket(new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8).trim());
                }
                catch (final SocketTimeoutException ignored) {}

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
        if (fields.length < 2 || !"ME2".equals(fields[0]))
            return;

        switch (fields[1])
        {
            case "MAP" -> {
                final int slot = parseSlot(fields);
                if (slot >= 0) mapRequested[slot].set(true);
            }
            case "UNMAP" -> {
                final int slot = parseSlot(fields);
                if (slot >= 0) unmapRequested[slot].set(true);
            }
            case "VALUES" -> {
                if (fields.length < 3 + OUTPUTS)
                    return;
                try
                {
                    latestSequence.set(Long.parseUnsignedLong(fields[2]));
                    for (int slot = 0; slot < OUTPUTS; ++slot)
                    {
                        final double value = Math.max(0.0, Math.min(1.0, Double.parseDouble(fields[3 + slot])));
                        latestValueBits[slot].set(Double.doubleToRawLongBits(value));
                    }
                    receivedTotal.incrementAndGet();
                }
                catch (final NumberFormatException ignored) {}
            }
            default -> { }
        }
    }

    private int parseSlot(final String[] fields)
    {
        if (fields.length < 3)
            return -1;
        try
        {
            final int slot = Integer.parseInt(fields[2]);
            return slot >= 0 && slot < OUTPUTS ? slot : -1;
        }
        catch (final NumberFormatException ignored)
        {
            return -1;
        }
    }

    private void sendStatus(final DatagramSocket localSocket, final double rxHz,
                            final double appliedHz, final double worstGapMs)
    {
        final InetSocketAddress destination = peer;
        if (destination == null)
            return;

        int mappedMask = 0;
        int armedMask = 0;
        final StringBuilder names = new StringBuilder();
        for (int slot = 0; slot < OUTPUTS; ++slot)
        {
            if (mapped[slot]) mappedMask |= 1 << slot;
            if (armed[slot]) armedMask |= 1 << slot;
            if (slot > 0) names.append('~');
            names.append(sanitize(targetNames[slot]));
        }

        final String message = String.format(Locale.ROOT,
            "ME2|STATUS|%.3f|%.3f|%.3f|%d|%d|%s\n",
            rxHz, appliedHz, worstGapMs, mappedMask, armedMask, names);
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
            return "None";
        return value.replace('|', '/').replace('~', '/').replace('\n', ' ').replace('\r', ' ');
    }
}

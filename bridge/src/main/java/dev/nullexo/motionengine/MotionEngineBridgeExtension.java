package dev.nullexo.motionengine;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
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
    private static final int MAX_SESSIONS = 8;
    private static final int DEFAULT_TRACKER_CAPACITY = 8;
    private static final int MAX_TRACKER_CAPACITY = MAX_SESSIONS * OUTPUTS;
    private static final double MAP_CHANGE_EPSILON = 1.0 / 65536.0;
    private static final int MAP_CANDIDATE_SETTLE_TICKS = 2;

    // Bitwig deliberately exposes every LastClickedParameter's name in user-facing
    // contexts. Keep only a small shared pool instead of eagerly advertising the
    // theoretical 8 sessions x 8 outputs as 64 separate controller targets.
    private LastClickedParameter[] trackerPool = new LastClickedParameter[0];
    private Parameter[] trackerTargetPool = new Parameter[0];
    private int[] trackerOwnerBank = new int[0];
    private int[] trackerOwnerSlot = new int[0];
    private int trackerCapacity = DEFAULT_TRACKER_CAPACITY;

    // Session/output lanes borrow trackers from the pool only while armed or mapped.
    private final int[][] trackerIndex = new int[MAX_SESSIONS][OUTPUTS];
    private final LastClickedParameter[][] lastClicked = new LastClickedParameter[MAX_SESSIONS][OUTPUTS];
    private final Parameter[][] targets = new Parameter[MAX_SESSIONS][OUTPUTS];
    private final boolean[][] mapped = new boolean[MAX_SESSIONS][OUTPUTS];
    private final boolean[][] armed = new boolean[MAX_SESSIONS][OUTPUTS];
    private final String[][] targetNames = new String[MAX_SESSIONS][OUTPUTS];

    private final boolean[][] candidateActive = new boolean[MAX_SESSIONS][OUTPUTS];
    private final String[][] candidateName = new String[MAX_SESSIONS][OUTPUTS];
    private final double[][] candidateValue = new double[MAX_SESSIONS][OUTPUTS];
    private final int[][] candidateSettleTicks = new int[MAX_SESSIONS][OUTPUTS];

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean[][] mapRequested = new AtomicBoolean[MAX_SESSIONS][OUTPUTS];
    private final AtomicBoolean[][] unmapRequested = new AtomicBoolean[MAX_SESSIONS][OUTPUTS];
    private final AtomicBoolean[] releaseRequested = new AtomicBoolean[MAX_SESSIONS];
    private final AtomicLong[] latestSequence = new AtomicLong[MAX_SESSIONS];
    private final AtomicLong[] receivedTotal = new AtomicLong[MAX_SESSIONS];
    private final AtomicLong[] appliedTotal = new AtomicLong[MAX_SESSIONS];
    private final AtomicLong[][] latestValueBits = new AtomicLong[MAX_SESSIONS][OUTPUTS];
    private final AtomicLong[] worstGapMicros = new AtomicLong[MAX_SESSIONS];

    private final Object sessionLock = new Object();
    private final String[] sessionIds = new String[MAX_SESSIONS];
    private final InetSocketAddress[] peers = new InetSocketAddress[MAX_SESSIONS];
    private final long[] appliedSequence = new long[MAX_SESSIONS];
    private final long[] lastApplyNanos = new long[MAX_SESSIONS];

    private Thread networkThread;
    private DatagramSocket socket;

    MotionEngineBridgeExtension(final ControllerExtensionDefinition definition, final ControllerHost host)
    {
        super(definition, host);
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
        {
            Arrays.fill(trackerIndex[bank], -1);
            releaseRequested[bank] = new AtomicBoolean(false);
            latestSequence[bank] = new AtomicLong(-1);
            receivedTotal[bank] = new AtomicLong(0);
            appliedTotal[bank] = new AtomicLong(0);
            worstGapMicros[bank] = new AtomicLong(0);
            appliedSequence[bank] = -1;
            for (int slot = 0; slot < OUTPUTS; ++slot)
            {
                mapRequested[bank][slot] = new AtomicBoolean(false);
                unmapRequested[bank][slot] = new AtomicBoolean(false);
                latestValueBits[bank][slot] = new AtomicLong(Double.doubleToRawLongBits(0.5));
                targetNames[bank][slot] = "None";
                candidateName[bank][slot] = "";
            }
        }
    }

    @Override
    public void init()
    {
        final ControllerHost host = getHost();

        // LastClickedParameter objects are Bitwig API objects and must be created
        // during extension initialization. Capacity therefore applies after the
        // extension is restarted/reloaded, not live while it is running.
        final var capacitySetting = host.getPreferences().getEnumSetting(
            "Mapping capacity (restart extension)",
            "Motion Engine Bridge",
            new String[] { "8", "16", "32", "64" },
            Integer.toString(DEFAULT_TRACKER_CAPACITY));
        trackerCapacity = parseTrackerCapacity(capacitySetting.get());

        trackerPool = new LastClickedParameter[trackerCapacity];
        trackerTargetPool = new Parameter[trackerCapacity];
        trackerOwnerBank = new int[trackerCapacity];
        trackerOwnerSlot = new int[trackerCapacity];
        Arrays.fill(trackerOwnerBank, -1);
        Arrays.fill(trackerOwnerSlot, -1);

        for (int index = 0; index < trackerCapacity; ++index)
        {
            final String identifier = "MotionEngineBridgeTarget" + (index + 1);
            final String label = "Motion Engine Bridge Target " + (index + 1);
            trackerPool[index] = host.createLastClickedParameter(identifier, label);
            trackerTargetPool[index] = trackerPool[index].parameter();
            trackerTargetPool[index].exists().markInterested();
            trackerTargetPool[index].name().markInterested();
            trackerTargetPool[index].value().markInterested();
            trackerPool[index].isLocked().set(true);
        }

        startNetworkThread();
        host.println("Motion Engine Bridge v2.1 listening on UDP 127.0.0.1:" + PORT
            + " (" + MAX_SESSIONS + " sessions, " + trackerCapacity + " shared mapping targets)");
        host.showPopupNotification("Motion Engine Bridge v2.1 loaded (mapping capacity " + trackerCapacity + ")");
        host.scheduleTask(this::controlTick, 1);
    }

    @Override
    public void exit()
    {
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            releaseBankOnControllerThread(bank, false);

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

        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
        {
            if (releaseRequested[bank].getAndSet(false))
            {
                releaseBankOnControllerThread(bank, true);
                continue;
            }

            if (!isBankActive(bank))
                continue;

            for (int slot = 0; slot < OUTPUTS; ++slot)
            {
                if (unmapRequested[bank][slot].getAndSet(false))
                {
                    restoreMappedAutomation(bank, slot);
                    mapped[bank][slot] = false;
                    armed[bank][slot] = false;
                    targetNames[bank][slot] = "None";
                    resetCandidate(bank, slot);
                    releaseTracker(bank, slot);
                    host.showPopupNotification("Motion " + (slot + 1) + " cleared; automation restored");
                }

                if (mapRequested[bank][slot].getAndSet(false))
                    armMapping(bank, slot);
            }
        }

        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            if (isBankActive(bank))
                for (int slot = 0; slot < OUTPUTS; ++slot)
                    if (armed[bank][slot])
                        observeCandidate(bank, slot);

        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
        {
            if (!isBankActive(bank))
                continue;
            final long sequence = latestSequence[bank].get();
            if (sequence == appliedSequence[bank])
                continue;

            boolean appliedAny = false;
            for (int slot = 0; slot < OUTPUTS; ++slot)
            {
                final Parameter target = targets[bank][slot];
                if (!mapped[bank][slot] || target == null || !target.exists().get())
                    continue;
                final double value = Double.longBitsToDouble(latestValueBits[bank][slot].get());
                target.value().set(value);
                appliedAny = true;
            }

            if (appliedAny)
            {
                appliedTotal[bank].incrementAndGet();
                final long now = System.nanoTime();
                if (lastApplyNanos[bank] != 0)
                {
                    final long gapMicros = (now - lastApplyNanos[bank]) / 1000L;
                    worstGapMicros[bank].accumulateAndGet(gapMicros, Math::max);
                }
                lastApplyNanos[bank] = now;
            }
            appliedSequence[bank] = sequence;
        }

        host.scheduleTask(this::controlTick, 1);
    }

    private void armMapping(final int bank, final int slot)
    {
        // Capture is global because every LastClickedParameter watches the same Bitwig
        // UI. Cancel any previous armed lane first. An unmapped capture immediately
        // returns its borrowed tracker to the pool.
        for (int otherBank = 0; otherBank < MAX_SESSIONS; ++otherBank)
        {
            for (int otherSlot = 0; otherSlot < OUTPUTS; ++otherSlot)
            {
                if (!armed[otherBank][otherSlot])
                    continue;
                armed[otherBank][otherSlot] = false;
                resetCandidate(otherBank, otherSlot);
                final LastClickedParameter previous = lastClicked[otherBank][otherSlot];
                if (previous != null)
                    previous.isLocked().set(true);
                if (!mapped[otherBank][otherSlot]
                    && (otherBank != bank || otherSlot != slot))
                    releaseTracker(otherBank, otherSlot);
            }
        }

        restoreMappedAutomation(bank, slot);
        mapped[bank][slot] = false;
        targetNames[bank][slot] = "None";
        resetCandidate(bank, slot);

        int tracker = trackerIndex[bank][slot];
        if (tracker < 0)
            tracker = acquireTracker(bank, slot);
        if (tracker < 0)
        {
            getHost().showPopupNotification(
                "Motion Engine mapping pool full (" + trackerCapacity
                    + "). Increase Mapping capacity in controller settings and restart the extension.");
            return;
        }

        armed[bank][slot] = true;
        lastClicked[bank][slot].isLocked().set(false);
        getHost().showPopupNotification("Motion " + (slot + 1) + ": move/drag the target parameter");
    }

    private int acquireTracker(final int bank, final int slot)
    {
        final int existing = trackerIndex[bank][slot];
        if (existing >= 0)
            return existing;

        for (int index = 0; index < trackerCapacity; ++index)
        {
            if (trackerOwnerBank[index] >= 0)
                continue;

            trackerOwnerBank[index] = bank;
            trackerOwnerSlot[index] = slot;
            trackerIndex[bank][slot] = index;
            lastClicked[bank][slot] = trackerPool[index];
            targets[bank][slot] = trackerTargetPool[index];
            trackerPool[index].isLocked().set(true);
            return index;
        }
        return -1;
    }

    private void releaseTracker(final int bank, final int slot)
    {
        final int index = trackerIndex[bank][slot];
        if (index < 0)
        {
            lastClicked[bank][slot] = null;
            targets[bank][slot] = null;
            return;
        }

        trackerPool[index].isLocked().set(true);
        trackerOwnerBank[index] = -1;
        trackerOwnerSlot[index] = -1;
        trackerIndex[bank][slot] = -1;
        lastClicked[bank][slot] = null;
        targets[bank][slot] = null;
    }

    private void observeCandidate(final int bank, final int slot)
    {
        final Parameter target = targets[bank][slot];
        if (target == null || !target.exists().get())
        {
            resetCandidate(bank, slot);
            return;
        }

        final String currentName = sanitize(target.name().get());
        final double currentValue = target.value().get();
        if (!candidateActive[bank][slot] || !currentName.equals(candidateName[bank][slot]))
        {
            candidateActive[bank][slot] = true;
            candidateName[bank][slot] = currentName;
            candidateValue[bank][slot] = currentValue;
            candidateSettleTicks[bank][slot] = 0;
            return;
        }

        if (candidateSettleTicks[bank][slot] < MAP_CANDIDATE_SETTLE_TICKS)
        {
            candidateValue[bank][slot] = currentValue;
            ++candidateSettleTicks[bank][slot];
            return;
        }

        if (Math.abs(currentValue - candidateValue[bank][slot]) >= MAP_CHANGE_EPSILON)
            lockCurrentTarget(bank, slot);
    }

    private void lockCurrentTarget(final int bank, final int slot)
    {
        final LastClickedParameter tracker = lastClicked[bank][slot];
        final Parameter target = targets[bank][slot];
        if (tracker == null || target == null)
            return;

        tracker.isLocked().set(true);
        mapped[bank][slot] = true;
        armed[bank][slot] = false;
        targetNames[bank][slot] = sanitize(target.name().get());
        resetCandidate(bank, slot);
        getHost().showPopupNotification("Motion " + (slot + 1) + " mapped: " + targetNames[bank][slot]);
    }

    private void resetCandidate(final int bank, final int slot)
    {
        candidateActive[bank][slot] = false;
        candidateName[bank][slot] = "";
        candidateValue[bank][slot] = 0.0;
        candidateSettleTicks[bank][slot] = 0;
    }

    private void restoreMappedAutomation(final int bank, final int slot)
    {
        final Parameter target = targets[bank][slot];
        if (mapped[bank][slot] && target != null && target.exists().get())
            target.restoreAutomationControl();
    }

    private boolean isBankActive(final int bank)
    {
        synchronized (sessionLock)
        {
            return sessionIds[bank] != null;
        }
    }

    private void releaseBankOnControllerThread(final int bank, final boolean announce)
    {
        String releasedSession;
        synchronized (sessionLock)
        {
            releasedSession = sessionIds[bank];
        }
        if (releasedSession == null)
            return;

        for (int slot = 0; slot < OUTPUTS; ++slot)
        {
            restoreMappedAutomation(bank, slot);
            mapped[bank][slot] = false;
            armed[bank][slot] = false;
            targetNames[bank][slot] = "None";
            resetCandidate(bank, slot);
            releaseTracker(bank, slot);
            mapRequested[bank][slot].set(false);
            unmapRequested[bank][slot].set(false);
            latestValueBits[bank][slot].set(Double.doubleToRawLongBits(0.5));
        }
        latestSequence[bank].set(-1);
        receivedTotal[bank].set(0);
        appliedTotal[bank].set(0);
        worstGapMicros[bank].set(0);
        appliedSequence[bank] = -1;
        lastApplyNanos[bank] = 0;

        synchronized (sessionLock)
        {
            sessionIds[bank] = null;
            peers[bank] = null;
        }
        if (announce)
            getHost().println("Motion Engine bridge session released: " + releasedSession);
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
        final long[] lastTelemetryNanos = new long[MAX_SESSIONS];
        final long[] lastReceived = new long[MAX_SESSIONS];
        final long[] lastApplied = new long[MAX_SESSIONS];
        final long now = System.nanoTime();
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            lastTelemetryNanos[bank] = now;

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
                    final InetSocketAddress source = new InetSocketAddress(packet.getAddress(), packet.getPort());
                    handlePacket(new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8).trim(), source);
                }
                catch (final SocketTimeoutException ignored) {}

                final long tick = System.nanoTime();
                for (int bank = 0; bank < MAX_SESSIONS; ++bank)
                {
                    if (!isBankActive(bank))
                        continue;
                    final double seconds = (tick - lastTelemetryNanos[bank]) / 1_000_000_000.0;
                    if (seconds < 1.0)
                        continue;

                    final long received = receivedTotal[bank].get();
                    final long applied = appliedTotal[bank].get();
                    final double rxHz = (received - lastReceived[bank]) / seconds;
                    final double applyHz = (applied - lastApplied[bank]) / seconds;
                    final double worstGapMs = worstGapMicros[bank].getAndSet(0) / 1000.0;
                    sendStatus(localSocket, bank, rxHz, applyHz, worstGapMs);
                    lastReceived[bank] = received;
                    lastApplied[bank] = applied;
                    lastTelemetryNanos[bank] = tick;
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

    private void handlePacket(final String message, final InetSocketAddress source)
    {
        final String[] fields = message.split("\\|", -1);
        if (fields.length < 3 || !"ME3".equals(fields[0]))
            return;

        final String session = sanitizeSession(fields[2]);
        if (session == null)
            return;

        if ("BYE".equals(fields[1]))
        {
            final int existing = findBank(session);
            if (existing >= 0)
                releaseRequested[existing].set(true);
            return;
        }

        final int bank = findOrAllocateBank(session, source);
        if (bank < 0)
            return;
        synchronized (sessionLock)
        {
            peers[bank] = source;
        }

        switch (fields[1])
        {
            case "MAP" -> {
                final int slot = parseSlot(fields, 3);
                if (slot >= 0) mapRequested[bank][slot].set(true);
            }
            case "UNMAP" -> {
                final int slot = parseSlot(fields, 3);
                if (slot >= 0) unmapRequested[bank][slot].set(true);
            }
            case "VALUES" -> {
                if (fields.length < 4 + OUTPUTS)
                    return;
                try
                {
                    latestSequence[bank].set(Long.parseUnsignedLong(fields[3]));
                    for (int slot = 0; slot < OUTPUTS; ++slot)
                    {
                        final double value = Math.max(0.0, Math.min(1.0, Double.parseDouble(fields[4 + slot])));
                        latestValueBits[bank][slot].set(Double.doubleToRawLongBits(value));
                    }
                    receivedTotal[bank].incrementAndGet();
                }
                catch (final NumberFormatException ignored) {}
            }
            default -> { }
        }
    }

    private int findBank(final String session)
    {
        synchronized (sessionLock)
        {
            for (int bank = 0; bank < MAX_SESSIONS; ++bank)
                if (session.equals(sessionIds[bank]))
                    return bank;
            return -1;
        }
    }

    private int findOrAllocateBank(final String session, final InetSocketAddress source)
    {
        synchronized (sessionLock)
        {
            for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            {
                if (session.equals(sessionIds[bank]))
                    return bank;
            }
            for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            {
                if (sessionIds[bank] == null)
                {
                    sessionIds[bank] = session;
                    peers[bank] = source;
                    getHost().println("Motion Engine bridge session " + session + " assigned bank " + (bank + 1));
                    return bank;
                }
            }
        }
        getHost().errorln("Motion Engine bridge has no free session banks (max " + MAX_SESSIONS + ")");
        return -1;
    }

    private int parseSlot(final String[] fields, final int index)
    {
        if (fields.length <= index)
            return -1;
        try
        {
            final int slot = Integer.parseInt(fields[index]);
            return slot >= 0 && slot < OUTPUTS ? slot : -1;
        }
        catch (final NumberFormatException ignored)
        {
            return -1;
        }
    }

    private void sendStatus(final DatagramSocket localSocket, final int bank, final double rxHz,
                            final double appliedHz, final double worstGapMs)
    {
        final String session;
        final InetSocketAddress destination;
        synchronized (sessionLock)
        {
            session = sessionIds[bank];
            destination = peers[bank];
        }
        if (session == null || destination == null)
            return;

        int mappedMask = 0;
        int armedMask = 0;
        final StringBuilder names = new StringBuilder();
        for (int slot = 0; slot < OUTPUTS; ++slot)
        {
            if (mapped[bank][slot]) mappedMask |= 1 << slot;
            if (armed[bank][slot]) armedMask |= 1 << slot;
            if (slot > 0) names.append('~');
            names.append(sanitize(targetNames[bank][slot]));
        }

        final String message = String.format(Locale.ROOT,
            "ME3|STATUS|%s|%.3f|%.3f|%.3f|%d|%d|%s\n",
            session, rxHz, appliedHz, worstGapMs, mappedMask, armedMask, names);
        final byte[] bytes = message.getBytes(StandardCharsets.UTF_8);
        try
        {
            localSocket.send(new DatagramPacket(bytes, bytes.length, destination));
        }
        catch (final Exception ignored) {}
    }

    private static int parseTrackerCapacity(final String value)
    {
        try
        {
            final int parsed = Integer.parseInt(value);
            if (parsed == 8 || parsed == 16 || parsed == 32 || parsed == MAX_TRACKER_CAPACITY)
                return parsed;
        }
        catch (final NumberFormatException ignored) {}
        return DEFAULT_TRACKER_CAPACITY;
    }

    private static String sanitizeSession(final String value)
    {
        if (value == null || !value.matches("[0-9a-fA-F]{8,32}"))
            return null;
        return value.toLowerCase(Locale.ROOT);
    }

    private static String sanitize(final String value)
    {
        if (value == null || value.isBlank())
            return "None";
        return value.replace('|', '/').replace('~', '/').replace('\n', ' ').replace('\r', ' ');
    }
}

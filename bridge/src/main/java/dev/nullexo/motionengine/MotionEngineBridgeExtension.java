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

/**
 * Safe companion for Motion Engine.
 *
 * The v0.2 bridge used last-clicked parameter proxies as durable arbitrary-target
 * references. Bitwig intentionally exposes each such proxy as a user-facing
 * controller mapping target. That means other controller scripts can bind to or
 * otherwise interact with the same proxy that Motion Engine is using internally,
 * and the proxy can also capture Motion Engine's own parameters.
 * There is no public API flag that makes those proxies private.
 *
 * v0.3 therefore creates no arbitrary-target proxies at all. The extension is
 * only a localhost heartbeat/diagnostic companion. Modulation is routed through
 * Motion Engine's eight CLAP auxiliary outputs and Bitwig's Audio Rate modulator.
 */
public final class MotionEngineBridgeExtension extends ControllerExtension
{
    private static final int PORT = 19782;
    private static final int OUTPUTS = 8;
    private static final int MAX_SESSIONS = 8;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean[] releaseRequested = new AtomicBoolean[MAX_SESSIONS];
    private final AtomicLong[] receivedTotal = new AtomicLong[MAX_SESSIONS];

    private final Object sessionLock = new Object();
    private final String[] sessionIds = new String[MAX_SESSIONS];
    private final InetSocketAddress[] peers = new InetSocketAddress[MAX_SESSIONS];

    private Thread networkThread;
    private DatagramSocket socket;

    MotionEngineBridgeExtension(final ControllerExtensionDefinition definition, final ControllerHost host)
    {
        super(definition, host);
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
        {
            releaseRequested[bank] = new AtomicBoolean(false);
            receivedTotal[bank] = new AtomicLong(0);
        }
    }

    @Override
    public void init()
    {
        final ControllerHost host = getHost();
        startNetworkThread();
        host.println("Motion Engine Bridge v3 listening on UDP 127.0.0.1:" + PORT
            + " (safe aux routing; no controller target proxies)");
        host.showPopupNotification("Motion Engine Bridge v3: safe aux routing active");
        host.scheduleTask(this::controlTick, 25);
    }

    @Override
    public void exit()
    {
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
            releaseBank(bank, false);

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
        // No controller mappings or hardware output are owned by this extension.
    }

    private void controlTick()
    {
        for (int bank = 0; bank < MAX_SESSIONS; ++bank)
        {
            if (releaseRequested[bank].getAndSet(false))
                releaseBank(bank, true);
        }
        getHost().scheduleTask(this::controlTick, 25);
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
                    final double rxHz = (received - lastReceived[bank]) / seconds;
                    sendStatus(localSocket, bank, rxHz);
                    lastReceived[bank] = received;
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
            case "VALUES" -> receivedTotal[bank].incrementAndGet();
            // MAP/UNMAP from older plug-ins are intentionally ignored. v3 owns no
            // arbitrary Bitwig target and can therefore never overwrite one.
            case "MAP", "UNMAP" -> { }
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
                    return bank;
                }
            }
        }
        return -1;
    }

    private boolean isBankActive(final int bank)
    {
        synchronized (sessionLock)
        {
            return sessionIds[bank] != null;
        }
    }

    private void releaseBank(final int bank, final boolean announce)
    {
        final String releasedSession;
        synchronized (sessionLock)
        {
            releasedSession = sessionIds[bank];
            sessionIds[bank] = null;
            peers[bank] = null;
        }
        if (releasedSession == null)
            return;

        receivedTotal[bank].set(0);
        if (announce)
            getHost().println("Motion Engine bridge session released: " + releasedSession);
    }

    private void sendStatus(final DatagramSocket localSocket, final int bank, final double rxHz)
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

        final StringBuilder names = new StringBuilder();
        for (int slot = 0; slot < OUTPUTS; ++slot)
        {
            if (slot > 0) names.append('~');
            names.append("None");
        }

        final String message = String.format(Locale.ROOT,
            "ME3|STATUS|%s|%.3f|0.000|0.000|0|0|%s\n",
            session, rxHz, names);
        final byte[] bytes = message.getBytes(StandardCharsets.UTF_8);
        try
        {
            localSocket.send(new DatagramPacket(bytes, bytes.length, destination));
        }
        catch (final Exception ignored) {}
    }

    private static String sanitizeSession(final String value)
    {
        if (value == null || value.length() < 8 || value.length() > 32)
            return null;
        for (int i = 0; i < value.length(); ++i)
        {
            final char c = value.charAt(i);
            final boolean hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hex)
                return null;
        }
        return value.toLowerCase(Locale.ROOT);
    }
}

package dev.nullexo.motionengine;

import java.util.UUID;

import com.bitwig.extension.api.PlatformType;
import com.bitwig.extension.controller.AutoDetectionMidiPortNamesList;
import com.bitwig.extension.controller.ControllerExtension;
import com.bitwig.extension.controller.ControllerExtensionDefinition;
import com.bitwig.extension.controller.api.ControllerHost;

public final class MotionEngineBridgeExtensionDefinition extends ControllerExtensionDefinition
{
    private static final UUID ID = UUID.fromString("2d4e6901-8d4d-4f1d-9d12-4c9f89c2a7b1");

    @Override public String getName() { return "Motion Engine Bridge"; }
    @Override public String getAuthor() { return "Null Exo"; }
    @Override public String getVersion() { return "0.2.0"; }
    @Override public UUID getId() { return ID; }
    @Override public int getRequiredAPIVersion() { return 25; }
    @Override public String getHardwareVendor() { return "Motion Engine"; }
    @Override public String getHardwareModel() { return "Software Bridge"; }
    @Override public int getNumMidiInPorts() { return 0; }
    @Override public int getNumMidiOutPorts() { return 0; }

    @Override
    public void listAutoDetectionMidiPortNames(final AutoDetectionMidiPortNamesList list, final PlatformType platformType)
    {
        // Software-only extension: no MIDI ports to auto-detect.
    }

    @Override
    public ControllerExtension createInstance(final ControllerHost host)
    {
        return new MotionEngineBridgeExtension(this, host);
    }
}

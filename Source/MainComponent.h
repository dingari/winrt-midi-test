#pragma once

#include <JuceHeader.h>

#include <combaseapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Radios;
using namespace winrt::Windows::Devices::Midi;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

#define USE_JUCE_MIDI 1

namespace Util {
using PropertyStore = Collections::IMapView<winrt::hstring, IInspectable>;

template<typename T>
static auto getProperty(const PropertyStore& map, const winrt::hstring& key) -> std::optional<T>
{
    return map.HasKey(key)
           ? std::optional(winrt::unbox_value<T>(map.Lookup(key)))
           : std::nullopt;
}

template<typename T>
static auto getPropertyOr(const PropertyStore& map, const winrt::hstring& key, T def) -> T
{
    const auto p = getProperty<T>(map, key);

    return p.has_value() ? *p : def;
}
} // namespace Util

class WinRTMidiInput
{
public:
    explicit WinRTMidiInput(String id, const String& winrtId) : identifier(std::move(id))
    {
        constexpr auto TimeoutMs = 2000;

        const auto op    = MidiInPort::FromIdAsync(winrt::to_hstring(winrtId.toStdString()));
        const auto start = Time::getMillisecondCounter();
        auto       now   = start;

        while (op.Status() == AsyncStatus::Started && (now - start) <= TimeoutMs)
        {
            Thread::sleep(50);
            now = Time::getMillisecondCounter();
        }

        if (op.Status() != AsyncStatus::Completed)
        {
            if (op.Status() == AsyncStatus::Started && (now - start) >= TimeoutMs)
                DBG("Timed out waiting for Midi port creation " << winrtId);
        }
        else if (port = op.GetResults();  port != nullptr)
        {
            DBG("Midi port opened successfully " << String(winrt::to_string(port.DeviceId())));

            port.MessageReceived([this](const MidiInPort&, const MidiMessageReceivedEventArgs& args)
            {
                const auto bytes = args.Message().RawData();
                const auto msg   = MidiMessage(bytes.data(), bytes.Length());
                DBG("Midi message received: " << msg.getDescription());

                if (messageReceived)
                    messageReceived(msg);
            });
        }
    }

    ~WinRTMidiInput()
    {
        if (port != nullptr)
            port.Close();
    }

    std::function<void(const MidiMessage&)> messageReceived;

    [[nodiscard]] const String& getIdentifier() const { return identifier; }

private:
    const String identifier;

    MidiInPort port{nullptr};
};

//==============================================================================
class MainComponent : public Component,
                      public MidiInputCallback,
                      private Timer,
                      private AsyncUpdater
{
public:
    auto openWinRTMidiInput(const String& identifier) -> std::unique_ptr<WinRTMidiInput>
    {
        const auto it = std::find_if(midiDevices.cbegin(), midiDevices.cend(), [&](const auto& d)
        {
            return d.containerID == identifier;
        });

        jassert(it != midiDevices.cend());

        return std::make_unique<WinRTMidiInput>(identifier, it->deviceID);
    }
    //==============================================================================
    MainComponent()
            : midiInputWatcher(createMidiDeviceWatcher()),
              bleDeviceWatcher(createBleDeviceWatcher())
    {
        midiInputWatcher.Added({this, &MainComponent::midiDeviceAdded});
        midiInputWatcher.Removed({this, &MainComponent::midiDeviceRemoved});

        bleDeviceWatcher.Added({this, &MainComponent::bleDeviceAdded});
        bleDeviceWatcher.Updated({this, &MainComponent::bleDeviceUpdated});
        bleDeviceWatcher.Removed({this, &MainComponent::bleDeviceRemoved});

        midiMonitor.setMultiLine(true);
        midiMonitor.setReturnKeyStartsNewLine(false);
        midiMonitor.setReadOnly(true);
        midiMonitor.setScrollbarsShown(true);
        midiMonitor.setCaretVisible(false);
        midiMonitor.setPopupMenuEnabled(false);
        midiMonitor.setText({});
        addAndMakeVisible(midiMonitor);


        for (auto* w : {&midiInputWatcher, &bleDeviceWatcher})
            w->Start();

        startTimer(1000);

        centreWithSize(200, 400);
    }

    ~MainComponent() override = default;

    //==============================================================================
    void paint(Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        midiMonitor.setBounds(getLocalBounds());
    }

    //==================================================================================================================
    void midiDeviceAdded(const DeviceWatcher&, const DeviceInformation& added)
    {
        WinRTMidiDeviceInfo info{};
        info.deviceID = winrt::to_string(added.Id());

        DBG ("Detected MIDI device: " << info.deviceID);

        if (!added.IsEnabled())
        {
            DBG ("MIDI device not enabled: " << info.deviceID);
            return;
        }

        if (const auto container_id = Util::getProperty<winrt::guid>(added.Properties(), L"System.Devices.ContainerId"))
            info.containerID = winrt::to_string(winrt::to_hstring(*container_id));

        info.name      = winrt::to_string(added.Name());
        info.isDefault = added.IsDefault();

        DBG("Adding MIDI device: " << info.deviceID << " " << info.containerID << " " << info.name);

        const ScopedLock lock(deviceChanges);
        midiDevices.emplace_back(std::move(info));
    }

    void midiDeviceRemoved(const DeviceWatcher&, const DeviceInformationUpdate& removed)
    {
        const String removedDeviceId(winrt::to_string(removed.Id()));

        DBG ("Removing MIDI device: " << removedDeviceId);

        const ScopedLock lock(deviceChanges);
        const auto       it = std::remove_if(midiDevices.begin(), midiDevices.end(),
                [&](const auto& d) { return d.deviceID == removedDeviceId; });

        midiDevices.erase(it);
    }

    //==================================================================================================================
    void bleDeviceAdded(const DeviceWatcher&, const DeviceInformation& added)
    {
        const String deviceID(winrt::to_string(added.Id()));
        const String deviceName(winrt::to_string(added.Name()));

        DBG("Detected paired BLE device: " << deviceID << ", " << deviceName);

        const auto props = added.Properties();

        if (const auto id = Util::getProperty<winrt::guid>(props, L"System.Devices.Aep.ContainerId"); id.has_value())
        {
            if (const String id_str = winrt::to_string(winrt::to_hstring(*id)); id_str.isNotEmpty())
            {
                const BleDeviceInfo info = {
                        .containerID = id_str,
                        .isConnected = Util::getPropertyOr<bool>(props, L"System.Devices.Aep.IsConnected", {})
                };

                DBG("Adding BLE device: " << deviceID << " " << info.containerID << ", name: " << deviceName
                                          << " " << (info.isConnected ? "connected" : "disconnected"));

                const ScopedLock lock(deviceChanges);
                bleDevices.insert_or_assign(deviceID, info);
            }
        }
    }

    void bleDeviceRemoved(const DeviceWatcher&, const DeviceInformationUpdate& removed)
    {
        const auto removedDeviceId = String(winrt::to_string(removed.Id()));

        DBG("Removing BLE device: " << removedDeviceId);

        if (bleDevices.contains(removedDeviceId))
            bleDevices.erase(removedDeviceId);
    }

    void bleDeviceUpdated(const DeviceWatcher&, const DeviceInformationUpdate& updated)
    {
        DBG("Device updated: " << String(winrt::to_string(updated.Id())));

        const auto updatedDeviceId = String(winrt::to_string(updated.Id()));

        DBG("Updated properties:");
        for (const auto& p : updated.Properties())
            DBG("  " << String(winrt::to_string(p.Key())));

        if (const auto opt = Util::getProperty<bool>(updated.Properties(), L"System.Devices.Aep.IsConnected"); opt.has_value())
        {
            const bool is_connected = *opt;

            DBG("Is connected? " << (is_connected ? "Yes" : "No"));

            const ScopedLock lock(deviceChanges);

            if (bleDevices.contains(updatedDeviceId))
            {
                auto& info = bleDevices.at(updatedDeviceId);

                if (info.isConnected != is_connected)
                    DBG("BLE device connection status change: " << updatedDeviceId << " " << info.containerID << " " << (is_connected ? "connected" : "disconnected"));

                info.isConnected = is_connected;
            }
        }
    }

    void handleIncomingMidiMessage(MidiInput*, const MidiMessage& msg) override
    {
        {
            const ScopedLock lock(midiMessageLock);
            incomingMessages.push_back(msg);
        }

        triggerAsyncUpdate();
    }

private:
    void handleAsyncUpdate() override
    {
        std::vector<MidiMessage> messages;

        {
            const ScopedLock lock(midiMessageLock);
            messages.swap(incomingMessages);
        }

        String text = "";
        for (const auto& msg : messages)
            text << msg.getDescription() << "\n";

        midiMonitor.insertTextAtCaret(text);
    }

    void timerCallback() override
    {
#if USE_JUCE_MIDI
        const ScopedLock lock(deviceChanges);
        const auto ds = MidiInput::getAvailableDevices();

        std::vector<MidiDeviceInfo> v(ds.size());
        std::transform(ds.begin(), ds.end(), v.begin(), [](const auto& d) { return d; });

        lastQueriedAvailableDevices = std::move(v);
#else
        const ScopedLock lock(deviceChanges);
        lastQueriedAvailableDevices = getAvailableDevices();
#endif

#if USE_JUCE_MIDI
        for (const auto&[name, id] : MidiInput::getAvailableDevices())
        {
            const auto it = std::find_if(midiInputs.begin(), midiInputs.end(), [id = id](const auto& mp)
            {
                return mp->getIdentifier() == id;
            });

            if (it == midiInputs.end())
            {
                DBG("Opening midi device: " << id << " " << name);

                if (auto input = MidiInput::openDevice(id, this); input != nullptr)
                {
                    input->start();
                    midiInputs.push_back(std::move(input));
                }
            }
        }
#else
        for (const auto&[name, id] : lastQueriedAvailableDevices)
        {
            const auto it = std::find_if(midiPorts.begin(), midiPorts.end(), [id = id](const auto& mp)
            {
                return mp->getIdentifier() == id;
            });

            DBG("Open midi ports:");
            for (const auto& p : midiPorts)
                DBG("  " << p->getIdentifier());

            if (it == midiPorts.end())
            {
                DBG("Opening midi device: " << id << " " << name << ", num open ports: " << String(midiPorts.size()));

                if (auto port = openWinRTMidiInput(id); port != nullptr)
                {
                    port->messageReceived = [wr = WeakReference(this)](const MidiMessage& msg)
                    {
                        if (auto* p = wr.get())
                        {
                            const ScopedLock lock(p->midiMessageLock);
                            p->incomingMessages.push_back(msg);

                            p->triggerAsyncUpdate();
                        }
                    };

                    midiPorts.push_back(std::move(port));
                }
            }
        }
#endif
    }

    [[nodiscard]] std::vector<MidiDeviceInfo> getAvailableDevices() const
    {
        std::vector<MidiDeviceInfo> devices(midiDevices.size());
        std::transform(midiDevices.cbegin(), midiDevices.cend(), devices.begin(),
                [](const auto& d) { return MidiDeviceInfo{d.name, d.containerID}; });

        return devices;
    }

    static DeviceWatcher createMidiDeviceWatcher()
    {
        const std::vector<winrt::hstring> props{
                L"System.Devices.ContainerId",
                L"System.Devices.Aep.ContainerId",
                L"System.Devices.Aep.IsConnected"
        };

        return DeviceInformation::CreateWatcher(MidiInPort::GetDeviceSelector(), props, DeviceInformationKind::DeviceInterface);
    }

    static DeviceWatcher createBleDeviceWatcher()
    {
        const std::vector<winrt::hstring> props{
                L"System.Devices.ContainerId",
                L"System.Devices.Aep.ContainerId",
                L"System.Devices.Aep.IsConnected"
        };

        // bb7bb05e-5972-42b5-94fc-76eaa7084d49 is the Bluetooth LE protocol ID, by the way...
        constexpr auto selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\""
                                  " AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";

        return DeviceInformation::CreateWatcher(selector, props, DeviceInformationKind::AssociationEndpoint);
    }

    struct WinRTMidiDeviceInfo
    {
        String deviceID{}, containerID{}, name{};
        bool   isDefault = false;
    };

    struct BleDeviceInfo
    {
        String containerID;
        bool   isConnected;
    };

    CriticalSection                  deviceChanges;
    std::vector<WinRTMidiDeviceInfo> midiDevices;
    std::map<String, BleDeviceInfo>  bleDevices;

    std::vector<MidiDeviceInfo> lastQueriedAvailableDevices;

#if USE_JUCE_MIDI
    std::unique_ptr<MidiInput>              midiInput;
    std::vector<std::unique_ptr<MidiInput>> midiInputs;
#else
    std::unique_ptr<WinRTMidiInput>              midiPort;
    std::vector<std::unique_ptr<WinRTMidiInput>> midiPorts;
#endif

    TextEditor midiMonitor{};

    CriticalSection          midiMessageLock;
    std::vector<MidiMessage> incomingMessages;

    //==============================================================================
    DeviceWatcher midiInputWatcher, bleDeviceWatcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE (MainComponent)
};

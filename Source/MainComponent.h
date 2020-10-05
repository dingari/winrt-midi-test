#pragma once

#include <JuceHeader.h>

#include <combaseapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Devices;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Radios;
using namespace winrt::Windows::Devices::Midi;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

//======================================================================================================================
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

//======================================================================================================================
class WinRTMidiInput
{
public:
    using Callback = std::function<void(const MidiMessage&)>;

    explicit WinRTMidiInput(String id, const String& winrtId, Callback cb)
            : identifier(std::move(id)),
              callback(std::move(cb))
    {
        MidiInPort::FromIdAsync(winrt::to_hstring(winrtId.toStdString())).Completed(
                [this, winrtId](const IAsyncOperation<MidiInPort>& op, AsyncStatus status)
                {
                    if (op.Status() != AsyncStatus::Completed)
                    {
                        DBG("Failed to open midi port: " << winrtId);
                    }
                    else if (port = op.GetResults(); port != nullptr)
                    {
                        DBG("Midi port opened successfully " << String(winrt::to_string(port.DeviceId())));

                        port.MessageReceived(
                                [this](const MidiInPort&, const MidiMessageReceivedEventArgs& args)
                                {
                                    const auto bytes = args.Message().RawData();
                                    const auto msg   = MidiMessage(bytes.data(), bytes.Length());

                                    if (callback)
                                        callback(msg);
                                }
                        );
                    }
                }
        );
    }

    ~WinRTMidiInput()
    {
        if (port != nullptr)
            port.Close();
    }

    [[nodiscard]] const String& getIdentifier() const { return identifier; }

private:
    const String identifier;

    MidiInPort port{nullptr};

    Callback callback;
};

//======================================================================================================================
struct BleDevice
{
    using Callback = std::function<void(std::vector<uint8_t>)>;

    explicit BleDevice(const String& id, Callback cb) : callback(std::move(cb))
    {
        jassert(callback != nullptr);

        DBG("Connecting to BLE device: " << id);

        BluetoothLEDevice::FromIdAsync(winrt::to_hstring(id.toStdString())).Completed(
                [this, id](const IAsyncOperation<BluetoothLEDevice>& sender, AsyncStatus status)
                {
                    if (status != AsyncStatus::Completed || sender.GetResults() == nullptr)
                    {
                        DBG("Failed to connect to device: " << id);
                        return;
                    }

                    device = sender.GetResults();
                    device.GetGattServicesAsync().Completed({this, &BleDevice::getGattServicesCompleted});
                }
        );
    }

private:
    //==================================================================================================================
    void getGattServicesCompleted(const IAsyncOperation<GattDeviceServicesResult>& sender, AsyncStatus status)
    {
        if (status != AsyncStatus::Completed)
        {
            DBG("Failed to get services");
            return;
        }

        const auto uuids = {
                L"{65e9296c-8dfb-11ea-bc55-0242ac130003}",
                L"{0e5a1523-ede8-4b33-a751-6ce34ec47c00}",
        };

        const auto services = sender.GetResults().Services();
        const auto it       = std::find_if(begin(services), end(services),
                [&](const GattDeviceService& s)
                {
                    return std::any_of(uuids.begin(), uuids.end(), [&](const auto& u) { return winrt::to_hstring(s.Uuid()) == u; });
                });

        if (it == end(services))
        {
            DBG("Failed to find service, available services: ");
            for (const auto& s : services)
                DBG("  " << String(winrt::to_string(winrt::to_hstring(s.Uuid()))));

            return;
        }

        service = *it;
        service.GetCharacteristicsAsync().Completed({this, &BleDevice::getCharacteristicsCompleted});
    }

    void getCharacteristicsCompleted(const IAsyncOperation<GattCharacteristicsResult>& sender, AsyncStatus status)
    {
        if (status != AsyncStatus::Completed)
        {
            DBG("Failed to get characteristics");
            return;
        }

        const auto uuids = {
                L"{65e92bb0-8dfb-11ea-bc55-0242ac130003}",
                L"{0e5a1525-ede8-4b33-a751-6ce34ec47c00}",
        };

        const auto chars = sender.GetResults().Characteristics();
        const auto it    = std::find_if(begin(chars), end(chars),
                [&](const GattCharacteristic& c)
                {
                    return std::any_of(uuids.begin(), uuids.end(), [&](const auto& u) { return winrt::to_hstring(c.Uuid()) == u; });
                });

        if (it == end(chars))
        {
            DBG("Failed to find characteristic, available characteristics:");
            for (const auto& c : chars)
                DBG("  " << String(winrt::to_string(winrt::to_hstring(c.Uuid()))));

            return;
        }

        charact = *it;
        charact.ValueChanged({this, &BleDevice::characteristicValueChanged});

        DBG("Got charcteristic successfully: " << String(winrt::to_string(winrt::to_hstring(charact.Uuid()))));

        const auto notify_type = GattClientCharacteristicConfigurationDescriptorValue::Notify;
        charact.WriteClientCharacteristicConfigurationDescriptorWithResultAsync(notify_type).Completed(
                [this](const IAsyncOperation<GattWriteResult>& sender, AsyncStatus status)
                {
                    if (status != AsyncStatus::Completed || sender.GetResults())
                    {
                        DBG("Failed to enable notifications");
                        return;
                    }

                    DBG("Notifications enabled successfully for characteristic: " << String(winrt::to_string(winrt::to_hstring(charact.Uuid()))));
                }
        );
    }

    void characteristicValueChanged(const GattCharacteristic& c, const GattValueChangedEventArgs& args)
    {
        const auto buf = args.CharacteristicValue();

        std::vector<uint8_t> packet(buf.Length());
        std::copy(buf.data(), buf.data() + buf.Length(), packet.begin());

        if (callback)
            callback(std::move(packet));
    }

    //==================================================================================================================
    BluetoothLEDevice  device{nullptr};
    GattDeviceService  service{nullptr};
    GattCharacteristic charact{nullptr};

    //==================================================================================================================
    Callback callback;
};

//======================================================================================================================
class MainComponent : public Component,
                      private Timer,
                      private AsyncUpdater
{
public:
    //==================================================================================================================
    MainComponent()
            : midiInputWatcher(createMidiDeviceWatcher()),
              bleDeviceWatcher(createBleDeviceWatcher())
    {
        midiInputWatcher.Added({this, &MainComponent::midiDeviceAdded});
        midiInputWatcher.Removed({this, &MainComponent::midiDeviceRemoved});

        bleDeviceWatcher.Added({this, &MainComponent::bleDeviceAdded});
        bleDeviceWatcher.Updated({this, &MainComponent::bleDeviceUpdated});
        bleDeviceWatcher.Removed({this, &MainComponent::bleDeviceRemoved});

        for (auto* w : {&midiInputWatcher, &bleDeviceWatcher})
            w->Start();

        startTimer(1000);
    }

    ~MainComponent() override = default;

    //==================================================================================================================
    void paint(Graphics& g) override
    {
        //==============================================================================================================
        const auto get_name = [this](const String& identifier)
        {
            const auto it = std::find_if(midiDeviceInfos.cbegin(), midiDeviceInfos.cend(),
                    [&](const auto& md) { return identifier == md.containerID; });

            return it == midiDeviceInfos.cend() ? "()" : it->name;
        };

        const auto get_midi_count = [this](const String& identifier)
        {
            const auto it = midiMessageCount.find(identifier);

            return it == midiMessageCount.end() ? 0 : it->second;
        };

        const auto get_ble_count = [this](const String& identifier)
        {
            const auto it = std::find_if(bleDeviceInfos.cbegin(), bleDeviceInfos.cend(),
                    [&](const auto& kv) { return kv.second.containerID == identifier; });

            if (it != bleDeviceInfos.cend())
            {
                const auto c_it = blePacketCount.find(it->first);

                return c_it == blePacketCount.end() ? 0 : c_it->second;
            }

            return 0;
        };

        //==============================================================================================================
        g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));

        auto       r = getLocalBounds();
        const auto w = r.proportionOfWidth(1.0 / 3.0);

        g.setColour(Colours::white);
        auto hdr = r.removeFromTop(30);
        for (const auto* t : {"Name", "Midi messages", "BLE packets"})
            g.drawText(t, hdr.removeFromLeft(w), Justification::left);

        const ScopedLock deviceLock(deviceChanges);
        const ScopedLock messageLock(midiMessageLock);
        const ScopedLock packetLock(blePacketLock);

        for (const auto& p : midiPorts)
        {
            auto row = r.removeFromTop(30);

            const auto& identifier = p->getIdentifier();
            const auto name       = get_name(identifier);
            const auto midi_count = String(get_midi_count(identifier));
            const auto ble_count  = String(get_ble_count(identifier));

            for (const auto* s : {&name, &midi_count, &ble_count})
                g.drawText(*s, row.removeFromLeft(w), Justification::left);
        }
    }

    void resized() override {}

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
        midiDeviceInfos.emplace_back(std::move(info));
    }

    void midiDeviceRemoved(const DeviceWatcher&, const DeviceInformationUpdate& removed)
    {
        const String removedDeviceId(winrt::to_string(removed.Id()));

        DBG ("Removing MIDI device: " << removedDeviceId);

        const ScopedLock lock(deviceChanges);
        const auto       it = std::remove_if(midiDeviceInfos.begin(), midiDeviceInfos.end(),
                [&](const auto& d) { return d.deviceID == removedDeviceId; });

        midiDeviceInfos.erase(it, midiDeviceInfos.end());
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
                bleDeviceInfos.insert_or_assign(deviceID, info);
            }
        }
    }

    void bleDeviceRemoved(const DeviceWatcher&, const DeviceInformationUpdate& removed)
    {
        const auto removedDeviceId = String(winrt::to_string(removed.Id()));

        DBG("Removing BLE device: " << removedDeviceId);

        const ScopedLock lock(deviceChanges);
        if (bleDeviceInfos.contains(removedDeviceId))
            bleDeviceInfos.erase(removedDeviceId);
    }

    void bleDeviceUpdated(const DeviceWatcher&, const DeviceInformationUpdate& updated)
    {
        DBG("Device updated: " << String(winrt::to_string(updated.Id())));

        const auto updatedDeviceId = String(winrt::to_string(updated.Id()));

        if (const auto opt = Util::getProperty<bool>(updated.Properties(), L"System.Devices.Aep.IsConnected"); opt.has_value())
        {
            const bool is_connected = *opt;

            DBG("Is connected? " << (is_connected ? "Yes" : "No"));

            const ScopedLock lock(deviceChanges);

            if (bleDeviceInfos.contains(updatedDeviceId))
            {
                auto& info = bleDeviceInfos.at(updatedDeviceId);

                if (info.isConnected != is_connected)
                {
                    DBG("BLE device connection status change: " << updatedDeviceId << " " << info.containerID << " " << (is_connected ? "connected" : "disconnected"));

                    if (is_connected)
                    {
                        if (const auto it = bleDevices.find(updatedDeviceId); it == bleDevices.end())
                        {
                            const auto callback = [wr = WeakReference(this), updatedDeviceId](auto bytes)
                            {
                                if (auto* p = wr.get())
                                    p->handleIncomingBlePacket(updatedDeviceId, bytes);
                            };

                            bleDevices.emplace(std::make_pair(updatedDeviceId, std::make_unique<BleDevice>(updatedDeviceId, callback)));
                        }
                    }
                    else if (const auto it = bleDevices.find(updatedDeviceId); it != bleDevices.end())
                    {
                        bleDevices.erase(it);

                        const auto& id      = info.containerID;

                        const ScopedLock midiLock(deviceChanges);
                        const auto       mp = std::find_if(midiPorts.begin(), midiPorts.end(),
                                [&](const auto& mp) { return mp->getIdentifier() == id; });

                        DBG("Closing midi device: " << id);
                        midiPorts.erase(mp);
                    }
                }

                info.isConnected = is_connected;
            }
        }
    }

    void handleIncomingMidiMessage(const String& deviceIdentifier, const MidiMessage& msg)
    {
        {
            const ScopedLock lock(midiMessageLock);

            if (const auto it = midiMessageCount.find(deviceIdentifier); it != midiMessageCount.end())
                it->second++;
            else
                midiMessageCount.insert(std::make_pair(deviceIdentifier, 1));
        }

        triggerAsyncUpdate();
    }

    void handleIncomingBlePacket(const String& deviceId, const std::vector<uint8_t>&)
    {
        {
            const ScopedLock lock(blePacketLock);

            if (const auto it = blePacketCount.find(deviceId); it != blePacketCount.end())
                it->second++;
            else
                blePacketCount.insert(std::make_pair(deviceId, 1));
        }

        triggerAsyncUpdate();
    }

private:
    //==================================================================================================================
    void handleAsyncUpdate() override { repaint(); }

    void timerCallback() override
    {
        const ScopedLock lock(deviceChanges);

        lastQueriedAvailableDevices.resize(midiDeviceInfos.size());
        std::transform(midiDeviceInfos.cbegin(), midiDeviceInfos.cend(), lastQueriedAvailableDevices.begin(),
                [](const auto& d) { return MidiDeviceInfo{d.name, d.containerID}; });

        for (const auto&[name, id] : lastQueriedAvailableDevices)
        {
            const auto it = std::find_if(midiPorts.begin(), midiPorts.end(), [id = id](const auto& mp)
            {
                return mp->getIdentifier() == id;
            });

            if (it == midiPorts.end())
            {
                DBG("Opening midi device: " << id << " " << name << ", num open ports: " << String(midiPorts.size()));

                const auto callback = [wr = WeakReference(this), id = id](const MidiMessage& msg)
                {
                    if (auto* p = wr.get())
                        p->handleIncomingMidiMessage(id, msg);
                };

                if (auto port = openWinRTMidiInput(id, callback); port != nullptr)
                    midiPorts.push_back(std::move(port));
            }
        }
    }

    //==================================================================================================================
    auto openWinRTMidiInput(const String& identifier, const WinRTMidiInput::Callback& callback) const -> std::unique_ptr<WinRTMidiInput>
    {
        const ScopedLock lock(deviceChanges);

        const auto it = std::find_if(midiDeviceInfos.cbegin(), midiDeviceInfos.cend(), [&](const auto& d)
        {
            return d.containerID == identifier;
        });

        jassert(it != midiDeviceInfos.cend());

        return std::make_unique<WinRTMidiInput>(identifier, it->deviceID, callback);
    }

    //==================================================================================================================
    static DeviceWatcher createMidiDeviceWatcher()
    {
        return createWatcher(MidiInPort::GetDeviceSelector(), DeviceInformationKind::DeviceInterface);
    }

    static DeviceWatcher createBleDeviceWatcher()
    {
        // bb7bb05e-5972-42b5-94fc-76eaa7084d49 is the Bluetooth LE protocol ID, by the way...
        constexpr auto selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\""
                                  " AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";

        return createWatcher(selector, DeviceInformationKind::AssociationEndpoint);
    }

    static DeviceWatcher createWatcher(const winrt::hstring& selector, DeviceInformationKind kind)
    {
        const std::vector<winrt::hstring> props{
                L"System.Devices.ContainerId",
                L"System.Devices.Aep.ContainerId",
                L"System.Devices.Aep.IsConnected"
        };

        return DeviceInformation::CreateWatcher(selector, props, kind);
    }

    //==================================================================================================================
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

    //==================================================================================================================
    CriticalSection                  deviceChanges;
    std::vector<WinRTMidiDeviceInfo> midiDeviceInfos;
    std::map<String, BleDeviceInfo>  bleDeviceInfos;

    std::vector<MidiDeviceInfo> lastQueriedAvailableDevices;

    std::vector<std::unique_ptr<WinRTMidiInput>> midiPorts;
    std::map<String, std::unique_ptr<BleDevice>> bleDevices;

    CriticalSection                   midiMessageLock, blePacketLock;
    std::vector<MidiMessage>          incomingMidiMessages;
    std::vector<std::vector<uint8_t>> incomingBlePackets;

    std::map<String, int> midiMessageCount, blePacketCount;

    //==================================================================================================================
    DeviceWatcher midiInputWatcher, bleDeviceWatcher;

    AudioDeviceManager deviceManager;

    //==================================================================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE (MainComponent)
};

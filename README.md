# WinRT/C++ - Concurrent MIDI/BLE communications

This is a sample application written using the [JUCE](https://github.com/juce-framework/JUCE) framework (just for GUI and some simple utility) and the [WinRT/C++](https://en.wikipedia.org/wiki/C%2B%2B/WinRT) library. 
It demonstrates a pretty strange scenario where a connection to more than one [MidiInPort](https://docs.microsoft.com/en-us/uwp/api/windows.devices.midi.midiinport?view=winrt-19041) and their corresponding [BluetoothLEDevice](https://docs.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.bluetoothledevice?view=winrt-19041) instance seems to severly limit the throughput of other such connections.

This sample program is performing roughly these steps:
* Check for available MIDI devices using a [DeviceWatcher](https://docs.microsoft.com/en-us/uwp/api/windows.devices.enumeration.devicewatcher?view=winrt-19041).
* Check for available Bluetooth LE devices using another instance of a DeviceWatcher.
* Match discovered MIDI and BluetoothLE devices on their ContainerId property (see [DeviceInfo](https://docs.microsoft.com/en-us/windows/uwp/devices-sensors/device-information-properties) for details). This is the method JUCE employs in the [native WinRT code](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/juce_win32_Midi.cpp) for their library, and works as expected.
* Open the MIDI port and attach a handler to the [MessageReceived](https://docs.microsoft.com/en-us/uwp/api/windows.devices.midi.midiinport.messagereceived?view=winrt-19041) event ([see the code](https://github.com/dingari/winrt-midi-test/blob/e1f0037db039cb8be4ae5be23220c3813695a76d/Source/MainComponent.h#L67)).
* This causes the system to create a connection to the Bluetooth LE device. The program detects this state change, creates a BluetoothLEDevice, we perform GATT service discovery and attach a handler to the [ValueChanged](https://docs.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattcharacteristic.valuechanged?view=winrt-19041) event for the characteristic we're interested in notifications from ([see the code](https://github.com/dingari/winrt-midi-test/blob/e1f0037db039cb8be4ae5be23220c3813695a76d/Source/MainComponent.h#L189)).

The program then counts how many MIDI messages are received on each port and how many BLE notifications are received from the corresponding device.

The behaviour we notice is that data from the most recently connected device streams just fine, while the throughput for the others is severly limited. We are at quite a standstill regarding this issue, and are not sure where the problem may lie.

Below are screenshots of two sample runs (about 10s each) with two devices sending MIDI messages and BLE notifications through a proprietary service. Notice that the "most recently connected" (further down the list) device recevies a bit more MIDI messages, but far more BLE packets.

![alt text](.github/winrt_midi_test_1.PNG "Test run #1 - White (Bluetooth MIDI IN) drops BLE packets")
![alt text](.github/winrt_midi_test_2.PNG "Test run #2 - Wave (Bluetooth MIDI IN) drops BLE packets")

## To build & run
The setup for this example is a bit specific, as you'd need at least two BLE MIDI devices that are actively sending MIDI and BLE notifications through a proprietary service at a pretty high rate. But I'll include this section for completeness.

* Run `git submodule update --init` to get the JUCE framework.
* You'll probably need to install the [Windows 10 SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/) as well, if you haven't already.

This is a CMake based project, so you can either load it up in CLion, Visual Studio, etc. Or alternatively from the command line:
```
cmake . -b cmake-build
cmake --build cmake-build --target WinRTMidiTest
```

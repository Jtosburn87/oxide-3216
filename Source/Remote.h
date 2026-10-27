#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "RemoteServer.h"
#include "Desk.h"

/** The Wi-Fi remote: a phone or tablet on the same network scans a QR code and gets the tape machine in its browser -
    the counter, the transport, record arm for the 16 tracks, the jog / shuttle wheel, the In / Out keys and the locate
    memories. Oxide serves the page itself (no internet, nothing to install) on `port` of every network adapter; every
    request needs the random key in the link. The page works on old iPads too (iOS 12 Safari).

    The remote plays the console's Jog / shuttle box (Store, the memories and its messages are the same ones).
    It also has the channel strips: In 1-8 / 9-16 and Out (the tape returns) 1-8 / 9-16, every knob and switch of the
    console strip, the fader and meters. A fader or switch moved there acts as by hand on the console (colour groups
    follow). */
class Remote : public juce::ChangeBroadcaster,
               private juce::Timer
{
public:
    static constexpr int port = 52819;   // Stinger's remote is 52818, so both can run on one computer

    Remote (AudioEngine&, juce::PropertiesFile& settings, std::function<Desk&()> desk, std::function<void()> saveSettings);
    ~Remote() override;

    /** Turns it on if it was on when Oxide last closed. */
    void restore();
    bool isOn() const { return server != nullptr; }
    void setOn (bool on);
    /** A new key: old links stop working and connected phones are dropped. */
    void newKey();

    struct Address
    {
        juce::String ip, adapter;
    };
    /** This computer's IPv4 addresses, the likeliest one for phones first (hotspot, then Wi-Fi). */
    static std::vector<Address> addresses();
    /** The address in the QR code: the one picked, else the likeliest. */
    juce::String getAddress() const;
    void setAddress (const juce::String& ip);
    juce::String url() const;

    int numClients() const { return server != nullptr ? server->numClients() : 0; }
    bool failed() const { return server != nullptr && server->failed(); }

    /** The state the page draws from (JSON). */
    juce::String stateJson() const;
    /** A message from the page. */
    void handle (const juce::var& message);
    /** The channel strips' controls (ids, ranges), their values, and the meters (JSON), for the page. */
    juce::String schemaJson() const;
    juce::String stripsJson();
    juce::String metersJson();

private:
    void start();
    void timerCallback() override;   // lets go of the shuttle ring if the phone holding it goes quiet
    void changed();
    void readBanks (bool);   // keeps the console's banks taking meter readings while the remote is on
    void setStripParam (const juce::String& id, float value);
    juce::String formatTime (juce::int64 samples) const;

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    std::function<Desk&()> desk;
    ShuttlePanel& shuttleBox() { return desk().getShuttle(); }
    std::function<void()> saveSettings;
    std::unique_ptr<RemoteServer> server;
    int heldShuttleStep = 0;          // the phone's shuttle ring, while it holds it off centre
    juce::uint32 heldShuttleSince = 0;
    bool readingBanks = false;
    std::uint32_t stripsChangeCount = 0;
    juce::String stripsNames, stripsText;   // the last strips message, rebuilt when a control, name or colour changes
};

/** The Wi-Fi remote window: on/off, the network for the QR code, the QR code and link, connected phones. */
class RemotePanel : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit RemotePanel (Remote&);
    ~RemotePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens the window (or brings it to the front). */
    static void show (Remote&);
    /** Closes the window (before the Remote goes). */
    static void closeWindow() { delete window.getComponent(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void fillAddresses();

    Remote& remote;
    juce::ToggleButton on { "Wi-Fi remote on: phones and tablets on this network can run the tape machine" };
    juce::Label networkLabel { {}, "Network" }, status, help;
    juce::ComboBox network;
    juce::TextButton rescan { "Rescan" }, copy { "Copy link" }, renew { "New link" }, hotspot { "Mobile hotspot settings..." };
    juce::TextEditor link;
    juce::Image qr;
    juce::Rectangle<int> qrArea;
    std::vector<Remote::Address> shownAddresses;

    static juce::Component::SafePointer<juce::DocumentWindow> window;
};

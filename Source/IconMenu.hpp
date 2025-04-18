//
//  IconMenu.hpp
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas on 12/26/15.
//  Modified for NovaHost April 18, 2025
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

#include <JuceHeader.h>
#include <memory>
#include <mutex>
#include <vector>
#include <map>

juce::ApplicationProperties& getAppProperties();

class IconMenu : public juce::SystemTrayIconComponent, private juce::Timer, public juce::ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const juce::MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(juce::ChangeBroadcaster* changed) override;
    static juce::String getKey(juce::String type, juce::PluginDescription plugin);

    const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
private:
    #if JUCE_MAC
    std::string exec(const char* cmd);
    #endif
    void timerCallback() override;
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void startAudioDevice();
    void loadAllPluginLists();
    void savePluginStates();
    void deletePluginStates();
    void clearBlacklist();
    juce::PluginDescription getNextPluginOlderThanTime(int &time);
    void removePluginsLackingInputOutput();
    std::vector<juce::PluginDescription> getTimeSortedList();
    void setIcon();
    
    // Plugin blacklisting and safe scanning functionality
    void loadPluginBlacklist();
    void savePluginBlacklist();
    void blacklistPlugin(const juce::PluginDescription& plugin);
    bool isPluginBlacklisted(const juce::String& pluginId) const;
    void safePluginScan(juce::AudioPluginFormat* format, const juce::String& formatName);
    
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::KnownPluginList activePluginList;
    juce::KnownPluginList::SortMethod pluginSortMethod;
    juce::PopupMenu menu;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    bool menuIconLeftClicked;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph::Node* inputNode;
    juce::AudioProcessorGraph::Node* outputNode;
    juce::StringArray pluginBlacklist;
    mutable std::mutex blacklistMutex;
    std::mutex pluginLoadMutex; // For safely accessing plugin lists
    #if JUCE_WINDOWS
    int x, y;
    #endif

    class PluginListWindow;
    std::unique_ptr<PluginListWindow> pluginListWindow;
};

#endif /* IconMenu_hpp */

//
//  IconMenu.hpp
//  Nova Host
//
//  Created by Rolando Islas on 12/26/15.
//  Modified for NovaHost fork 2025
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

#include <memory>
#include <mutex>

ApplicationProperties& getAppProperties();

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
	static String getKey(String type, PluginDescription plugin);

	const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
private:
	#if JUCE_MAC
    std::string exec(const char* cmd);
	#endif
    void timerCallback();
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
	PluginDescription getNextPluginOlderThanTime(int &time);
	void removePluginsLackingInputOutput();
	std::vector<PluginDescription> getTimeSortedList();
	void setIcon();
    
    // Plugin blacklisting and safe scanning functionality
    void loadPluginBlacklist();
    void savePluginBlacklist();
    void blacklistPlugin(const PluginDescription& plugin);
    bool isPluginBlacklisted(const String& pluginId) const;
    void safePluginScan(AudioPluginFormat* format, const String& formatName);
    
    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    std::unique_ptr<PluginDirectoryScanner> scanner;
    bool menuIconLeftClicked;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node *inputNode;
    AudioProcessorGraph::Node *outputNode;
    StringArray pluginBlacklist;
    mutable std::mutex blacklistMutex;
	#if JUCE_WINDOWS
	int x, y;
	#endif

	class PluginListWindow;
	std::unique_ptr<PluginListWindow> pluginListWindow;
    
    // Enhanced plugin scanning components
    class SafePluginScanner;
    friend class SafePluginScanner;
    class PluginScanDialog;
    std::unique_ptr<PluginScanDialog> scanDialog;
};

#endif /* IconMenu_hpp */

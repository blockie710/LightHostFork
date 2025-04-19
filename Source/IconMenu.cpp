//
//  IconMenu.cpp
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas on 12/26/15.
//  Modified for NovaHost April 18, 2025
//

#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include "SafePluginScanner.h"
#include "SplashScreen.h"
#include <ctime>
#include <limits>
#include <climits> // For INT_MAX
#if JUCE_WINDOWS
#include <Windows.h>
#endif

// All JUCE class references need juce:: prefix for proper namespace resolution
// Instead of using declarations, we'll use fully qualified names

class IconMenu::PluginListWindow : public juce::DocumentWindow
{
public:
    PluginListWindow(IconMenu& owner_, juce::AudioPluginFormatManager& pluginFormatManager)
        : juce::DocumentWindow("Available Plugins", juce::Colours::white,
            juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
        owner(owner_)
    {
        const juce::File deadMansPedalFile(getAppProperties().getUserSettings()
            ->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

        auto* listComponent = new juce::PluginListComponent(pluginFormatManager,
            owner.knownPluginList,
            deadMansPedalFile,
            getAppProperties().getUserSettings());
            
        // Custom scanner handling - remove if not available in this JUCE version
        #if JUCE_VERSION >= 0x060000
        listComponent->setCustomScanner(
            [this](juce::AudioPluginFormat* format) -> bool {
                juce::String formatName = format->getName();
                owner.safePluginScan(format, formatName);
                return true;
            });
        #endif

        setContentOwned(listComponent, true);

        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(300, 400, 800, 1500);
        setTopLeftPosition(60, 60);

        restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
        setVisible(true);
    }

    ~PluginListWindow() override
    {
        getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        juce::Process::setDockIconVisible(false);
        #endif
        owner.pluginListWindow = nullptr;
    }

private:
    IconMenu& owner;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000),
                       menuIconLeftClicked(false), inputNode(nullptr), outputNode(nullptr)
{
    // Initialization with explicit format registration rather than just defaults
    // This ensures all available plugin formats are supported
    #if JUCE_PLUGINHOST_VST
    formatManager.addFormat(new juce::VSTPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(new juce::VST3PluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AU
    formatManager.addFormat(new juce::AudioUnitPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_LADSPA
    formatManager.addFormat(new juce::LADSPAPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_LV2 && JUCE_LINUX
    formatManager.addFormat(new juce::LV2PluginFormat());
    #endif

    // Load blacklisted plugins if available
    loadPluginBlacklist();

    #if JUCE_WINDOWS
    x = y = 0;
    #endif
    
    // Audio device initialization
    startAudioDevice();
    
    // Plugins - load on a background thread to avoid UI stutter on startup
    juce::Thread::launch([this] {
        loadAllPluginLists();
        juce::MessageManager::callAsync([this] { 
            loadActivePlugins();
            setIcon();
            setIconTooltip(juce::JUCEApplication::getInstance()->getApplicationName());
        });
    });
}

void IconMenu::startAudioDevice()
{
    auto savedAudioState = std::unique_ptr<juce::XmlElement>(getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    
    // Setup audio with safe defaults first
    const int defaultNumInputChannels = 2;
    const int defaultNumOutputChannels = 2;
    
    // Initialize device manager with conservative settings first
    deviceManager.initialise(defaultNumInputChannels, defaultNumOutputChannels, 
                             savedAudioState.get(), true, {}, nullptr);
    
    // Set up graph processor and player
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
}

void IconMenu::loadAllPluginLists()
{
    std::unique_lock<std::mutex> lock(pluginLoadMutex);
    
    // Plugins - all available
    auto savedPluginList = std::unique_ptr<juce::XmlElement>(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = juce::KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    
    // Plugins - active in chain
    auto savedPluginListActive = std::unique_ptr<juce::XmlElement>(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    activePluginList.addChangeListener(this);
}

IconMenu::~IconMenu()
{
    // Properly shut down audio to prevent crashes on exit
    deviceManager.removeAudioCallback(&player);
    player.setProcessor(nullptr);
    
    // Save any plugin states before destruction
    savePluginStates();
}

void IconMenu::setIcon()
{
    // Set menu icon
    #if JUCE_MAC
        if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
            setIconImage(juce::ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
        else
            setIconImage(juce::ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
    #else
        juce::String defaultColor;
    #if JUCE_WINDOWS
        defaultColor = "white";
    #elif JUCE_LINUX
        defaultColor = "black";
    #endif
        if (!getAppProperties().getUserSettings()->containsKey("icon"))
            getAppProperties().getUserSettings()->setValue("icon", defaultColor);
        juce::String color = getAppProperties().getUserSettings()->getValue("icon");
        juce::Image icon;
        if (color.equalsIgnoreCase("white"))
            icon = juce::ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
        else if (color.equalsIgnoreCase("black"))
            icon = juce::ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
        setIconImage(icon);
    #endif
}

void IconMenu::loadActivePlugins()
{
    const int INPUT_NODE_ID = 1000000;
    const int OUTPUT_NODE_ID = INPUT_NODE_ID + 1;
    const int CHANNEL_ONE = 0;
    const int CHANNEL_TWO = 1;
    
    PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    
    // Create input/output nodes using proper API for current JUCE version
    inputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode)).get();
    
    outputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode)).get();
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection({{ inputNode->nodeID, CHANNEL_ONE }, { outputNode->nodeID, CHANNEL_ONE }});
        graph.addConnection({{ inputNode->nodeID, CHANNEL_TWO }, { outputNode->nodeID, CHANNEL_TWO }});
        return;
    }
    
    // Lock to prevent concurrent access to plugin list during load
    std::lock_guard<std::mutex> lock(pluginLoadMutex);
    
    struct PluginLoadJob {
        juce::PluginDescription plugin;
        juce::String stateKey;
        int nodeId;
        bool bypass;
    };
    
    // Prepare all plugin load jobs first
    std::vector<PluginLoadJob> loadJobs;
    int pluginTime = 0;
    
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
        juce::PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        juce::String pluginUid = getKey("state", plugin);
        juce::String key = getKey("bypass", plugin);
        bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        
        loadJobs.push_back({plugin, pluginUid, i, bypass});
    }
    
    // Load and connect all plugins
    juce::AudioProcessorGraph::Node* lastNode = nullptr;
    bool hasInputConnected = false;
    
    for (const auto& job : loadJobs)
    {
        juce::String errorMessage;
        std::unique_ptr<juce::AudioPluginInstance> instance = formatManager.createPluginInstance(
            job.plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
        
        if (instance == nullptr)
        {
            // Log the error and continue with the next plugin
            std::cerr << "Failed to create plugin instance for " << job.plugin.name << ": " << errorMessage << std::endl;
            continue;
        }
        
        // Apply saved state if available
        juce::String savedPluginState = getAppProperties().getUserSettings()->getValue(job.stateKey);
        if (savedPluginState.isNotEmpty())
        {
            juce::MemoryBlock savedPluginBinary;
            if (savedPluginBinary.fromBase64Encoding(savedPluginState))
            {
                // Protect against corrupt state data
                try
                {
                    instance->setStateInformation(savedPluginBinary.getData(), 
                                                static_cast<int>(savedPluginBinary.getSize()));
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error loading state for plugin " << job.plugin.name << ": " << e.what() << std::endl;
                }
            }
        }
        
        juce::AudioProcessorGraph::Node* currentNode = graph.addNode(std::move(instance)).get();
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection({{ inputNode->nodeID, CHANNEL_ONE }, { currentNode->nodeID, CHANNEL_ONE }});
            graph.addConnection({{ inputNode->nodeID, CHANNEL_TWO }, { currentNode->nodeID, CHANNEL_TWO }});
            hasInputConnected = true;
        }
        else if (lastNode != nullptr)
        {
            // Connect previous plugin to current
            graph.addConnection({{ lastNode->nodeID, CHANNEL_ONE }, { currentNode->nodeID, CHANNEL_ONE }});
            graph.addConnection({{ lastNode->nodeID, CHANNEL_TWO }, { currentNode->nodeID, CHANNEL_TWO }});
        }
        
        lastNode = currentNode;
    }
    
    // Connect last plugin to output
    if (lastNode != nullptr)
    {
        graph.addConnection({{ lastNode->nodeID, CHANNEL_ONE }, { outputNode->nodeID, CHANNEL_ONE }});
        graph.addConnection({{ lastNode->nodeID, CHANNEL_TWO }, { outputNode->nodeID, CHANNEL_TWO }});
    }
    else if (!hasInputConnected)
    {
        // Default connection if no plugins were loaded successfully
        graph.addConnection({{ inputNode->nodeID, CHANNEL_ONE }, { outputNode->nodeID, CHANNEL_ONE }});
        graph.addConnection({{ inputNode->nodeID, CHANNEL_TWO }, { outputNode->nodeID, CHANNEL_TWO }});
    }
}

juce::PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
    int pluginTime = INT_MAX;
    int pluginIndex = -1;
    
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        juce::String key = getKey("time", activePluginList.getType(i));
        juce::String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
        
        if (pluginTimeString.isEmpty())
            continue;
            
        int currentTime = pluginTimeString.getIntValue();
        
        if (currentTime > time && currentTime < pluginTime)
        {
            pluginTime = currentTime;
            pluginIndex = i;
        }
    }
    
    if (pluginIndex != -1)
    {
        time = pluginTime;
        return activePluginList.getType(pluginIndex);
    }
    
    return juce::PluginDescription();
}

void IconMenu::savePluginStates()
{
    auto xmlPluginListActive = std::unique_ptr<juce::XmlElement>(activePluginList.createXml());
    
    if (xmlPluginListActive != nullptr)
    {
        getAppProperties().getUserSettings()->setValue("pluginListActive", xmlPluginListActive.get());
        getAppProperties().getUserSettings()->saveIfNeeded();
    }
    
    auto xmlPluginList = std::unique_ptr<juce::XmlElement>(knownPluginList.createXml());
    
    if (xmlPluginList != nullptr)
    {
        getAppProperties().getUserSettings()->setValue("pluginList", xmlPluginList.get());
        getAppProperties().getUserSettings()->saveIfNeeded();
    }
}

void IconMenu::deletePluginStates()
{
    std::vector<juce::PluginDescription> plugins = getTimeSortedList();
    
    juce::ApplicationCommandManager* commandManager = juce::JUCEApplicationBase::getInstance()->getCommandManager();
    
    for (const auto& plugin : plugins)
    {
        juce::String pluginUid = getKey("state", plugin);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
    }
    
    activePluginList.clear();
    savePluginStates();
}

juce::String IconMenu::getKey(juce::String type, juce::PluginDescription plugin)
{
    return "plugin_" + type + "_" + plugin.createIdentifierString();
}

void IconMenu::mouseDown(const juce::MouseEvent& e)
{
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    
    menu.clear();
    
    if (e.mods.isLeftButtonDown())
    {
        juce::PopupMenu pluginsMenu;
        
        // Add plugins menu
        juce::String key = getKey("pluginsMenuClosed", juce::PluginDescription());
        bool pluginsMenuClosed = getAppProperties().getUserSettings()->getBoolValue(key, true);
        
        // First menu section - Add Plugin
        menu.addItem(1, "Add Plugin");
        menu.addSeparator();
        
        std::vector<juce::PluginDescription> plugins = getTimeSortedList();
        
        if (plugins.size() == 0)
            menu.addItem(-1, "No plugins in chain", false);
        else
        {
            // Add all plugins to menu
            for (int i = 0; i < (int)plugins.size(); i++)
            {
                juce::String pluginUid = getKey("uid", plugins[i]);
                int uid = getAppProperties().getUserSettings()->getIntValue(pluginUid, i + 2);
                
                juce::PopupMenu pluginSubMenu;
                
                // Edit plugin UI
                pluginSubMenu.addItem(INDEX_EDIT + uid, "Edit");
                
                // Bypass plugin
                juce::String key = getKey("bypass", plugins[i]);
                bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
                pluginSubMenu.addItem(INDEX_BYPASS + uid, "Bypass", true, bypass);
                
                // Delete plugin
                pluginSubMenu.addItem(INDEX_DELETE + uid, "Delete");
                
                // Position controls
                if (i > 0)
                    pluginSubMenu.addItem(INDEX_MOVE_UP + uid, "Move Up");
                if (i < (int)plugins.size() - 1)
                    pluginSubMenu.addItem(INDEX_MOVE_DOWN + uid, "Move Down");
                
                menu.addSubMenu(plugins[i].name, pluginSubMenu);
            }
            
            menu.addSeparator();
        }
        
        menu.addItem(2, "Delete All Plugins");
        menu.addSeparator();
        menu.addItem(3, "Audio Settings");
        
        // Set icon color menu item
        #if JUCE_WINDOWS || JUCE_LINUX
        juce::PopupMenu iconColorMenu;
        juce::String color = getAppProperties().getUserSettings()->getValue("icon");
        iconColorMenu.addItem(4, "Black", true, color.equalsIgnoreCase("black"));
        iconColorMenu.addItem(5, "White", true, color.equalsIgnoreCase("white"));
        menu.addSubMenu("Icon Color", iconColorMenu);
        #endif
        
        menu.addSeparator();
        menu.addItem(6, "Exit");
    }
    else if (e.mods.isRightButtonDown())
    {
        menu.addItem(1, "Show");
        menu.addItem(2, "Hide");
        menu.addSeparator();
        menu.addItem(3, "Exit");
    }
    
    juce::ScopedPointer<juce::MenuBarComponent> menuComponent;
    menu.showMenuAsync(juce::PopupMenu::Options(), 
                      juce::ModalCallbackFunction::create([this](int result) { menuInvocationCallback(result, this); }));
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    if (im->menuIconLeftClicked)
    {
        if (id == 0)
            return;
        else if (id == 1)
        {
            // Show plugin selection window
            if (im->pluginListWindow == nullptr)
                im->pluginListWindow.reset(new PluginListWindow(*im, im->formatManager));
            else
                im->pluginListWindow->toFront(true);
        }
        else if (id == 2)
            im->deletePluginStates();
        else if (id == 3)
            im->showAudioSettings();
        else if (id == 4)
        {
            im->getAppProperties().getUserSettings()->setValue("icon", "black");
            im->setIcon();
        }
        else if (id == 5)
        {
            im->getAppProperties().getUserSettings()->setValue("icon", "white");
            im->setIcon();
        }
        else if (id == 6)
            juce::JUCEApplicationBase::quit();
        else
        {
            // Handle plugin-specific actions
            if (id >= im->INDEX_EDIT && id < im->INDEX_BYPASS)
            {
                juce::String key = juce::String(id - im->INDEX_EDIT);
                for (int j = 0; j < im->activePluginList.getNumTypes(); j++)
                {
                    juce::String pluginUid = im->getKey("uid", im->activePluginList.getType(j));
                    int uid = im->getAppProperties().getUserSettings()->getIntValue(pluginUid, j + 2);
                    
                    if (uid == id - im->INDEX_EDIT)
                    {
                        juce::AudioPluginInstance* instance = nullptr;
                        
                        // Find the AudioProcessor for this plugin
                        for (auto node : im->graph.getNodes())
                        {
                            if (auto plugin = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor()))
                            {
                                if (plugin->getPluginDescription().createIdentifierString() == 
                                    im->activePluginList.getType(j).createIdentifierString())
                                {
                                    instance = plugin;
                                    break;
                                }
                            }
                        }
                        
                        if (instance != nullptr)
                        {
                            if (PluginWindow::getWindowFor(instance) != nullptr)
                                PluginWindow::getWindowFor(instance)->toFront(true);
                            else
                                new PluginWindow(instance);
                        }
                    }
                }
            }
            else if (id >= im->INDEX_BYPASS && id < im->INDEX_DELETE)
            {
                juce::String key = juce::String(id - im->INDEX_BYPASS);
                for (int j = 0; j < im->activePluginList.getNumTypes(); j++)
                {
                    juce::String pluginUid = im->getKey("uid", im->activePluginList.getType(j));
                    int uid = im->getAppProperties().getUserSettings()->getIntValue(pluginUid, j + 2);
                    
                    if (uid == id - im->INDEX_BYPASS)
                    {
                        juce::String keyToMove = im->getKey("bypass", im->activePluginList.getType(j));
                        bool valueAbove = !im->getAppProperties().getUserSettings()->getBoolValue(keyToMove, false);
                        im->getAppProperties().getUserSettings()->setValue(keyToMove, valueAbove);
                        im->loadActivePlugins();
                    }
                }
            }
            else if (id >= im->INDEX_DELETE && id < im->INDEX_MOVE_UP)
            {
                juce::String key = juce::String(id - im->INDEX_DELETE);
                for (int j = 0; j < im->activePluginList.getNumTypes(); j++)
                {
                    juce::String pluginUid = im->getKey("uid", im->activePluginList.getType(j));
                    int uid = im->getAppProperties().getUserSettings()->getIntValue(pluginUid, j + 2);
                    
                    if (uid == id - im->INDEX_DELETE)
                    {
                        im->activePluginList.removeType(j);
                        im->savePluginStates();
                        im->loadActivePlugins();
                    }
                }
            }
            else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_DOWN)
            {
                juce::String key = juce::String(id - im->INDEX_MOVE_UP);
                for (int j = 0; j < im->activePluginList.getNumTypes(); j++)
                {
                    juce::String pluginUid = im->getKey("uid", im->activePluginList.getType(j));
                    int uid = im->getAppProperties().getUserSettings()->getIntValue(pluginUid, j + 2);
                    
                    if (uid == id - im->INDEX_MOVE_UP)
                    {
                        juce::String keyToMove = im->getKey("time", im->activePluginList.getType(j));
                        juce::String valueAbove = im->getAppProperties().getUserSettings()->getValue(keyToMove);
                        juce::String keyAbove = im->getKey("time", im->activePluginList.getType(j - 1));
                        juce::String valueToMove = im->getAppProperties().getUserSettings()->getValue(keyAbove);
                        im->getAppProperties().getUserSettings()->setValue(keyToMove, valueToMove);
                        im->getAppProperties().getUserSettings()->setValue(keyAbove, valueAbove);
                        im->loadActivePlugins();
                    }
                }
            }
            else if (id >= im->INDEX_MOVE_DOWN)
            {
                juce::String key = juce::String(id - im->INDEX_MOVE_DOWN);
                for (int j = 0; j < im->activePluginList.getNumTypes(); j++)
                {
                    juce::String pluginUid = im->getKey("uid", im->activePluginList.getType(j));
                    int uid = im->getAppProperties().getUserSettings()->getIntValue(pluginUid, j + 2);
                    
                    if (uid == id - im->INDEX_MOVE_DOWN)
                    {
                        juce::String keyToMove = im->getKey("time", im->activePluginList.getType(j));
                        juce::String valueBelow = im->getAppProperties().getUserSettings()->getValue(keyToMove);
                        juce::String keyBelow = im->getKey("time", im->activePluginList.getType(j + 1));
                        juce::String valueToMove = im->getAppProperties().getUserSettings()->getValue(keyBelow);
                        im->getAppProperties().getUserSettings()->setValue(keyToMove, valueToMove);
                        im->getAppProperties().getUserSettings()->setValue(keyBelow, valueBelow);
                        im->loadActivePlugins();
                    }
                }
            }
        }
    }
    else
    {
        if (id == 1)
        {
            #if JUCE_WINDOWS
            ShowWindow((HWND)im->getPeer()->getNativeHandle(), SW_RESTORE);
            SetForegroundWindow((HWND)im->getPeer()->getNativeHandle());
            #elif JUCE_MAC
            // Mac-specific show code here if needed
            #endif
        }
        else if (id == 2)
        {
            #if JUCE_WINDOWS
            ShowWindow((HWND)im->getPeer()->getNativeHandle(), SW_HIDE);
            #elif JUCE_MAC
            // Mac-specific hide code here if needed
            #endif
        }
        else if (id == 3)
            juce::JUCEApplicationBase::quit();
    }
}

juce::String IconMenu::exec(const char* cmd)
{
    juce::String key = "cmd_";
    key += cmd;
    
    FILE* pipe = popen(cmd, "r");
    
    if (!pipe)
        return juce::String();
        
    char buffer[128];
    juce::String result;
    
    while (!feof(pipe))
        if (fgets(buffer, 128, pipe) != nullptr)
            result += buffer;
            
    pclose(pipe);
    return result;
}

void IconMenu::timerCallback()
{
    for (int j = 0; j < activePluginList.getNumTypes(); j++)
    {
        juce::String pluginUid = getKey("uid", activePluginList.getType(j));
    }
}

void IconMenu::changeListenerCallback(juce::ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        auto savedPluginList = std::unique_ptr<juce::XmlElement>(knownPluginList.createXml());
        
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList.get());
            getAppProperties().getUserSettings()->saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        auto savedPluginListActive = std::unique_ptr<juce::XmlElement>(activePluginList.createXml());
        
        if (savedPluginListActive != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginListActive", savedPluginListActive.get());
            getAppProperties().getUserSettings()->saveIfNeeded();
        }
        
        loadActivePlugins();
    }
}

void IconMenu::reloadPlugins()
{
    juce::PluginDirectoryScanner::applyBlacklistingsToKnownPluginList(knownPluginList, getAppProperties().getUserSettings());
}

void IconMenu::showAudioSettings()
{
    auto audioSettingsComponent = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2, true, false, true, false);
    audioSettingsComponent->setSize(500, 400);
    
    juce::DialogWindow::LaunchOptions o;
    o.content.setNonOwned(audioSettingsComponent);
    o.dialogTitle = "Audio Settings";
    o.componentToCentreAround = nullptr;
    o.dialogBackgroundColour = juce::Colour(0xFF323232);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    
    auto* window = o.create();
    window->setVisible(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
    for (int i = activePluginList.getNumTypes() - 1; i >= 0; i--)
    {
        juce::PluginDescription plugin = activePluginList.getType(i);
        
        if (plugin.numInputChannels == 0 || plugin.numOutputChannels == 0)
        {
            activePluginList.removeType(i);
            savePluginStates();
        }
    }
}

std::vector<juce::PluginDescription> IconMenu::getTimeSortedList()
{
    std::vector<juce::PluginDescription> plugins;
    int pluginTime = 0;
    
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        plugins.push_back(getNextPluginOlderThanTime(pluginTime));
    }
    
    return plugins;
}

void IconMenu::loadPluginBlacklist()
{
    juce::String blacklistStr = getAppProperties().getUserSettings()->getValue("pluginBlacklist");
    
    if (blacklistStr.isNotEmpty())
    {
        juce::StringArray tokens;
        tokens.addTokens(blacklistStr, "|", "");
        std::unique_lock<std::mutex> lock(blacklistMutex);
        pluginBlacklist = tokens;
    }
}

void IconMenu::savePluginBlacklist()
{
    juce::String blacklistStr = pluginBlacklist.joinIntoString("|");
    
    getAppProperties().getUserSettings()->setValue("pluginBlacklist", blacklistStr);
    getAppProperties().getUserSettings()->saveIfNeeded();
}

void IconMenu::clearBlacklist()
{
    juce::String pluginId = "";
    
    {
        std::unique_lock<std::mutex> lock(blacklistMutex);
        pluginBlacklist.clear();
    }
    
    savePluginBlacklist();
    
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
        "Plugin Blacklist", "Plugin blacklist has been cleared.");
}

void IconMenu::blacklistPlugin(const juce::PluginDescription& plugin)
{
    juce::String pluginId = plugin.createIdentifierString();
    
    {
        std::unique_lock<std::mutex> lock(blacklistMutex);
        if (pluginBlacklist.contains(pluginId))
            return;
            
        juce::String currentId = pluginId;
        if (currentId.isNotEmpty())
            pluginBlacklist.add(currentId);
    }
    
    juce::PluginDirectoryScanner::applyBlacklistingsToKnownPluginList(knownPluginList, getAppProperties().getUserSettings());
    savePluginBlacklist();
}

bool IconMenu::isPluginBlacklisted(const juce::String& pluginId) const
{
    std::unique_lock<std::mutex> lock(blacklistMutex);
    return pluginBlacklist.contains(pluginId);
}

void IconMenu::safePluginScan(juce::AudioPluginFormat* format, const juce::String& formatName)
{
    // Create splash screen for scan progress
    SplashScreen* splashScreen = new SplashScreen();
    
    // Only show scan UI if not running in background
    if (splashScreen != nullptr)
    {
        juce::DialogWindow* splashWindow = splashScreen->createAsDialog("Scanning " + formatName + " plugins", true);
        if (splashWindow != nullptr)
            splashWindow->setVisible(true);
    }
    
    // Use safe plugin scanner if available
    SafePluginScanner scanner;
    
    // Connect scanner to progress UI
    scanner.setProgressListener([splashScreen](float progress, const juce::String& message) {
        if (splashScreen != nullptr)
            splashScreen->setProgress(progress, message);
    });
    
    // Run scan in separate thread
    scanner.runThread();
    
    // Report results
    int numFound = scanner.getNumPluginsFound();
    
    juce::String message = formatName + " plugins scan completed:";
    
    if (scanner.wasScanCancelled())
    {
        message = "Scan cancelled by user";
    }
    else if (scanner.didScanTimeout())
    {
        message = "Scan timed out - some plugins may have been skipped";
    }
    else
    {
        message = juce::String(numFound) + " " + formatName + " plugins found";
    }
    
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
        "Plugin Scan Complete", message);
}

#if JUCE_WINDOWS
void IconMenu::reactivate(int x, int y)
{
    this->x = x;
    this->y = y;
    startTimer(200);
}
#endif

//
//  IconMenu.cpp
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas on 12/26/15.
//  Modified for NovaHost April 18, 2025
//

// Using the standard JUCE include path
#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include "SafePluginScanner.h"
#include "SplashScreen.h"
#include <ctime>
#include <limits.h>
#if JUCE_WINDOWS
#include <Windows.h>
#endif

// Using namespace to reduce repetitive typing
using namespace juce;

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
    PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
        : DocumentWindow("Available Plugins", Colours::white,
            DocumentWindow::minimiseButton | DocumentWindow::closeButton),
        owner(owner_)
    {
        const File deadMansPedalFile(getAppProperties().getUserSettings()
            ->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

        // Create a custom PluginListComponent that uses our safe scanning method
        auto* listComponent = new PluginListComponent(pluginFormatManager,
            owner.knownPluginList,
            deadMansPedalFile,
            getAppProperties().getUserSettings());

        // Modify the default scanner with our own safe scanner
        listComponent->setCustomScanner([this](AudioPluginFormat* format) {
            String formatName = format->getName();
            owner.safePluginScan(format, formatName);
            return true; // we've handled the scanning
        });

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
        Process::setDockIconVisible(false);
        #endif
        owner.pluginListWindow = nullptr;
    }

private:
    IconMenu& owner;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000)
{
    // Initialization with explicit format registration rather than just defaults
    // This ensures all available plugin formats are supported
    #if JUCE_PLUGINHOST_VST
    formatManager.addFormat(new VSTPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(new VST3PluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AU
    formatManager.addFormat(new AudioUnitPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_LADSPA
    formatManager.addFormat(new LADSPAPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_LV2 && JUCE_LINUX
    formatManager.addFormat(new LV2PluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AAX
    formatManager.addFormat(new AAXPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_ARA
    formatManager.addFormat(new ARAPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AU_AIRMUSIC && JUCE_MAC
    formatManager.addFormat(new AudioUnitv3PluginFormat());
    #endif

    // Load blacklisted plugins if available
    loadPluginBlacklist();

    #if JUCE_WINDOWS
    x = y = 0;
    #endif
    
    // Audio device initialization
    startAudioDevice();
    
    // Plugins - load on a background thread to avoid UI stutter on startup
    Thread::launch([this] {
        loadAllPluginLists();
        MessageManager::callAsync([this] { 
            loadActivePlugins();
            setIcon();
            setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
        });
    });
}

void IconMenu::startAudioDevice()
{
    auto savedAudioState = std::unique_ptr<XmlElement>(getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    
    // Setup audio with safe defaults first
    const int defaultNumInputChannels = 2;
    const int defaultNumOutputChannels = 2;
    const int defaultBufferSize = 512;
    
    // Initialize device manager with conservative settings first
    deviceManager.initialise(defaultNumInputChannels, defaultNumOutputChannels, 
                             savedAudioState.get(), true, String(), nullptr);
    
    // Set up graph processor and player
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
}

void IconMenu::loadAllPluginLists()
{
    std::unique_lock<std::mutex> lock(pluginLoadMutex);
    
    // Plugins - all available
    auto savedPluginList = std::unique_ptr<XmlElement>(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    
    // Plugins - active in chain
    auto savedPluginListActive = std::unique_ptr<XmlElement>(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
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
            setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
        else
            setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
    #else
        String defaultColor;
    #if JUCE_WINDOWS
        defaultColor = "white";
    #elif JUCE_LINUX
        defaultColor = "black";
    #endif
        if (!getAppProperties().getUserSettings()->containsKey("icon"))
            getAppProperties().getUserSettings()->setValue("icon", defaultColor);
        String color = getAppProperties().getUserSettings()->getValue("icon");
        Image icon;
        if (color.equalsIgnoreCase("white"))
            icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
        else if (color.equalsIgnoreCase("black"))
            icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
        setIconImage(icon);
    #endif
}

void IconMenu::loadActivePlugins()
{
    const int INPUT = 1000000;
    const int OUTPUT = INPUT + 1;
    const int CHANNEL_ONE = 0;
    const int CHANNEL_TWO = 1;
    
    PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    
    inputNode = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), NodeID(INPUT));
    outputNode = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), NodeID(OUTPUT));
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection({{NodeID(INPUT), CHANNEL_ONE}, {NodeID(OUTPUT), CHANNEL_ONE}});
        graph.addConnection({{NodeID(INPUT), CHANNEL_TWO}, {NodeID(OUTPUT), CHANNEL_TWO}});
        return;
    }
    
    // Lock to prevent concurrent access to plugin list during load
    std::lock_guard<std::mutex> lock(pluginLoadMutex);
    
    struct PluginLoadJob {
        PluginDescription plugin;
        String stateKey;
        int nodeId;
        bool bypass;
    };
    
    // Prepare all plugin load jobs first
    std::vector<PluginLoadJob> loadJobs;
    int pluginTime = 0;
    
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        String pluginUid = getKey("state", plugin);
        String key = getKey("bypass", plugin);
        bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        
        loadJobs.push_back({plugin, pluginUid, i, bypass});
    }
    
    // Load and connect all plugins
    int lastId = 0;
    bool hasInputConnected = false;
    
    for (const auto& job : loadJobs)
    {
        String errorMessage;
        std::unique_ptr<AudioPluginInstance> instance = formatManager.createPluginInstance(
            job.plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
        
        if (instance == nullptr)
        {
            // Log the error and continue with the next plugin
            std::cerr << "Failed to create plugin instance for " << job.plugin.name << ": " << errorMessage << std::endl;
            continue;
        }
        
        // Apply saved state if available
        String savedPluginState = getAppProperties().getUserSettings()->getValue(job.stateKey);
        if (savedPluginState.isNotEmpty())
        {
            MemoryBlock savedPluginBinary;
            if (savedPluginBinary.fromBase64Encoding(savedPluginState))
            {
                // Protect against corrupt state data
                try
                {
                    instance->setStateInformation(savedPluginBinary.getData(), savedPluginBinary.getSize());
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error loading state for plugin " << job.plugin.name << ": " << e.what() << std::endl;
                }
            }
        }
        
        AudioProcessorGraph::Node::Ptr node = graph.addNode(std::move(instance), NodeID(job.nodeId));
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection({{NodeID(INPUT), CHANNEL_ONE}, {NodeID(job.nodeId), CHANNEL_ONE}});
            graph.addConnection({{NodeID(INPUT), CHANNEL_TWO}, {NodeID(job.nodeId), CHANNEL_TWO}});
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection({{NodeID(lastId), CHANNEL_ONE}, {NodeID(job.nodeId), CHANNEL_ONE}});
            graph.addConnection({{NodeID(lastId), CHANNEL_TWO}, {NodeID(job.nodeId), CHANNEL_TWO}});
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection({{NodeID(lastId), CHANNEL_ONE}, {NodeID(OUTPUT), CHANNEL_ONE}});
        graph.addConnection({{NodeID(lastId), CHANNEL_TWO}, {NodeID(OUTPUT), CHANNEL_TWO}});
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection({{NodeID(INPUT), CHANNEL_ONE}, {NodeID(OUTPUT), CHANNEL_ONE}});
        graph.addConnection({{NodeID(INPUT), CHANNEL_TWO}, {NodeID(OUTPUT), CHANNEL_TWO}});
    }
}

PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
    int timeStatic = time;
    PluginDescription closest;
    int diff = INT_MAX;
    bool found = false;
    
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = *activePluginList.getType(i);
        String key = getKey("order", plugin);
        String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
        
        // Handle case where the value doesn't exist or isn't a number
        if (pluginTimeString.isEmpty())
            continue;
            
        int pluginTime = pluginTimeString.getIntValue();
        if (pluginTime > timeStatic && abs(timeStatic - pluginTime) < diff)
        {
            diff = abs(timeStatic - pluginTime);
            closest = plugin;
            time = pluginTime;
            found = true;
        }
    }
    
    if (!found && activePluginList.getNumTypes() > 0)
    {
        // If no plugin with a time greater than timeStatic was found, 
        // return the first plugin as a fallback
        closest = *activePluginList.getType(0);
    }
    
    return closest;
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        auto savedPluginList = std::unique_ptr<XmlElement>(knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            // Save plugin list in a background thread to prevent UI lag
            Thread::launch([savedPluginList = XmlElement(*savedPluginList)] {
                getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList);
                getAppProperties().saveIfNeeded();
            });
        }
    }
    else if (changed == &activePluginList)
    {
        auto savedPluginList = std::unique_ptr<XmlElement>(activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            Thread::launch([savedPluginList = XmlElement(*savedPluginList)] {
                getAppProperties().getUserSettings()->setValue("pluginListActive", savedPluginList);
                getAppProperties().saveIfNeeded();
            });
        }
    }
}

#if JUCE_MAC
std::string IconMenu::exec(const char* cmd)
{
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while (!feof(pipe.get()))
    {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    if (menuIconLeftClicked) {
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Edit Plugins");
        menu.addSeparator();
        menu.addSectionHeader("Active Plugins");
        // Active plugins
        int time = 0;
        for (int i = 0; i < activePluginList.getNumTypes(); i++)
        {
            PopupMenu options;
            options.addItem(INDEX_EDIT + i, "Edit");
            std::vector<PluginDescription> timeSorted = getTimeSortedList();
            String key = getKey("bypass", timeSorted[i]);
            bool bypass = getAppProperties().getUserSettings()->getBoolValue(key);
            options.addItem(INDEX_BYPASS + i, "Bypass", true, bypass);
            options.addSeparator();
            options.addItem(INDEX_MOVE_UP + i, "Move Up", i > 0);
            options.addItem(INDEX_MOVE_DOWN + i, "Move Down", i < timeSorted.size() - 1);
            options.addSeparator();
            options.addItem(INDEX_DELETE + i, "Delete");
            PluginDescription plugin = getNextPluginOlderThanTime(time);
            menu.addSubMenu(plugin.name, options);
        }
        menu.addSeparator();
        menu.addSectionHeader("Available Plugins");
        // All plugins
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
        menu.addSeparator();
        menu.addItem(2, "Delete Plugin States");
        menu.addItem(3, "Clear Blacklisted Plugins");
        #if !JUCE_MAC
            menu.addItem(4, "Invert Icon Color");
        #endif
    }
    
    #if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #else
    if (x == 0 || y == 0)
    {
        POINT iconLocation;
        iconLocation.x = 0;
        iconLocation.y = 0;
        if (GetCursorPos(&iconLocation))
        {
            // Apply DPI scaling to get accurate coordinates
            double scaleFactor = Desktop::getInstance().getDisplays().getDisplayContaining(Point<int>(iconLocation.x, iconLocation.y)).scale;
            x = static_cast<int>(iconLocation.x / scaleFactor);
            y = static_cast<int>(iconLocation.y / scaleFactor);
        }
        else
        {
            // Fallback in case GetCursorPos fails
            Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getDisplayContaining(Point<int>(0, 0)).userArea;
            x = screenArea.getCentreX();
            y = screenArea.getCentreY();
        }
    }
    Rectangle<int> rect(x, y, 1, 1);
    menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    #if JUCE_MAC
        Process::setDockIconVisible(true);
    #endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer(50);
}

void IconMenu::clearBlacklist()
{
    std::lock_guard<std::mutex> lock(blacklistMutex);
    pluginBlacklist.clear();
    getAppProperties().getUserSettings()->setValue("pluginBlacklist", String());
    getAppProperties().getUserSettings()->saveIfNeeded();
    
    // Show confirmation message
    AlertWindow::showMessageBox(AlertWindow::InfoIcon, 
                               "Blacklist Cleared", 
                               "The plugin blacklist has been cleared. All plugins will be available for scanning again.");
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // Right click
    if ((!im->menuIconLeftClicked))
    {
        if (id == 1)
        {
            im->savePluginStates();
            return JUCEApplication::getInstance()->quit();
        }
        if (id == 2)
        {
            im->deletePluginStates();
            return im->loadActivePlugins();
        }
        if (id == 3)
        {
            return im->clearBlacklist();
        }
        if (id == 4)
        {
            String color = getAppProperties().getUserSettings()->getValue("icon");
            getAppProperties().getUserSettings()->setValue("icon", color.equalsIgnoreCase("black") ? "white" : "black");
            return im->setIcon();
        }
    }
    
    #if JUCE_MAC
    // Click elsewhere
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
    #endif
    
    // Audio settings
    if (id == 1)
        im->showAudioSettings();
    // Reload
    if (id == 2)
        im->reloadPlugins();
    // Plugins
    if (id > 2)
    {
        // Delete plugin - run in background thread to avoid UI stutter
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            const int index = id - im->INDEX_DELETE;
            Thread::launch([im, index]() {
                // Need a copy of the sorted list since we're in a background thread
                im->deletePluginStates();
                std::vector<PluginDescription> timeSorted;
                
                {
                    std::lock_guard<std::mutex> lock(im->pluginLoadMutex);
                    timeSorted = im->getTimeSortedList();
                }
                
                // Check array bounds
                if (index >= 0 && index < timeSorted.size())
                {
                    String key = im->getKey("order", timeSorted[index]);
                    int unsortedIndex = -1;
                    
                    {
                        std::lock_guard<std::mutex> lock(im->pluginLoadMutex);
                        for (int i = 0; i < im->activePluginList.getNumTypes(); i++)
                        {
                            PluginDescription current = *im->activePluginList.getType(i);
                            if (key.equalsIgnoreCase(im->getKey("order", current)))
                            {
                                unsortedIndex = i;
                                break;
                            }
                        }
                    }
                    
                    // Fix the infinite loop bug by checking if we found a matching index
                    if (unsortedIndex >= 0)
                    {
                        // Remove plugin order
                        getAppProperties().getUserSettings()->removeValue(key);
                        // Remove bypass entry
                        getAppProperties().getUserSettings()->removeValue(im->getKey("bypass", timeSorted[index]));
                        getAppProperties().saveIfNeeded();
                        
                        // Remove plugin from list
                        {
                            std::lock_guard<std::mutex> lock(im->pluginLoadMutex);
                            im->activePluginList.removeType(unsortedIndex);
                        }
                        
                        // Update UI on the message thread
                        MessageManager::callAsync([im]() {
                            // Save current states
                            im->savePluginStates();
                            im->loadActivePlugins();
                            im->startTimer(50); // Refresh menu
                        });
                    }
                }
            });
        }
        // Add plugin
        else if (im->knownPluginList.getIndexChosenByMenu(id) > -1)
        {
            const int pluginIndex = im->knownPluginList.getIndexChosenByMenu(id);
            Thread::launch([im, pluginIndex]() {
                std::lock_guard<std::mutex> lock(im->pluginLoadMutex);
                PluginDescription plugin = *im->knownPluginList.getType(pluginIndex);
                String key = im->getKey("order", plugin);
                int t = time(0);
                getAppProperties().getUserSettings()->setValue(key, t);
                getAppProperties().saveIfNeeded();
                im->activePluginList.addType(plugin);
                
                MessageManager::callAsync([im]() {
                    im->savePluginStates();
                    im->loadActivePlugins();
                    im->startTimer(50); // Refresh menu
                });
            });
        }
        // Bypass plugin
        else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
        {
            int index = id - im->INDEX_BYPASS;
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            
            if (index >= 0 && index < timeSorted.size())
            {
                String key = im->getKey("bypass", timeSorted[index]);

                // Set bypass flag
                bool bypassed = getAppProperties().getUserSettings()->getBoolValue(key);
                getAppProperties().getUserSettings()->setValue(key, !bypassed);
                getAppProperties().saveIfNeeded();

                im->savePluginStates();
                im->loadActivePlugins();
            }
        }
        // Show active plugin GUI
        else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            int pluginIndex = id - im->INDEX_EDIT + 1;
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(NodeID(pluginIndex)))
                if (PluginWindow* const w = PluginWindow::getWindowFor(f, PluginWindow::Normal))
                    w->toFront(true);
        }
        // Move plugin up the list
        else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
        {
            im->savePluginStates();
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            
            // Verify the index is valid and not already at the top
            int index = id - im->INDEX_MOVE_UP;
            if (index > 0 && index < timeSorted.size())
            {
                PluginDescription pluginToMove = timeSorted[index];
                PluginDescription pluginAbove = timeSorted[index - 1];
                
                // Swap the order values
                String keyToMove = im->getKey("order", pluginToMove);
                String keyAbove = im->getKey("order", pluginAbove);
                
                String valueToMove = getAppProperties().getUserSettings()->getValue(keyToMove);
                String valueAbove = getAppProperties().getUserSettings()->getValue(keyAbove);
                
                getAppProperties().getUserSettings()->setValue(keyToMove, valueAbove);
                getAppProperties().getUserSettings()->setValue(keyAbove, valueToMove);
                getAppProperties().getUserSettings()->saveIfNeeded();
            }
            im->loadActivePlugins();
        }
        // Move plugin down the list
        else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
        {
            im->savePluginStates();
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            
            // Verify the index is valid and not already at the bottom
            int index = id - im->INDEX_MOVE_DOWN;
            if (index >= 0 && index < timeSorted.size() - 1)
            {
                PluginDescription pluginToMove = timeSorted[index];
                PluginDescription pluginBelow = timeSorted[index + 1];
                
                // Swap the order values
                String keyToMove = im->getKey("order", pluginToMove);
                String keyBelow = im->getKey("order", pluginBelow);
                
                String valueToMove = getAppProperties().getUserSettings()->getValue(keyToMove);
                String valueBelow = getAppProperties().getUserSettings()->getValue(keyBelow);
                
                getAppProperties().getUserSettings()->setValue(keyToMove, valueBelow);
                getAppProperties().getUserSettings()->setValue(keyBelow, valueToMove);
                getAppProperties().getUserSettings()->saveIfNeeded();
            }
            im->loadActivePlugins();
        }
        // Update menu
        im->startTimer(50);
    }
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
    int time = 0;
    std::vector<PluginDescription> list;
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
        list.push_back(getNextPluginOlderThanTime(time));
    return list;
}

juce::String IconMenu::getKey(juce::String type, juce::PluginDescription plugin)
{
    String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
    return key;
}

void IconMenu::deletePluginStates()
{
    // Use a background thread for I/O operations
    Thread::launch([this]() {
        std::vector<PluginDescription> list;
        {
            std::lock_guard<std::mutex> lock(pluginLoadMutex);
            list = getTimeSortedList();
        }
        
        for (int i = 0; i < list.size(); i++)
        {
            String pluginUid = getKey("state", list[i]);
            getAppProperties().getUserSettings()->removeValue(pluginUid);
        }
        
        // Save once at the end, not for each plugin
        getAppProperties().getUserSettings()->saveIfNeeded();
    });
}

void IconMenu::savePluginStates()
{
    // Use a background thread for potentially slow I/O operations
    Thread::launch([this]() {
        std::vector<PluginDescription> list;
        {
            std::lock_guard<std::mutex> lock(pluginLoadMutex);
            list = getTimeSortedList();
        }
        
        // Build all updates first, then apply in a batch
        std::map<String, String> updates;
        
        for (int i = 0; i < list.size(); i++)
        {
            // We need synchronized access to the graph
            MemoryBlock savedStateBinary;
            AudioProcessorGraph::Node::Ptr node = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(pluginLoadMutex);
                node = graph.getNodeForId(NodeID(i + 1));
            }
            
            if (node == nullptr)
                continue;
            
            // Get the state on the message thread to avoid threading issues
            MessageManager::getInstance()->callFunctionOnMessageThread([&](void*) -> void* {
                node->getProcessor()->getStateInformation(savedStateBinary);
                return nullptr;
            }, nullptr);
            
            if (savedStateBinary.getSize() > 0)
            {
                String pluginUid = getKey("state", list[i]);
                updates[pluginUid] = savedStateBinary.toBase64Encoding();
            }
        }
        
        // Apply all updates at once
        auto* settings = getAppProperties().getUserSettings();
        for (const auto& update : updates)
            settings->setValue(update.first, update.second);
        
        settings->saveIfNeeded();
    });
}

void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp(deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp.setSize(500, 450);
    
    DialogWindow::LaunchOptions o;
    o.content.setNonOwned(&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = false;

    o.runModal();
    auto audioState = std::unique_ptr<XmlElement>(deviceManager.createStateXml());
    getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState.get());
    getAppProperties().getUserSettings()->saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow>(*this, formatManager);
    pluginListWindow->toFront(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
    // Move this potentially slow operation to a background thread
    Thread::launch([this]() {
        std::vector<int> removeIndex;
        
        {
            std::lock_guard<std::mutex> lock(pluginLoadMutex);
            for (int i = 0; i < knownPluginList.getNumTypes(); i++)
            {
                PluginDescription* plugin = knownPluginList.getType(i);
                if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
                    removeIndex.push_back(i);
            }
            
            // Remove from end to avoid index shifting issues
            std::sort(removeIndex.begin(), removeIndex.end(), std::greater<int>());
            for (int i : removeIndex)
                knownPluginList.removeType(i);
        }
    });
}

// Plugin blacklist management and safe scanning functions
void IconMenu::loadPluginBlacklist()
{
    std::lock_guard<std::mutex> lock(blacklistMutex);
    pluginBlacklist.clear();
    // Load blacklisted plugins from user settings
    String blacklistStr = getAppProperties().getUserSettings()->getValue("pluginBlacklist", "");
    if (blacklistStr.isNotEmpty())
    {
        StringArray tokens;
        tokens.addTokens(blacklistStr, "|", "");
        pluginBlacklist = tokens;
    }
}

void IconMenu::savePluginBlacklist()
{
    std::lock_guard<std::mutex> lock(blacklistMutex);
    String blacklistStr = pluginBlacklist.joinIntoString("|");
    getAppProperties().getUserSettings()->setValue("pluginBlacklist", blacklistStr);
    getAppProperties().getUserSettings()->saveIfNeeded();
}

void IconMenu::blacklistPlugin(const PluginDescription& plugin)
{
    // Create a unique ID for the plugin that combines all relevant info
    String pluginId = plugin.pluginFormatName + ":" + plugin.fileOrIdentifier;
    
    {
        std::lock_guard<std::mutex> lock(blacklistMutex);
        if (!isPluginBlacklisted(pluginId))
        {
            pluginBlacklist.add(pluginId);
        }
        else
        {
            return; // Already blacklisted, no need to continue
        }
    }
    savePluginBlacklist();
    
    // Also remove it from the known plugins list if it's there
    Thread::launch([this, pluginId]() {
        std::lock_guard<std::mutex> lock(pluginLoadMutex);
        for (int i = 0; i < knownPluginList.getNumTypes(); ++i)
        {
            PluginDescription* desc = knownPluginList.getType(i);
            String currentId = desc->pluginFormatName + ":" + desc->fileOrIdentifier;
            if (currentId == pluginId)
            {
                knownPluginList.removeType(i);
                break;
            }
        }
    });
}

bool IconMenu::isPluginBlacklisted(const String& pluginId) const
{
    std::lock_guard<std::mutex> lock(blacklistMutex);
    return pluginBlacklist.contains(pluginId);
}

void IconMenu::safePluginScan(AudioPluginFormat* format, const String& formatName)
{
    if (format == nullptr)
        return;

    // Create the splash screen as a progress listener
    SplashScreen* splashScreen = nullptr;
    
    // Create splash screen on the message thread
    MessageManager::callAsync([&]() {
        auto* splashWindow = new DialogWindow("Loading Nova Host", 
                                           Colours::transparentBlack, 
                                           true, 
                                           false);
        splashScreen = new SplashScreen();
        splashWindow->setContentOwned(splashScreen, false);
        splashWindow->setUsingNativeTitleBar(false);
        splashWindow->setOpaque(false);
        splashWindow->setDropShadowEnabled(true);
        splashWindow->setVisible(true);
        splashWindow->toFront(true);
    });
    
    // Give the message thread time to create the window
    Thread::sleep(200);

    // Create and run the safe plugin scanner
    SafePluginScanner scanner(formatManager, knownPluginList, formatName);
    
    // Set the splash screen as a listener if it was successfully created
    if (splashScreen != nullptr) {
        scanner.setProgressListener(splashScreen);
    }
    
    if (scanner.runThread())
    {
        // Scan completed successfully
        int numFound = scanner.getNumPluginsFound();
        
        String message;
        if (numFound > 0)
        {
            message = String(numFound) + " " + formatName + " plugins were found.";
        }
        else if (scanner.wasScanCancelled())
        {
            message = "Plugin scan was cancelled.";
        }
        else if (scanner.didScanTimeout())
        {
            message = "Plugin scan timed out. Some plugins may not have been detected.";
        }
        else
        {
            message = "No new " + formatName + " plugins were found.";
        }
        
        AlertWindow::showMessageBox(
            AlertWindow::InfoIcon,
            "Plugin Scan Complete",
            message);
    }
}

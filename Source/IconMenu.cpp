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
        PluginListComponent* listComponent = new PluginListComponent(pluginFormatManager,
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
    
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Default passthrough connection when no plugins are active
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
        AudioPluginInstance* instance = formatManager.createPluginInstance(
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
        
        graph.addNode(instance, job.nodeId);
        
        // Skip connections if plugin is bypassed
        if (job.bypass)
            continue;
            
        // Input to plugin
        if (!hasInputConnected)
        {
            graph.addConnection(INPUT, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            hasInputConnected = true;
            lastId = job.nodeId;
        }
        // Connect previous plugin to current
        else
        {
            graph.addConnection(lastId, CHANNEL_ONE, job.nodeId, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, job.nodeId, CHANNEL_TWO);
            lastId = job.nodeId;
        }
    }
    
    // Connect the last active plugin to output
    if (lastId > 0)
    {
        graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
    // If all plugins are bypassed, connect input directly to output
    else if (hasInputConnected == false && activePluginList.getNumTypes() > 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
                    
                    // Handle the case where the plugin scan timed out
                    if (scanTimedOut || !lastPluginSuccessful)
                    {
                        // Ask user if they want to blacklist the problematic plugin
                        if (AlertWindow::showOkCancelBox(
                                AlertWindow::WarningIcon,
                                "Problem with plugin: " + currentPlugin,
                                scanTimedOut 
                                    ? "Plugin scan timed out, this plugin may be causing the application to hang. "
                                      "Would you like to blacklist this plugin?" 
                                    : "There was a problem loading this plugin. Would you like to blacklist it?",
                                "Blacklist",
                                "Skip"))
                        {
                            // Blacklist the plugin
                            owner.blacklistPlugin(*desc);
                        }
                    }
                    
                    setProgress((float)j / (float)found.size());
                }
            }
        }
    }
    
    void timerCallback() override
    {
        // This gets called if the plugin scan times out
        scanTimedOut = true;
        lastPluginSuccessful = false;
        stopTimer();
        
        // Interrupt the thread to handle the timeout
        signalThreadShouldExit();
    }
    
    int getNumPluginsFound() const { return numFound; }
    
private:
    IconMenu& owner;
    int numFound;
    String currentPlugin;
    bool scanTimedOut;
    bool lastPluginSuccessful;
};

void IconMenu::safePluginScan(AudioPluginFormat* format, const String& formatName)
{
    if (format == nullptr)
        return;
        
    // Create scan dialog
    scanDialog = new PluginScanDialog(formatName, *this);
    
    // Start scanning
    if (scanDialog->runThread())
    {
        // Scan completed successfully
        int numFound = scanDialog->getNumPluginsFound();
        if (numFound > 0)
        {
            AlertWindow::showMessageBox(
                AlertWindow::InfoIcon,
                "Plugin Scan Complete",
                String(numFound) + " " + formatName + " plugins were found.");
        }
        else
        {
            AlertWindow::showMessageBox(
                AlertWindow::InfoIcon,
                "Plugin Scan Complete",
                "No new " + formatName + " plugins were found.");
        }
    }
    
    scanDialog = nullptr;

//
//  IconMenu.cpp
//  Nova Host
//
//  Created by Rolando Islas on 12/26/15.
//  Modified for NovaHost fork 2025
//

// Update the include path to match project structure
#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include <ctime>
#include <limits.h>
#include <memory> // Add std::unique_ptr support
#if JUCE_WINDOWS
#include "Windows.h"
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

	~PluginListWindow()
	{
		getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
	}

	void closeButtonPressed()
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
    
    #if JUCE_PLUGINHOST_LV2
    formatManager.addFormat(new LV2PluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AAX
    formatManager.addFormat(new AAXPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_ARA
    formatManager.addFormat(new ARAPluginFormat());
    #endif
    
    #if JUCE_PLUGINHOST_AU_AIRMUSIC
    formatManager.addFormat(new AudioUnitv3PluginFormat());
    #endif

    // Add Universal Audio Apollo plugin support
    #if JUCE_PLUGINHOST_UAD
    formatManager.addFormat(new UADPluginFormat());
    #endif

    // Load blacklisted plugins if available
    loadPluginBlacklist();

	#if JUCE_WINDOWS
	x = y = 0;
	#endif
    // Audio device
    std::unique_ptr<XmlElement> savedAudioState(getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState.get(), true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    // Plugins - all
    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    // Plugins - active
    std::unique_ptr<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    loadActivePlugins();
    activePluginList.addChangeListener(this);
	setIcon();
	setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
};

IconMenu::~IconMenu()
{
	savePluginStates();
	
	// Proper resource cleanup
	deviceManager.removeAudioCallback(&player);
	player.setProcessor(nullptr);
	knownPluginList.removeChangeListener(this);
	activePluginList.removeChangeListener(this);
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
    
    // Get number of active plugins for pre-allocation
    const int numPlugins = activePluginList.getNumTypes();
    
    // Pre-allocate connection arrays for better performance
    // This avoids multiple reallocations during the connection process
    Array<AudioProcessorGraph::Connection> connectionsToMake;
    connectionsToMake.ensureStorageAllocated(numPlugins * 2 + 2);
    
    // Create input and output nodes
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    
    // Direct input to output if no plugins
    if (numPlugins == 0)
    {
        connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE));
        connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO));
        graph.addConnections(connectionsToMake);
        return;
    }
    
    // Begin a transaction for better performance
    graph.suspendProcessing(true);
    
	int pluginTime = 0;
	int lastId = 0;
	bool hasInputConnected = false;
	std::vector<uint32> activeNodeIds;
	activeNodeIds.reserve(numPlugins);
	
	// NOTE: Node ids cannot begin at 0.
    for (int i = 1; i <= numPlugins; i++)
    {
        PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        String errorMessage;
        
        // Early check for blacklisted plugins
        String pluginId = plugin.pluginFormatName + ":" + plugin.fileOrIdentifier;
        if (isPluginBlacklisted(pluginId))
            continue;
            
        // Check minimum channel requirements
        if (plugin.numInputChannels < 2 || plugin.numOutputChannels < 2)
            continue;
            
        // Create the instance with appropriate buffer size and sample rate
        AudioPluginInstance* instance = formatManager.createPluginInstance(
            plugin, 
            graph.getSampleRate() > 0 ? graph.getSampleRate() : 44100.0,
            graph.getBlockSize() > 0 ? graph.getBlockSize() : 512, 
            errorMessage);
        
        if (instance == nullptr)
        {
            // Log error and continue with next plugin
            std::cerr << "Failed to load plugin " << plugin.name << ": " << errorMessage << std::endl;
            continue;
        }
        
		String pluginUid = getKey("state", plugin);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        
        if (savedPluginState.isNotEmpty() && savedPluginBinary.fromBase64Encoding(savedPluginState))
        {
            try
            {
                instance->setStateInformation(savedPluginBinary.getData(), savedPluginBinary.getSize());
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error loading state for plugin " << plugin.name << ": " << e.what() << std::endl;
            }
        }
        
        // Add the node to the graph
        graph.addNode(instance, i);
        activeNodeIds.push_back(i);
        
		String key = getKey("bypass", plugin);
		bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        
        // Skip bypassed plugins
        if (bypass)
            continue;
            
        // Input to plugin (first active plugin)
        if (!hasInputConnected)
        {
            connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_ONE, i, CHANNEL_ONE));
            connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_TWO, i, CHANNEL_TWO));
			hasInputConnected = true;
        }
        // Connect previous plugin to current
        else if (lastId > 0)
        {
            connectionsToMake.add(AudioProcessorGraph::Connection(lastId, CHANNEL_ONE, i, CHANNEL_ONE));
            connectionsToMake.add(AudioProcessorGraph::Connection(lastId, CHANNEL_TWO, i, CHANNEL_TWO));
        }
        
		lastId = i;
    }
    
	if (lastId > 0)
	{
		// Last active plugin to output
		connectionsToMake.add(AudioProcessorGraph::Connection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE));
		connectionsToMake.add(AudioProcessorGraph::Connection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO));
	}
	else if (hasInputConnected == false && numPlugins > 0)
	{
	    // If we have plugins but none are active (all bypassed), connect input directly to output
	    connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE));
	    connectionsToMake.add(AudioProcessorGraph::Connection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO));
	}
	
	// Batch process all connections at once for better performance
	graph.addConnections(connectionsToMake);
	
	// Resume processing
	graph.suspendProcessing(false);
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
			
		int pluginTime = atoi(pluginTimeString.toStdString().c_str());
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
        std::unique_ptr<XmlElement> savedPluginList(knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList);
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList(activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginListActive", savedPluginList);
            getAppProperties().saveIfNeeded();
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
		menu.addSectionHeader("Avaliable Plugins");
        // All plugins
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
		menu.addSeparator();
		menu.addItem(2, "Delete Plugin States");
		#if !JUCE_MAC
			menu.addItem(3, "Invert Icon Color");
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
	juce::Rectangle<int> rect(x, y, 1, 1);
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
        // Delete plugin
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            im->deletePluginStates();

			int index = id - im->INDEX_DELETE;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = getKey("order", timeSorted[index]);
			int unsortedIndex = -1;
			for (int i = 0; i < im->activePluginList.getNumTypes(); i++)
			{
				PluginDescription current = *im->activePluginList.getType(i);
				if (key.equalsIgnoreCase(getKey("order", current)))
				{
					unsortedIndex = i;
					break;
				}
			}
            
            // Fix the infinite loop bug by checking if we found a matching index
            if (unsortedIndex >= 0 && unsortedIndex < im->activePluginList.getNumTypes())
            {
                // Remove plugin order
                getAppProperties().getUserSettings()->removeValue(key);
                // Remove bypass entry
                getAppProperties().getUserSettings()->removeValue(getKey("bypass", timeSorted[index]));
                getAppProperties().saveIfNeeded();
                
                // Remove plugin from list
                im->activePluginList.removeType(unsortedIndex);

                // Save current states
                im->savePluginStates();
                im->loadActivePlugins();
            }
        }
        // Add plugin
        else if (im->knownPluginList.getIndexChosenByMenu(id) > -1)
        {
			PluginDescription plugin = *im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id));
			String key = getKey("order", plugin);
			int t = time(0);
			getAppProperties().getUserSettings()->setValue(key, t);
			getAppProperties().saveIfNeeded();
            im->activePluginList.addType(plugin);

			im->savePluginStates();
			im->loadActivePlugins();
        }
		// Bypass plugin
		else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
		{
			int index = id - im->INDEX_BYPASS;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = getKey("bypass", timeSorted[index]);

			// Set bypass flag
			bool bypassed = getAppProperties().getUserSettings()->getBoolValue(key);
			getAppProperties().getUserSettings()->setValue(key, !bypassed);
			getAppProperties().saveIfNeeded();

			im->savePluginStates();
			im->loadActivePlugins();
		}
        // Show active plugin GUI
		else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(id - im->INDEX_EDIT + 1))
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
				String keyToMove = getKey("order", pluginToMove);
				String keyAbove = getKey("order", pluginAbove);
				
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
				String keyToMove = getKey("order", pluginToMove);
				String keyBelow = getKey("order", pluginBelow);
				
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

String IconMenu::getKey(String type, PluginDescription plugin)
{
	String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
	return key;
}

void IconMenu::deletePluginStates()
{
	std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
	std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(i + 1);
		if (node == nullptr)
			break;
        AudioProcessor& processor = *node->getProcessor();
		String pluginUid = getKey("state", list[i]);
        MemoryBlock savedStateBinary;
        processor.getStateInformation(savedStateBinary);
        getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager, 0, 256, 0, 256, false, false, true, true);
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
        
    std::unique_ptr<XmlElement> audioState(deviceManager.createStateXml());
        
    getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState);
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
	std::vector<int> removeIndex;
	for (int i = 0; i < knownPluginList.getNumTypes(); i++)
	{
		PluginDescription* plugin = knownPluginList.getType(i);
		if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
			removeIndex.push_back(i);
	}
	for (int i = 0; i < removeIndex.size(); i++)
		knownPluginList.removeType(removeIndex[i] - i);
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
}

bool IconMenu::isPluginBlacklisted(const String& pluginId) const
{
    std::lock_guard<std::mutex> lock(blacklistMutex);
    return pluginBlacklist.contains(pluginId);
}

// Custom dialog to show progress during plugin scanning with option to blacklist problematic plugins
class IconMenu::PluginScanDialog : public ThreadWithProgressWindow
{
public:
    PluginScanDialog(const String& formatName, IconMenu& owner)
        : ThreadWithProgressWindow("Scanning for " + formatName + " plugins...", 
                                  true, // can be canceled
                                  true), // catch exceptions
          owner(owner),
          numFound(0),
          currentPlugin(""),
          scanTimedOut(false),
          lastPluginSuccessful(true)
    {
    }
    
    void run() override
    {
        numFound = 0;
        lastPluginSuccessful = true;
        
        // Use appropriate default paths for the OS
        StringArray defaultPaths;
        
        #if JUCE_WINDOWS
        defaultPaths.add("C:\\Program Files\\Common Files\\VST3");
        defaultPaths.add("C:\\Program Files\\Common Files\\VST2");
        defaultPaths.add("C:\\Program Files\\VSTPlugins");
        defaultPaths.add("C:\\Program Files\\Steinberg\\VSTPlugins");
        #elif JUCE_MAC
        defaultPaths.add("~/Library/Audio/Plug-Ins/Components");
        defaultPaths.add("~/Library/Audio/Plug-Ins/VST");
        defaultPaths.add("~/Library/Audio/Plug-Ins/VST3");
        defaultPaths.add("/Library/Audio/Plug-Ins/Components");
        defaultPaths.add("/Library/Audio/Plug-Ins/VST");
        defaultPaths.add("/Library/Audio/Plug-Ins/VST3");
        #elif JUCE_LINUX
        // Expand the home directory path properly on Linux
        const String homeDir = File::getSpecialLocation(File::userHomeDirectory).getFullPathName();
        defaultPaths.add(homeDir + "/.vst");
        defaultPaths.add(homeDir + "/.vst3");
        defaultPaths.add(homeDir + "/.lxvst");
        defaultPaths.add("/usr/lib/vst");
        defaultPaths.add("/usr/lib/vst3");
        defaultPaths.add("/usr/lib/lxvst");
        defaultPaths.add("/usr/local/lib/vst");
        defaultPaths.add("/usr/local/lib/vst3");
        defaultPaths.add("/usr/local/lib/lxvst");
        defaultPaths.add("/usr/lib/x86_64-linux-gnu/vst");
        defaultPaths.add("/usr/lib/x86_64-linux-gnu/vst3");
        #endif
        
        // Get user-defined paths from settings
        String savedPaths = getAppProperties().getUserSettings()->getValue("pluginSearchPaths", "");
        if (savedPaths.isNotEmpty())
        {
            StringArray customPaths;
            customPaths.addTokens(savedPaths, "|", "");
            
            for (int i = 0; i < customPaths.size(); ++i)
                if (!defaultPaths.contains(customPaths[i]))
                    defaultPaths.add(customPaths[i]);
        }
        
        for (int pathIndex = 0; pathIndex < defaultPaths.size(); ++pathIndex)
        {
            const File path(defaultPaths[pathIndex]);
            if (!path.exists() || threadShouldExit())
                continue;
                
            setStatusMessage("Scanning: " + path.getFullPathName());
            
            FileSearchPath searchPath(path);
            for (int i = 0; i < owner.formatManager.getNumFormats(); ++i)
            {
                AudioPluginFormat* format = owner.formatManager.getFormat(i);
                if (threadShouldExit())
                    return;
                    
                // Loop through each plugin type supported by this format
                FileSearchPath formatPath(searchPath);
                format->findAllTypesForFile(formatPath);
                    
                OwnedArray<PluginDescription> found;
                format->searchPathsForPlugins(formatPath, true, found);
                
                for (int j = 0; j < found.size(); ++j)
                {
                    if (threadShouldExit())
                        return;
                        
                    PluginDescription* desc = found[j];
                    
                    // Check if this plugin is already blacklisted
                    String pluginId = desc->pluginFormatName + ":" + desc->fileOrIdentifier;
                    if (owner.isPluginBlacklisted(pluginId))
                        continue;
                        
                    currentPlugin = desc->name;
                    setStatusMessage("Testing plugin: " + currentPlugin + " (" + desc->pluginFormatName + ")");
                    
                    // Use a timeout to detect plugins that hang during scanning
                    scanTimedOut = false;
                    lastPluginSuccessful = true;
                    
                    // Start the timeout timer
                    startTimer(5000); // 5 second timeout
                    
                    String errorMessage;
                    
                    try
                    {
                        // This can hang if a plugin is problematic
                        AudioPluginInstance* instance = owner.formatManager.createPluginInstance(
                            *desc, 
                            48000.0, // standard sample rate
                            1024,    // standard buffer size
                            errorMessage);
                            
                        if (instance != nullptr)
                        {
                            // Successfully loaded the plugin
                            delete instance;
                            owner.knownPluginList.addType(*desc);
                            numFound++;
                        }
                        else if (errorMessage.isNotEmpty())
                        {
                            // Failed to load, but didn't crash
                            DBG("Failed to load plugin: " + desc->name + " - " + errorMessage);
                            lastPluginSuccessful = false;
                        }
                    }
                    catch (...)
                    {
                        // Plugin crashed during instantiation
                        DBG("Plugin crashed during scan: " + desc->name);
                        lastPluginSuccessful = false;
                    }
                    
                    // Stop the timeout timer
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
        
    // Create scan dialog with std::unique_ptr for automatic resource management
    std::unique_ptr<PluginScanDialog> scanDialogPtr = std::make_unique<PluginScanDialog>(formatName, *this);
    
    // Start scanning
    if (scanDialogPtr->runThread())
    {
        // Scan completed successfully
        int numFound = scanDialogPtr->getNumPluginsFound();
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
    
    // No need to manually delete - std::unique_ptr will automatically handle cleanup
}

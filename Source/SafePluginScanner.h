/*
 * SafePluginScanner.h
 * NovaHost - Audio Plugin Host
 *
 * This file provides a safer way to scan for plugins, preventing crashes from problematic plugins
 * Created April 18, 2025
 */

#ifndef SAFEPLUGINSCANNER_H_INCLUDED
#define SAFEPLUGINSCANNER_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"

#include <atomic>
#include <future>
#include <memory>
#include <chrono>
#include <mutex>

/**
 * Interface for objects that want to receive updates about plugin scanning progress
 */
class PluginScanProgressListener
{
public:
    virtual ~PluginScanProgressListener() = default;
    
    /** Called when the plugin scan progress updates */
    virtual void onScanProgressUpdate(float progressPercent, const juce::String& statusMessage) = 0;
};

class SafePluginScanner : public juce::ThreadWithProgressWindow
{
public:
    SafePluginScanner(juce::AudioPluginFormatManager& formatManager, 
                      juce::KnownPluginList& pluginList,
                      const juce::String& formatName,
                      int timeoutMilliseconds = 180000)
        : juce::ThreadWithProgressWindow("Scanning for " + formatName + " plugins...", true, true),
          formatManager(formatManager),
          pluginList(pluginList),
          formatName(formatName),
          scanTimedOut(false),
          numFound(0),
          scanCancelled(false),
          progressListener(nullptr),
          searchPath(getPluginSearchPaths())
    {
        setTimeoutMs(timeoutMilliseconds); // Configurable timeout for plugin scanning
    }
    
    void setProgressListener(PluginScanProgressListener* listener)
    {
        progressListener = listener;
    }
    
    void run() override
    {
        scanTimedOut = false;
        scanCancelled.store(false);
        numFound = 0;
        
        // Find the requested format
        juce::AudioPluginFormat* format = nullptr;
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            if (formatManager.getFormat(i)->getName() == formatName)
            {
                format = formatManager.getFormat(i);
                break;
            }
        }
        
        if (format == nullptr)
        {
            juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon,
                                   "Plugin Scan Error", 
                                   formatName + " format not available.");
            return;
        }

        // Search for plugins
        juce::OwnedArray<juce::PluginDescription> results;
        
        try
        {
            int totalPaths = searchPath.getNumPaths();
            
            // Set up search parameters
            for (int i = 0; i < totalPaths; ++i)
            {
                if (threadShouldExit())
                {
                    scanCancelled.store(true);
                    return;
                }
                
                const juce::File& path = searchPath[i];
                
                juce::String statusMsg = "Scanning: " + path.getFullPathName();
                float progress = (float)i / (float)totalPaths * 0.5f;
                
                setStatusMessage(statusMsg);
                setProgress(progress);
                
                // Update external progress listener if available
                updateProgressListener(progress, statusMsg);
                
                // Skip scanning of empty or non-existent directories
                if (!path.exists() || !path.isDirectory() || path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) 
                {
                    continue;
                }
                
                // Use a timeout for each directory search
                searchSinglePathForPlugins(format, path, results);
            }
        }
        catch (const std::exception& e)
        {
            // Use Desktop::getInstance() to ensure this runs on the message thread
            const juce::String errorMsg = juce::String("Error: ") + e.what();
            juce::MessageManager::callAsync([errorMsg]() {
                juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon,
                                          "Plugin Scan Error",
                                          errorMsg);
            });
            return;
        }
        catch (...)
        {
            juce::MessageManager::callAsync([]() {
                juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon,
                                          "Plugin Scan Error",
                                          "An unknown error occurred during the plugin scan.");
            });
            return;
        }
        
        // Process the results safely
        int validPluginCount = 0;
        juce::String statusMsg = "Testing discovered plugins";
        setStatusMessage(statusMsg);
        updateProgressListener(0.5f, statusMsg);
        
        for (int i = 0; i < results.size() && !threadShouldExit(); ++i)
        {
            juce::PluginDescription* desc = results[i];
            if (desc == nullptr)
                continue;
                
            statusMsg = "Testing plugin: " + desc->name;
            float progress = 0.5f + ((float)i / (float)results.size() * 0.5f);
            
            setStatusMessage(statusMsg);
            setProgress(progress);
            updateProgressListener(progress, statusMsg);
            
            if (isPluginSafe(*desc))
            {
                // Check if this is a new plugin
                bool isNew = true;
                for (int j = 0; j < pluginList.getNumTypes(); ++j)
                {
                    if (desc->isDuplicateOf(*pluginList.getType(j)))
                    {
                        isNew = false;
                        break;
                    }
                }
                
                if (isNew)
                {
                    pluginList.addType(*desc);
                    numFound++;
                }
                
                validPluginCount++;
            }
            else if (!threadShouldExit() && !scanCancelled.load())
            {
                handlePluginLoadFailure(*desc);
            }
        }
        
        if (validPluginCount == 0 && results.size() > 0 && !scanCancelled.load())
        {
            statusMsg = "No valid plugins found";
            setStatusMessage(statusMsg);
            updateProgressListener(1.0f, statusMsg);
        }
        else
        {
            statusMsg = juce::String(numFound) + " new plugins found";
            setStatusMessage(statusMsg);
            updateProgressListener(1.0f, statusMsg);
        }
    }
    
    int getNumPluginsFound() const { return numFound; }
    bool didScanTimeout() const { return scanTimedOut; }
    bool wasScanCancelled() const { return scanCancelled.load(); }
    
private:
    void updateProgressListener(float progress, const juce::String& statusMessage)
    {
        if (progressListener != nullptr)
            progressListener->onScanProgressUpdate(progress, statusMessage);
    }
    
    juce::FileSearchPath getPluginSearchPaths()
    {
        juce::FileSearchPath searchPath;
        
        // Add default search paths based on OS
        #if JUCE_WINDOWS
        searchPath.addPath(juce::File("C:\\Program Files\\Common Files\\VST3"));
        searchPath.addPath(juce::File("C:\\Program Files\\Common Files\\VST2"));
        searchPath.addPath(juce::File("C:\\Program Files\\VSTPlugins"));
        searchPath.addPath(juce::File("C:\\Program Files\\Steinberg\\VSTPlugins"));
        searchPath.addPath(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
        searchPath.addPath(juce::File("C:\\Program Files (x86)\\Common Files\\VST2"));
        searchPath.addPath(juce::File("C:\\Program Files (x86)\\VSTPlugins"));
        searchPath.addPath(juce::File("C:\\Program Files (x86)\\Steinberg\\VSTPlugins"));
        #elif JUCE_MAC
        searchPath.addPath(juce::File("~/Library/Audio/Plug-Ins/Components"));
        searchPath.addPath(juce::File("~/Library/Audio/Plug-Ins/VST"));
        searchPath.addPath(juce::File("~/Library/Audio/Plug-Ins/VST3"));
        searchPath.addPath(juce::File("/Library/Audio/Plug-Ins/Components"));
        searchPath.addPath(juce::File("/Library/Audio/Plug-Ins/VST"));
        searchPath.addPath(juce::File("/Library/Audio/Plug-Ins/VST3"));
        #elif JUCE_LINUX
        const juce::File homeDir(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        searchPath.addPath(homeDir.getChildFile(".vst"));
        searchPath.addPath(homeDir.getChildFile(".vst3"));
        searchPath.addPath(homeDir.getChildFile(".lxvst"));
        searchPath.addPath(juce::File("/usr/lib/vst"));
        searchPath.addPath(juce::File("/usr/lib/vst3"));
        searchPath.addPath(juce::File("/usr/lib/lxvst"));
        searchPath.addPath(juce::File("/usr/local/lib/vst"));
        searchPath.addPath(juce::File("/usr/local/lib/vst3"));
        searchPath.addPath(juce::File("/usr/local/lib/lxvst"));
        #endif
        
        // Add user paths from settings
        juce::StringArray userPaths;
        userPaths.addTokens(juce::JUCEApplication::getInstance()->getGlobalProperties().getUserSettings()->getValue("pluginSearchPaths", ""), "|", "");
        for (int i = 0; i < userPaths.size(); ++i)
        {
            if (userPaths[i].isNotEmpty())
                searchPath.addPath(juce::File(userPaths[i]));
        }
            
        return searchPath;
    }
    
    void searchSinglePathForPlugins(juce::AudioPluginFormat* format, const juce::File& path, juce::OwnedArray<juce::PluginDescription>& results)
    {
        if (format == nullptr || threadShouldExit())
            return;
            
        std::promise<void> searchPromise;
        std::future<void> searchFuture = searchPromise.get_future();
        std::atomic<bool> exceptionOccurred(false);
        juce::String exceptionMessage;
        
        juce::Thread::launch([&, path]()
        {
            try 
            {
                format->findAllTypesForFile(results, path);
                searchPromise.set_value();
            }
            catch (const std::exception& e)
            {
                exceptionOccurred.store(true);
                exceptionMessage = e.what();
                try {
                    searchPromise.set_exception(std::current_exception());
                }
                catch (...) {} // Set exception might throw if promise was already satisfied
            }
            catch (...)
            {
                exceptionOccurred.store(true);
                try {
                    searchPromise.set_exception(std::current_exception());
                }
                catch (...) {} // Set exception might throw if promise was already satisfied
            }
        });
        
        // Wait for search with timeout - use variable timeout based on directory size
        const int baseTimeoutMs = 5000; // 5 seconds base
        int fileCount = path.getNumberOfChildFiles(juce::File::findFiles);
        // Add 500ms per 10 files, capped at 30 seconds
        int additionalTime = juce::jmin(25000, (fileCount / 10) * 500);
        const int maxWaitTimeMs = baseTimeoutMs + additionalTime;
        
        if (searchFuture.wait_for(std::chrono::milliseconds(maxWaitTimeMs)) == std::future_status::timeout)
        {
            scanTimedOut = true;
            setStatusMessage("Warning: Scan timed out for " + path.getFullPathName());
            juce::Thread::sleep(1000); // Give user time to see message
            return;
        }
        
        // Check if search threw an exception
        if (exceptionOccurred.load())
        {
            setStatusMessage("Warning: Error scanning " + path.getFullPathName() + 
                            (exceptionMessage.isNotEmpty() ? (" - " + exceptionMessage) : ""));
            juce::Thread::sleep(1000);
            return;
        }
        
        try {
            searchFuture.get(); // Will re-throw any exception from the thread
        }
        catch (const std::exception& e)
        {
            setStatusMessage("Warning: Error scanning " + path.getFullPathName() + " - " + e.what());
            juce::Thread::sleep(1000);
        }
        catch (...) {
            setStatusMessage("Warning: Unknown error scanning " + path.getFullPathName());
            juce::Thread::sleep(1000);
        }
    }
    
    bool isPluginSafe(const juce::PluginDescription& desc)
    {
        // Skip blacklisted plugins
        if (isPluginBlacklisted(desc))
        {
            setStatusMessage("Skipping blacklisted plugin: " + desc.name);
            juce::Thread::sleep(300); // Shorter pause to speed up scanning
            return false;
        }
            
        // Try loading the plugin with a timeout
        std::atomic<bool> loadComplete(false);
        std::atomic<bool> loadSuccessful(false);
        juce::String errorMessage;
        
        // Use a shared_ptr for better memory management
        std::shared_ptr<juce::AudioPluginInstance> instance;
            
        juce::Thread::launch([&]()
        {
            try 
            {
                std::unique_ptr<juce::AudioPluginInstance> pluginInstance = formatManager.createPluginInstance(
                    desc, 44100.0, 512, errorMessage);
                    
                if (pluginInstance != nullptr)
                {
                    // Transfer ownership to our shared_ptr
                    instance = std::shared_ptr<juce::AudioPluginInstance>(pluginInstance.release());
                    
                    // Test basic functionality
                    instance->prepareToPlay(44100.0, 512);
                    instance->releaseResources();
                    loadSuccessful.store(true);
                }
                
                loadComplete.store(true);
            }
            catch (...)
            {
                // Ensure we mark loading as complete even if an exception occurs
                loadComplete.store(true);
            }
        });
            
        // Wait for plugin to load with timeout - adjust timeout based on plugin type
        // Some larger plugins need more time to initialize
        int maxWaitTimeMs = 10000; // Default 10 seconds
        
        // Give extra time to complex plugin types that typically take longer to load
        if (desc.pluginFormatName == "VST3" || desc.pluginFormatName == "AudioUnit")
            maxWaitTimeMs = 15000;
        
        int elapsed = 0;
        const int checkInterval = 100;
            
        while (!loadComplete.load() && !threadShouldExit() && !scanCancelled.load())
        {
            juce::Thread::sleep(checkInterval);
            elapsed += checkInterval;
                
            if (elapsed > maxWaitTimeMs)
            {
                setStatusMessage("Plugin load timeout: " + desc.name);
                juce::Thread::sleep(500);
                return false; // Plugin took too long to load
            }
        }
        
        // Explicitly clear the instance to ensure proper cleanup
        instance.reset();
            
        return loadSuccessful.load();
    }
    
    bool isPluginBlacklisted(const juce::PluginDescription& desc)
    {
        // Get the blacklist from settings
        juce::String blacklistStr = juce::JUCEApplication::getInstance()->getGlobalProperties().getUserSettings()->getValue("pluginBlacklist", "");
        if (blacklistStr.isEmpty())
            return false;
            
        juce::StringArray blacklist;
        blacklist.addTokens(blacklistStr, "|", "");
            
        // Create a unique ID for this plugin
        juce::String pluginId = desc.pluginFormatName + ":" + desc.fileOrIdentifier;
            
        return blacklist.contains(pluginId);
    }
    
    void handlePluginLoadFailure(const juce::PluginDescription& desc)
    {
        // Only show the dialog if we're not in the process of exiting
        if (threadShouldExit() || scanCancelled.load())
            return;
            
        // Create a variable to store the result since we can't capture by reference in the lambda
        std::shared_ptr<std::atomic<bool>> shouldBlacklist = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> dialogCompleted = std::make_shared<std::atomic<bool>>(false);
        
        // Ask user if they want to blacklist the plugin on the message thread
        juce::MessageManager::callAsync([this, desc, shouldBlacklist, dialogCompleted]() {
            bool result = juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::WarningIcon,
                "Plugin Failed to Load",
                "The plugin '" + desc.name + "' failed to load properly. Would you like to blacklist this plugin to prevent it from being scanned in the future?",
                "Blacklist",
                "Skip"
            );
            
            shouldBlacklist->store(result);
            dialogCompleted->store(true);
        });
        
        // Wait for the dialog to complete
        while (!dialogCompleted->load() && !threadShouldExit() && !scanCancelled.load()) {
            juce::Thread::sleep(100);
        }
            
        if (shouldBlacklist->load() && !threadShouldExit() && !scanCancelled.load())
        {
            juce::String pluginId = desc.pluginFormatName + ":" + desc.fileOrIdentifier;
                
            // Get existing blacklist
            juce::String blacklistStr = juce::JUCEApplication::getInstance()->getGlobalProperties().getUserSettings()->getValue("pluginBlacklist", "");
            juce::StringArray blacklist;
                
            if (blacklistStr.isNotEmpty())
                blacklist.addTokens(blacklistStr, "|", "");
                
            // Add plugin to blacklist if not already there
            if (!blacklist.contains(pluginId))
            {
                blacklist.add(pluginId);
                juce::JUCEApplication::getInstance()->getGlobalProperties()->getUserSettings()->setValue("pluginBlacklist", blacklist.joinIntoString("|"));
                juce::JUCEApplication::getInstance()->getGlobalProperties()->getUserSettings()->saveIfNeeded();
            }
        }
    }

    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& pluginList;
    juce::String formatName;
    bool scanTimedOut;
    int numFound;
    std::atomic<bool> scanCancelled;
    PluginScanProgressListener* progressListener;
    std::mutex blacklistMutex;
    juce::FileSearchPath searchPath;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SafePluginScanner)
};

#endif  // SAFEPLUGINSCANNER_H_INCLUDED

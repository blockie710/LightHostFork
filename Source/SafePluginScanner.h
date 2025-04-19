/*
 * SafePluginScanner.h
 * NovaHost - Audio Plugin Host
 *
 * This file provides a safer way to scan for plugins, preventing crashes from problematic plugins
 * Created April 18, 2025
 * Updated April 19, 2025 - Added parallel scanning with ThreadPool
 */

#ifndef SAFEPLUGINSCANNER_H_INCLUDED
#define SAFEPLUGINSCANNER_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"
#include "ThreadPool.h"

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
          searchPath(getPluginSearchPaths()),
          lastProgressUpdateTime(std::chrono::steady_clock::now())
    {
        setTimeoutMs(timeoutMilliseconds); // Configurable timeout for plugin scanning
    }
    
    // Use shared_ptr instead of raw pointer for better safety
    void setProgressListener(std::shared_ptr<PluginScanProgressListener> listener)
    {
        std::lock_guard<std::mutex> lock(progressListenerMutex);
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
            
            // Create a thread pool for parallel scanning
            // Use a reasonable number of threads - one per CPU core but no more than 8
            int numThreads = juce::jmin(8, juce::SystemStats::getNumCPUs());
            ThreadPool scanPool(numThreads);
            
            // Progress tracking
            std::atomic<int> completedPaths(0);
            std::mutex resultsMutex; // Protects access to the results array
            
            // Initial status update
            juce::String statusMsg = "Scanning " + juce::String(totalPaths) + " paths with " +
                                    juce::String(numThreads) + " parallel threads";
            setStatusMessage(statusMsg);
            updateProgressListener(0.0f, statusMsg);
            
            // Create a vector to hold futures for all scan tasks
            std::vector<std::future<void>> scanTasks;
            scanTasks.reserve(totalPaths);
            
            // Submit search tasks to the thread pool
            for (int i = 0; i < totalPaths; ++i)
            {
                if (threadShouldExit())
                {
                    scanCancelled.store(true);
                    break;
                }
                
                const juce::File& path = searchPath[i];
                
                // Skip scanning of empty or non-existent directories
                if (!path.exists() || !path.isDirectory() || path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) 
                {
                    // Count this as completed
                    completedPaths++;
                    continue;
                }
                
                // Submit task to thread pool
                auto future = scanPool.addJob(
                    [this, format, path, &results, &resultsMutex, &completedPaths, totalPaths]() {
                        // Create a local array to collect results for this path
                        juce::OwnedArray<juce::PluginDescription> pathResults;
                        
                        // Search this path for plugins
                        searchSinglePathForPlugins(format, path, pathResults);
                        
                        // Update progress
                        int completed = ++completedPaths;
                        float progress = (float)completed / (float)totalPaths * 0.5f;
                        
                        // Update status safely on the message thread
                        juce::String statusUpdate = "Scanned " + juce::String(completed) + 
                                                " of " + juce::String(totalPaths) + 
                                                " paths: " + path.getFileName();
                        
                        juce::MessageManager::callAsync([this, progress, statusUpdate]() {
                            setStatusMessage(statusUpdate);
                            setProgress(progress);
                            updateProgressListener(progress, statusUpdate);
                        });
                        
                        // Transfer local results to main result array
                        if (pathResults.size() > 0)
                        {
                            std::lock_guard<std::mutex> lock(resultsMutex);
                            for (int j = 0; j < pathResults.size(); ++j)
                            {
                                // Transfer ownership of each description to the main results array
                                results.add(pathResults.getUnchecked(j));
                            }
                            
                            // Clear without deleting the transferred objects
                            pathResults.clearQuick(false);
                        }
                    }
                );
                
                scanTasks.push_back(std::move(future));
            }
            
            // Wait for all scan tasks to complete or for cancellation
            while (completedPaths < totalPaths && !threadShouldExit() && !scanCancelled.load())
            {
                // Update progress periodically
                juce::String statusUpdate = "Scanned " + juce::String(completedPaths.load()) + 
                                         " of " + juce::String(totalPaths) + " paths";
                float progress = (float)completedPaths.load() / (float)totalPaths * 0.5f;
                
                setStatusMessage(statusUpdate);
                setProgress(progress);
                updateProgressListener(progress, statusUpdate);
                
                // Small sleep to prevent UI thread hogging
                juce::Thread::sleep(100);
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
        
        // Create a thread pool specifically for testing plugins
        // Use fewer threads for testing to avoid overwhelming the system
        ThreadPool testPool(juce::jmin(4, juce::SystemStats::getNumCPUs()));
        
        std::atomic<int> completedTests(0);
        std::atomic<int> validTests(0);
        std::mutex pluginListMutex; // Protects access to the plugin list
        
        // Vector to hold futures for plugin test tasks
        std::vector<std::future<void>> testTasks;
        testTasks.reserve(results.size());
        
        // Submit plugin testing tasks
        for (int i = 0; i < results.size(); ++i)
        {
            if (threadShouldExit() || scanCancelled.load())
                break;
            
            auto desc = results.getUnchecked(i);
            
            // Submit the plugin test to the thread pool
            auto future = testPool.addJob(
                [this, desc, &completedTests, &validTests, &pluginListMutex, totalPlugins = results.size()]() {
                    bool isValid = isPluginSafe(*desc);
                    
                    // Update progress
                    int completed = ++completedTests;
                    float progress = 0.5f + ((float)completed / (float)totalPlugins * 0.5f);
                    
                    // Update status safely on the message thread
                    juce::String statusUpdate = "Tested " + juce::String(completed) + 
                                             " of " + juce::String(totalPlugins) + 
                                             " plugins: " + desc->name;
                    
                    juce::MessageManager::callAsync([this, progress, statusUpdate]() {
                        setStatusMessage(statusUpdate);
                        setProgress(progress);
                        updateProgressListener(progress, statusUpdate);
                    });
                    
                    if (isValid)
                    {
                        // Add valid plugin to the list safely
                        std::lock_guard<std::mutex> lock(pluginListMutex);
                        pluginList.addType(*desc);
                        validTests++;
                        numFound++;
                    }
                    else
                    {
                        // Handle plugin load failure
                        handlePluginLoadFailure(*desc);
                    }
                }
            );
            
            testTasks.push_back(std::move(future));
        }
        
        // Wait for all plugin tests to complete or for cancellation
        int totalPlugins = results.size();
        while (completedTests < totalPlugins && !threadShouldExit() && !scanCancelled.load())
        {
            // Update progress periodically
            juce::String statusUpdate = "Tested " + juce::String(completedTests.load()) + 
                                     " of " + juce::String(totalPlugins) + 
                                     " plugins (" + juce::String(validTests.load()) + " valid)";
            float progress = 0.5f + ((float)completedTests.load() / (float)totalPlugins * 0.5f);
            
            setStatusMessage(statusUpdate);
            setProgress(progress);
            updateProgressListener(progress, statusUpdate);
            
            // Small sleep to prevent UI thread hogging
            juce::Thread::sleep(100);
        }
        
        // Final status update
        if (!threadShouldExit() && !scanCancelled.load())
        {
            juce::String finalStatus = "Scan complete: Found " + juce::String(numFound) + " " + formatName + " plugins";
            setStatusMessage(finalStatus);
            setProgress(1.0f);
            updateProgressListener(1.0f, finalStatus);
        }
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
        // Thread-safe blacklist checking
        std::lock_guard<std::mutex> lock(blacklistMutex);
        
        // Get the blacklist from settings
        juce::String blacklistStr = juce::JUCEApplication::getInstance()->getGlobalProperties()->getUserSettings()->getValue("pluginBlacklist", "");
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
        
        // Wait for the dialog to complete with a timeout to prevent hanging
        int timeoutMs = 30000; // 30 seconds max wait for user response
        int elapsedMs = 0;
        int sleepIntervalMs = 100;
        
        while (!dialogCompleted->load() && !threadShouldExit() && !scanCancelled.load() && elapsedMs < timeoutMs) {
            juce::Thread::sleep(sleepIntervalMs);
            elapsedMs += sleepIntervalMs;
        }
            
        if (shouldBlacklist->load() && !threadShouldExit() && !scanCancelled.load())
        {
            // Thread-safe blacklist update
            std::lock_guard<std::mutex> lock(blacklistMutex);
            
            juce::String pluginId = desc.pluginFormatName + ":" + desc.fileOrIdentifier;
                
            // Get existing blacklist
            auto* settings = juce::JUCEApplication::getInstance()->getGlobalProperties()->getUserSettings();
            juce::String blacklistStr = settings->getValue("pluginBlacklist", "");
            juce::StringArray blacklist;
                
            if (blacklistStr.isNotEmpty())
                blacklist.addTokens(blacklistStr, "|", "");
                
            // Add plugin to blacklist if not already there
            if (!blacklist.contains(pluginId))
            {
                blacklist.add(pluginId);
                settings->setValue("pluginBlacklist", blacklist.joinIntoString("|"));
                settings->saveIfNeeded();
            }
        }
    }

private:
    void updateProgressListener(float progress, const juce::String& message)
    {
        // Use rate limiting to avoid too many UI updates
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastProgressUpdateTime).count();
            
        // Limit updates to once every 250ms unless it's the final update (progress == 1.0f)
        if (progress == 1.0f || elapsedMs > 250)
        {
            lastProgressUpdateTime = now;
            
            std::lock_guard<std::mutex> lock(progressListenerMutex);
            if (progressListener != nullptr)
            {
                progressListener->onScanProgressUpdate(progress, message);
            }
        }
    }

    juce::FileSearchPath getPluginSearchPaths()
    {
        juce::FileSearchPath searchPath;
        
        // Add standard paths based on platform
        #if JUCE_WINDOWS
        searchPath.addPath(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VST3"));
        searchPath.addPath(juce::File("C:\\Program Files\\Common Files\\VST3"));
        searchPath.addPath(juce::File("C:\\Program Files\\Common Files\\VST2"));
        searchPath.addPath(juce::File("C:\\Program Files\\VSTPlugins"));
        searchPath.addPath(juce::File("C:\\Program Files\\Steinberg\\VSTPlugins"));
        #elif JUCE_MAC
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
        userPaths.addTokens(juce::JUCEApplication::getInstance()->getGlobalProperties()->getUserSettings()->getValue("pluginSearchPaths", ""), "|", "");
        for (int i = 0; i < userPaths.size(); ++i)
        {
            if (userPaths[i].isNotEmpty())
                searchPath.addPath(juce::File(userPaths[i]));
        }
            
        return searchPath;
    }

    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& pluginList;
    juce::String formatName;
    bool scanTimedOut;
    int numFound;
    std::atomic<bool> scanCancelled;
    std::shared_ptr<PluginScanProgressListener> progressListener;
    std::mutex progressListenerMutex;
    std::mutex blacklistMutex;
    juce::FileSearchPath searchPath;
    std::chrono::steady_clock::time_point lastProgressUpdateTime;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SafePluginScanner)
};

#endif  // SAFEPLUGINSCANNER_H_INCLUDED

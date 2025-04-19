//
//  PluginWindow.h
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas
//  Modified for NovaHost April 18, 2025
//

#pragma once

#include <JuceHeader.h>
#include "GPUAccelerationManager.h"
#include <memory>

class PluginWindow : public juce::DocumentWindow, 
                     private juce::Timer, 
                     public std::enable_shared_from_this<PluginWindow>
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters
    };

    // Note: use createPluginWindow factory method instead of constructor directly
    PluginWindow(juce::Component* pluginEditor,
                 juce::AudioProcessorGraph::Node* owner,
                 WindowFormatType type);

    ~PluginWindow() override;

    // Factory method to properly create shared_ptr-managed windows
    static std::shared_ptr<PluginWindow> createPluginWindow(
        juce::Component* pluginEditor,
        juce::AudioProcessorGraph::Node* owner,
        WindowFormatType type);

    static PluginWindow* getWindowFor(juce::AudioProcessorGraph::Node* node,
                                     WindowFormatType type);

    static void closeCurrentlyOpenWindowsFor(const uint32 nodeId);

    static void closeAllCurrentlyOpenWindows();

    static bool containsActiveWindows();
    
    // Override DocumentWindow methods
    void moved() override;
    void closeButtonPressed() override;
    
    // Timer callback for handling UI responsiveness
    void timerCallback() override;

    static juce::String getLastXProp(WindowFormatType type)    { return "uiLastX_" + juce::String((int) type); }
    static juce::String getLastYProp(WindowFormatType type)    { return "uiLastY_" + juce::String((int) type); }
    static juce::String getOpenProp(WindowFormatType type)     { return "uiopen_"  + juce::String((int) type); }
    
    // Enable/disable GPU acceleration on this window
    void setGPUAccelerationEnabled(bool enabled);
    
    // Check if GPU acceleration is currently enabled
    bool isGPUAccelerationEnabled() const;

private:
    juce::AudioProcessorGraph::Node* owner;
    WindowFormatType type;
    int positionCheckCount = 5; // Number of timer checks to perform
    bool gpuAccelerationEnabled = false;
    
    // Helper method to position the window intelligently
    void positionPluginWindow();
    
    // Apply GPU acceleration to plugin editor if available
    void applyGPUAccelerationIfAvailable();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

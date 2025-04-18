//
//  PluginWindow.h
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas
//  Modified for NovaHost April 18, 2025
//

#pragma once

#include <JuceHeader.h>

class PluginWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters
    };

    PluginWindow (juce::Component* pluginEditor,
                 juce::AudioProcessorGraph::Node* owner,
                 WindowFormatType type);

    ~PluginWindow() override;

    static PluginWindow* getWindowFor (juce::AudioProcessorGraph::Node* node,
                                       WindowFormatType type);

    static void closeCurrentlyOpenWindowsFor (const uint32 nodeId);

    static void closeAllCurrentlyOpenWindows();

    static bool containsActiveWindows();
    
    // Override DocumentWindow methods
    void moved() override;
    void closeButtonPressed() override;
    
    // Timer callback for handling UI responsiveness
    void timerCallback() override;

    static juce::String getLastXProp (WindowFormatType type)    { return "uiLastX_" + juce::String ((int) type); }
    static juce::String getLastYProp (WindowFormatType type)    { return "uiLastY_" + juce::String ((int) type); }
    static juce::String getOpenProp  (WindowFormatType type)    { return "uiopen_"  + juce::String ((int) type); }

private:
    juce::AudioProcessorGraph::Node* owner;
    WindowFormatType type;
    int positionCheckCount = 5; // Number of timer checks to perform
    
    // Helper method to position the window intelligently
    void positionPluginWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

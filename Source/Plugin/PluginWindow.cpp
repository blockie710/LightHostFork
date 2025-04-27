#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"

static Array<PluginWindow*> activePluginWindows;

// Single definition of ProcessorProgramPropertyComp class
class ProcessorProgramPropertyComp : public PropertyComponent, private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp(const String& name, AudioProcessor& p, int index_)
        : PropertyComponent(name),
          owner(p),
          index(index_)
    {
        owner.addListener(this);
    }

    ~ProcessorProgramPropertyComp() override
    {
        owner.removeListener(this);
    }

    void refresh() override { }
    void audioProcessorChanged(AudioProcessor*) override { }
    void audioProcessorParameterChanged(AudioProcessor*, int, float) override { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor(AudioProcessor* const p)
        : AudioProcessorEditor(p)
    {
        jassert(p != nullptr);
        setOpaque(true);

        addAndMakeVisible(panel);

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        // More efficient properties array allocation
        Array<PropertyComponent*> programs;
        programs.ensureStorageAllocated(numPrograms);

        for (int i = 0; i < numPrograms; ++i)
        {
            String name(p->getProgramName(i).trim());
            if (name.isEmpty())
                name = "Unnamed";

            auto* pc = new ProcessorProgramPropertyComp(name, *p, i);
            programs.add(pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties(programs);
        setSize(400, jlimit(25, 400, totalHeight));
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::grey);
    }

    void resized() override
    {
        panel.setBounds(getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramAudioProcessorEditor)
};

PluginWindow::PluginWindow(Component* const pluginEditor,
                         AudioProcessorGraph::Node* const o,
                         WindowFormatType t)
    : DocumentWindow(pluginEditor->getName(), Colours::lightgrey,
                    DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(o),
      type(t)
{
    setSize(400, 300);
    setUsingNativeTitleBar(true);
    setContentOwned(pluginEditor, true);

    // Optimize window positioning with smarter placement strategy
    int defaultX, defaultY;

    #if JUCE_WINDOWS
    // Get screen dimensions for better positioning
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getMainDisplay().userArea;
    
    // Create deterministic but distributed positions based on window count
    // This avoids random placement while ensuring windows don't stack exactly
    defaultX = screenArea.getWidth() / 4 + (activePluginWindows.size() * 30) % (screenArea.getWidth() / 2);
    defaultY = screenArea.getHeight() / 4 + (activePluginWindows.size() * 25) % (screenArea.getHeight() / 2);
    
    // Ensure window is visible on screen
    defaultX = jlimit(20, screenArea.getWidth() - 420, defaultX);
    defaultY = jlimit(20, screenArea.getHeight() - 320, defaultY);
    #else
    // Default positioning for other platforms
    defaultX = 100 + (activePluginWindows.size() * 30) % 300;
    defaultY = 100 + (activePluginWindows.size() * 25) % 200;
    #endif

    setTopLeftPosition(owner->properties.getWithDefault(getLastXProp(type), defaultX),
                     owner->properties.getWithDefault(getLastYProp(type), defaultY));

    owner->properties.set(getOpenProp(type), true);
    setVisible(true);
    activePluginWindows.add(this);
}

void PluginWindow::closeCurrentlyOpenWindowsFor(const uint32 nodeId)
{
    // Use a temporary array to avoid modifying while iterating
    Array<PluginWindow*> windowsToClose;
    
    for (auto* window : activePluginWindows)
        if (window->owner->nodeId == nodeId)
            windowsToClose.add(window);
            
    for (auto* window : windowsToClose)
        delete window;
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    // Thread-safe implementation using a mutex to protect plugin window list access
    static std::mutex windowListMutex;
    std::lock_guard<std::mutex> lock(windowListMutex);
    
    if (activePluginWindows.isEmpty())
        return;
        
    // Make a copy to safely delete while iterating
    Array<PluginWindow*> windowsToClose;
    windowsToClose.addArray(activePluginWindows);
    
    for (auto* window : windowsToClose)
    {
        // Save window state before closing
        if (window != nullptr && window->owner != nullptr)
        {
            window->moved(); // Ensure position is saved
        }
        delete window;
    }

    // Only create a modal component if needed
    if (!activePluginWindows.isEmpty())
    {
        // Use MessageManager lock to safely handle any remaining windows
        MessageManager::getInstance()->callAsync([]() {
            // This will run on the message thread
            Component dummyModalComp;
            dummyModalComp.enterModalState();
            MessageManager::getInstance()->runDispatchLoopUntil(50);
        });
    }
}

bool PluginWindow::containsActiveWindows()
{
    return !activePluginWindows.isEmpty();
}

#if JUCE_WINDOWS
float PluginWindow::getDesktopScaleFactor() const
{
    // Get the appropriate scale factor based on the display
    return Desktop::getInstance().getDisplays().getDisplayContaining(getScreenBounds().getCentre()).scale;
}
#else
float PluginWindow::getDesktopScaleFactor() const
{
    return Desktop::getInstance().getGlobalScaleFactor();
}
#endif

//==============================================================================
PluginWindow* PluginWindow::getWindowFor(AudioProcessorGraph::Node* const node,
                                       WindowFormatType type)
{
    jassert(node != nullptr);

    // First check if this window already exists
    for (auto* window : activePluginWindows)
        if (window->owner == node && window->type == type)
            return window;

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();
        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor(processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor(processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*>(processor))
            ui->setName(plugin->getName());

        return new PluginWindow(ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue(this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set(getLastXProp(type), getX());
    owner->properties.set(getLastYProp(type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set(getOpenProp(type), false);
    delete this;
}

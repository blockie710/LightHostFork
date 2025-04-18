//
//  PluginWindow.cpp
//  Nova Host
//
//  Created originally for Light Host by Rolando Islas
//  Modified for NovaHost April 18, 2025
//

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"

class PluginWindow;
static Array<PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow(Component* const pluginEditor,
                           AudioProcessorGraph::Node* const o,
                           WindowFormatType t)
    : DocumentWindow(pluginEditor->getName(), Colours::lightgrey,
                     DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(o),
      type(t)
{
    // Set a good default size based on plugin editor size
    setContentOwned(pluginEditor, true);
    
    // Ensure the window is resizable if needed
    setResizable(pluginEditor->isResizable(), false);

    // Use native title bar for better OS integration
    setUsingNativeTitleBar(true);
    
    // Set window position with smart defaults based on screen size
    positionPluginWindow();

    owner->properties.set(getOpenProp(type), true);
    setVisible(true);

    activePluginWindows.add(this);
    
    // Start position & size timer to handle plugins that resize themselves after creation
    startTimer(500);
}

void PluginWindow::positionPluginWindow()
{
    const int defaultWidth = getWidth();
    const int defaultHeight = getHeight();
    
    // Try to restore previous position
    int defaultX = owner->properties.getWithDefault(getLastXProp(type), -1);
    int defaultY = owner->properties.getWithDefault(getLastYProp(type), -1);
    
    // If we don't have a saved position, create a reasonable one
    if (defaultX < 0 || defaultY < 0)
    {
        // Get screen info
        Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getDisplayContaining(
            Point<int>(0, 0)).userArea;
        
        // Choose a position that's visible and not overlapping with other windows
        defaultX = Random::getSystemRandom().nextInt(
            jmax(10, screenArea.getWidth() - defaultWidth - 50));
        defaultY = Random::getSystemRandom().nextInt(
            jmax(10, screenArea.getHeight() - defaultHeight - 50));
            
        // Try to avoid having windows stack directly on top of each other
        for (const auto* existingWindow : activePluginWindows)
        {
            if (std::abs(existingWindow->getX() - defaultX) < 50 && 
                std::abs(existingWindow->getY() - defaultY) < 50)
            {
                defaultX = (defaultX + 100) % (screenArea.getWidth() - defaultWidth - 20);
                defaultY = (defaultY + 100) % (screenArea.getHeight() - defaultHeight - 20);
            }
        }
    }
    
    #if JUCE_WINDOWS || JUCE_LINUX
    // Ensure window is fully visible on screen
    Rectangle<int> screenArea = Desktop::getInstance().getDisplays().getDisplayContaining(
        Point<int>(defaultX, defaultY)).userArea;
        
    defaultX = jlimit(screenArea.getX(), screenArea.getRight() - defaultWidth, defaultX);
    defaultY = jlimit(screenArea.getY(), screenArea.getBottom() - defaultHeight, defaultY);
    #endif
    
    // Set the window position now
    setTopLeftPosition(defaultX, defaultY);
}

void PluginWindow::timerCallback()
{
    // Handle plugins that resize themselves after creation
    Component* content = getContentComponent();
    if (content != nullptr)
    {
        const int contentWidth = content->getWidth();
        const int contentHeight = content->getHeight();
        
        // If the content component size doesn't match the window, resize
        if (getWidth() != contentWidth + getContentComponentBorder().getLeftAndRight() ||
            getHeight() != contentHeight + getContentComponentBorder().getTopAndBottom())
        {
            // Resize the window to properly fit the content
            setSize(contentWidth + getContentComponentBorder().getLeftAndRight(),
                   contentHeight + getContentComponentBorder().getTopAndBottom());
            
            // Check if window is now partially off-screen and reposition if needed
            Rectangle<int> screenArea = Desktop::getInstance().getDisplays()
                .getDisplayContaining(getBoundsInParent().getCentre()).userArea;
            
            int x = getX();
            int y = getY();
            bool needsRepositioning = false;
            
            if (x + getWidth() > screenArea.getRight())
            {
                x = screenArea.getRight() - getWidth();
                needsRepositioning = true;
            }
            
            if (y + getHeight() > screenArea.getBottom())
            {
                y = screenArea.getBottom() - getHeight();
                needsRepositioning = true;
            }
            
            if (x < screenArea.getX())
            {
                x = screenArea.getX();
                needsRepositioning = true;
            }
            
            if (y < screenArea.getY())
            {
                y = screenArea.getY();
                needsRepositioning = true;
            }
            
            if (needsRepositioning)
                setTopLeftPosition(x, y);
        }
    }
    
    // Only check a few times at startup, then stop the timer
    if (--positionCheckCount <= 0)
        stopTimer();
}

void PluginWindow::closeCurrentlyOpenWindowsFor(const uint32 nodeId)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner->nodeId == nodeId)
            delete activePluginWindows.getUnchecked(i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    if (activePluginWindows.size() > 0)
    {
        for (int i = activePluginWindows.size(); --i >= 0;)
            delete activePluginWindows.getUnchecked(i);

        Component dummyModalComp;
        dummyModalComp.enterModalState();
        MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent,
                                     private AudioProcessorListener
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

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name(p->getProgramName(i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            ProcessorProgramPropertyComp* const pc = new ProcessorProgramPropertyComp(name, *p, i);
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

//==============================================================================
PluginWindow* PluginWindow::getWindowFor(AudioProcessorGraph::Node* const node,
                                         WindowFormatType type)
{
    jassert(node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node
             && activePluginWindows.getUnchecked(i)->type == type)
            return activePluginWindows.getUnchecked(i);

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
        {
            // Create a more responsive generic editor with better automation control
            AudioProcessorEditor* editor = new GenericAudioProcessorEditor(processor);
            
            // Set a reasonable size for the generic editor
            const int numParameters = processor->getNumParameters();
            const int preferredHeight = jmin(600, 100 + numParameters * 25);
            editor->setSize(400, preferredHeight);
            
            ui = editor;
        }
        else if (type == Programs)
        {
            ui = new ProgramAudioProcessorEditor(processor);
        }
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

#include "../JuceLibraryCode/JuceHeader.h"
#include "../Source/UI/IconMenu.hpp"
#include "../Source/Core/HostStartup.cpp"

// Using JUCE's unit testing framework
class PluginHostTests : public UnitTest
{
public:
    PluginHostTests() : UnitTest("Plugin Host Tests") {}

    void runTest() override
    {
        beginTest("Basic Application Initialization");
        {
            // Test that the application can be initialized without crashing
            std::unique_ptr<PluginHostApp> app = std::make_unique<PluginHostApp>();
            expect(app != nullptr, "App should be created successfully");
        }

        beginTest("Audio Device Manager Initialization");
        {
            // Create a standalone AudioDeviceManager and verify it can be initialized
            AudioDeviceManager deviceManager;
            String error = deviceManager.initialiseWithDefaultDevices(2, 2);
            expect(error.isEmpty(), "AudioDeviceManager should initialize with default devices");
        }

        beginTest("Plugin Format Manager");
        {
            // Test that the plugin format manager can be initialized and formats registered
            AudioPluginFormatManager formatManager;
            formatManager.addDefaultFormats();
            expect(formatManager.getNumFormats() > 0, "Format manager should have formats registered");
        }

        beginTest("Audio Plugin Graph");
        {
            // Test creating a basic audio processor graph
            AudioProcessorGraph graph;
            expect(graph.getNumNodes() == 0, "New graph should have no nodes");
            
            // Create input and output nodes
            AudioProcessorGraph::NodeID inputNodeId = graph.addNode(
                std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
                    AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode))->nodeID;
                    
            AudioProcessorGraph::NodeID outputNodeId = graph.addNode(
                std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
                    AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode))->nodeID;
                    
            expect(graph.getNumNodes() == 2, "Graph should have input and output nodes");
        }
    }
};

static PluginHostTests pluginHostTests;

// This file can be used as a standalone test runner
#if JUCE_UNIT_TESTS
int main (int argc, char* argv[])
{
    UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    return 0;
}
#endif
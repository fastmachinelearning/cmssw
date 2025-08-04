#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonClient.h"
#include "HeterogeneousCore/SonicTriton/interface/RetryActionDiffServer.h"

#include <string>

// Anonymous namespace to hold our mock object, keeping it local to this test file.
namespace {
    // Mock TritonClient to intercept and verify method calls without needing a real server or CMSSW services.
    class MockTritonClient : public TritonClient {
    public:
        // Use the protected, testing-only constructor from the base class.
        MockTritonClient() : TritonClient(true) {}

        // --- Methods to override for testing ---
        void evaluate() override {
            // This method is called by RetryActionBase::eval()
            // We can leave it empty as the test directly calls retry().
        }

        void connectToServer(const std::string& url) override {
            connectToServer_called_ = true;
            last_url_ = url;
        }

        // --- Test utility methods ---
        bool connectToServerCalled() const { return connectToServer_called_; }
        const std::string& getLastUrl() const { return last_url_; }
        void reset() {
            connectToServer_called_ = false;
            last_url_ = "";
        }

    private:
        bool connectToServer_called_ = false;
        std::string last_url_;
    };
}

TEST_CASE("Test RetryActionDiffServer Logic", "[RetryActionDiffServer]") {

    // 1. Create the mock client object.
    MockTritonClient mockClient;

    // 2. Create the ParameterSet that configures the retry action.
    edm::ParameterSet retryPSet;
    const std::string alternate_server = "grpc://new-server-for-retry.com:8001";
    retryPSet.addUntrackedParameter<std::string>("diffServerUrl", alternate_server);
    
    // 3. Create an instance of the class we are testing.
    RetryActionDiffServer retryAction(retryPSet, &mockClient);

    SECTION("Retry calls connectToServer with the correct URL") {
        // ARRANGE: Reset state before the test.
        mockClient.reset();
        retryAction.start(); // Arms the action, setting shouldRetry_ = true

        // ACT: Manually call the retry method to simulate a failure event.
        retryAction.retry();

        // ASSERT: Verify that our mock's overridden method was called with the expected arguments.
        REQUIRE(mockClient.connectToServerCalled());
        REQUIRE(mockClient.getLastUrl() == alternate_server);
    }

    SECTION("Retry action is a one-shot") {
        // ARRANGE
        mockClient.reset();
        retryAction.start();

        // ACT
        retryAction.retry(); // First retry, should work.
        
        // After the first retry, the internal `shouldRetry_` flag should be false.
        // A second call to retry() should do nothing.
        // We can verify this by checking that connectToServer was not called a second time.
        mockClient.reset(); // Reset our trackers.
        retryAction.retry(); // Second retry, should fail silently.

        // ASSERT
        REQUIRE_FALSE(mockClient.connectToServerCalled());
    }
    
    SECTION("Start method re-arms the action") {
        // ARRANGE
        mockClient.reset();
        retryAction.start();
        retryAction.retry(); // Use up the action.
        REQUIRE_FALSE(retryAction.shouldRetry()); // Verify it's spent.

        // ACT: A new inference call begins, so `start()` is called again.
        retryAction.start();

        // ASSERT: The action should now be ready for another retry.
        REQUIRE(retryAction.shouldRetry());
    }

    SECTION("Constructor disables action if URL is missing") {
        // ARRANGE: Create a PSet with no URL.
        edm::ParameterSet emptyPSet;
        
        // ACT
        RetryActionDiffServer disabledAction(emptyPSet, &mockClient);

        // ASSERT
        REQUIRE_FALSE(disabledAction.shouldRetry());
    }
}

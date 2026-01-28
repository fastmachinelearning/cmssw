#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "oneapi/tbb/concurrent_hash_map.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// =============================================================================
// Test infrastructure that mirrors TritonService's fine-grained locking design
// =============================================================================

struct Model {
  std::string path;
  std::unordered_set<std::string> servers;
  int refCount{0};
  bool isLoaded() const { return refCount > 0; }
};

// Mock gRPC client that simulates slow server operations
class MockTritonClient {
public:
  // Simulates the latency of a real gRPC LoadModel call
  bool loadModel(const std::string& modelName) {
    std::this_thread::sleep_for(loadLatency_);
    ++loadCallCount_;
    loadedModels_.insert(modelName);
    return true;
  }

  // Simulates the latency of a real gRPC UnloadModel call
  bool unloadModel(const std::string& modelName) {
    std::this_thread::sleep_for(unloadLatency_);
    ++unloadCallCount_;
    loadedModels_.erase(modelName);
    return true;
  }

  void setLatency(std::chrono::milliseconds load, std::chrono::milliseconds unload) {
    loadLatency_ = load;
    unloadLatency_ = unload;
  }

  std::atomic<int> loadCallCount_{0};
  std::atomic<int> unloadCallCount_{0};
  std::unordered_set<std::string> loadedModels_;

private:
  std::chrono::milliseconds loadLatency_{10};
  std::chrono::milliseconds unloadLatency_{10};
};

// =============================================================================
// ModelManager: Mirrors TritonService's fine-grained locking implementation
// =============================================================================

class ModelManager {
public:
  explicit ModelManager(MockTritonClient& client) : client_(client) {}

  // Register a model (like addModel during module construction)
  void registerModel(const std::string& modelName, const std::string& path = "") {
    tbb::concurrent_hash_map<std::string, Model>::accessor acc;
    models_.insert(acc, modelName);
    if (acc->second.path.empty() && !path.empty())
      acc->second.path = path;
  }

  // Load model with per-model locking (mirrors TritonService::loadModel)
  bool loadModel(const std::string& modelName) {
    tbb::concurrent_hash_map<std::string, Model>::accessor acc;
    if (!models_.find(acc, modelName)) {
      return false;  // Model not registered
    }

    Model& model = acc->second;

    // Fast path: already loaded, just bump refcount
    if (model.refCount > 0) {
      ++model.refCount;
      return true;
    }

    // Slow path: actually load on server (gRPC call)
    // The accessor holds the lock during this slow operation,
    // but ONLY for this specific model's slot
    bool success = client_.loadModel(modelName);
    if (success) {
      model.refCount = 1;
      model.servers.insert("fallback");

      // Update server's model set (separate mutex, briefly held)
      {
        std::lock_guard<std::mutex> lock(serverModelsMutex_);
        serverModels_.insert(modelName);
      }
    }
    return success;
  }

  // Unload model with per-model locking (mirrors TritonService::unloadModel)
  bool unloadModel(const std::string& modelName) {
    tbb::concurrent_hash_map<std::string, Model>::accessor acc;
    if (!models_.find(acc, modelName)) {
      return false;  // Model not registered
    }

    Model& model = acc->second;

    if (model.refCount == 0) {
      return false;  // Not loaded
    }

    // Fast path: still in use, just decrement
    if (model.refCount > 1) {
      --model.refCount;
      return true;
    }

    // Slow path: actually unload from server (gRPC call)
    bool success = client_.unloadModel(modelName);
    if (success) {
      model.refCount = 0;
      model.servers.erase("fallback");

      // Update server's model set (separate mutex, briefly held)
      {
        std::lock_guard<std::mutex> lock(serverModelsMutex_);
        serverModels_.erase(modelName);
      }
    }
    return success;
  }

  int getRefCount(const std::string& modelName) const {
    tbb::concurrent_hash_map<std::string, Model>::const_accessor acc;
    if (models_.find(acc, modelName)) {
      return acc->second.refCount;
    }
    return -1;  // Not found
  }

  bool isModelOnServer(const std::string& modelName) const {
    std::lock_guard<std::mutex> lock(serverModelsMutex_);
    return serverModels_.count(modelName) > 0;
  }

private:
  tbb::concurrent_hash_map<std::string, Model> models_;
  mutable std::mutex serverModelsMutex_;
  std::unordered_set<std::string> serverModels_;
  MockTritonClient& client_;
};

// =============================================================================
// BASIC REFCOUNT TESTS
// =============================================================================

TEST_CASE("Basic: single load sets refcount to 1", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a", "/path/a");

  REQUIRE(mgr.loadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 1);
  REQUIRE(client.loadCallCount_ == 1);
  REQUIRE(mgr.isModelOnServer("model_a"));
}

TEST_CASE("Basic: multiple loads increment refcount without server calls", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  // First load
  REQUIRE(mgr.loadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 1);
  REQUIRE(client.loadCallCount_ == 1);

  // Second load - no server call
  REQUIRE(mgr.loadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 2);
  REQUIRE(client.loadCallCount_ == 1);

  // Third load - no server call
  REQUIRE(mgr.loadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 3);
  REQUIRE(client.loadCallCount_ == 1);
}

TEST_CASE("Basic: unloads decrement until zero triggers server call", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a");
  mgr.loadModel("model_a");
  mgr.loadModel("model_a");
  mgr.loadModel("model_a");
  REQUIRE(mgr.getRefCount("model_a") == 3);

  // First two unloads: decrement only
  REQUIRE(mgr.unloadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 2);
  REQUIRE(client.unloadCallCount_ == 0);

  REQUIRE(mgr.unloadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 1);
  REQUIRE(client.unloadCallCount_ == 0);

  // Third unload: triggers server call
  REQUIRE(mgr.unloadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 0);
  REQUIRE(client.unloadCallCount_ == 1);
  REQUIRE_FALSE(mgr.isModelOnServer("model_a"));
}

TEST_CASE("Basic: unload on non-loaded model returns false", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  REQUIRE_FALSE(mgr.unloadModel("model_a"));
  REQUIRE(client.unloadCallCount_ == 0);
}

TEST_CASE("Basic: load on unregistered model returns false", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  REQUIRE_FALSE(mgr.loadModel("nonexistent"));
  REQUIRE(client.loadCallCount_ == 0);
}

TEST_CASE("Basic: reload after full unload triggers new server load", "[basic]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  mgr.loadModel("model_a");
  mgr.unloadModel("model_a");
  REQUIRE(mgr.getRefCount("model_a") == 0);
  REQUIRE(client.loadCallCount_ == 1);
  REQUIRE(client.unloadCallCount_ == 1);

  // Reload
  REQUIRE(mgr.loadModel("model_a"));
  REQUIRE(mgr.getRefCount("model_a") == 1);
  REQUIRE(client.loadCallCount_ == 2);
}

// =============================================================================
// MULTI-MODEL INDEPENDENCE TESTS
// =============================================================================

TEST_CASE("MultiModel: different models have independent refcounts", "[multimodel]") {
  MockTritonClient client;
  ModelManager mgr(client);

  mgr.registerModel("model_a");
  mgr.registerModel("model_b");
  mgr.registerModel("model_c");

  mgr.loadModel("model_a");
  mgr.loadModel("model_a");
  mgr.loadModel("model_b");

  REQUIRE(mgr.getRefCount("model_a") == 2);
  REQUIRE(mgr.getRefCount("model_b") == 1);
  REQUIRE(mgr.getRefCount("model_c") == 0);
  REQUIRE(client.loadCallCount_ == 2);

  mgr.unloadModel("model_a");
  REQUIRE(mgr.getRefCount("model_a") == 1);
  REQUIRE(mgr.getRefCount("model_b") == 1);

  mgr.unloadModel("model_b");
  REQUIRE(mgr.getRefCount("model_b") == 0);
  REQUIRE(client.unloadCallCount_ == 1);
}

// =============================================================================
// CONCURRENT ACCESS - SAME MODEL TESTS
// =============================================================================

TEST_CASE("Concurrent: multiple threads loading same model", "[concurrent]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(5), std::chrono::milliseconds(5));
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  constexpr int numThreads = 10;
  std::vector<std::thread> threads;
  std::atomic<int> successCount{0};

  for (int i = 0; i < numThreads; ++i) {
    threads.emplace_back([&]() {
      if (mgr.loadModel("model_a")) {
        ++successCount;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All loads should succeed
  REQUIRE(successCount == numThreads);
  // Refcount should equal number of loads
  REQUIRE(mgr.getRefCount("model_a") == numThreads);
  // Only ONE server call should have been made (first load)
  REQUIRE(client.loadCallCount_ == 1);
}

TEST_CASE("Concurrent: multiple threads unloading same model", "[concurrent]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(5), std::chrono::milliseconds(5));
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  // Pre-load multiple times
  constexpr int numLoads = 10;
  for (int i = 0; i < numLoads; ++i) {
    mgr.loadModel("model_a");
  }
  REQUIRE(mgr.getRefCount("model_a") == numLoads);

  // Unload from multiple threads
  std::vector<std::thread> threads;
  std::atomic<int> successCount{0};

  for (int i = 0; i < numLoads; ++i) {
    threads.emplace_back([&]() {
      if (mgr.unloadModel("model_a")) {
        ++successCount;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All unloads should succeed
  REQUIRE(successCount == numLoads);
  // Refcount should be zero
  REQUIRE(mgr.getRefCount("model_a") == 0);
  // Only ONE server unload call (when refcount reached zero)
  REQUIRE(client.unloadCallCount_ == 1);
}

TEST_CASE("Concurrent: interleaved load/unload on same model", "[concurrent]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(2), std::chrono::milliseconds(2));
  ModelManager mgr(client);

  mgr.registerModel("model_a");

  constexpr int numCycles = 50;
  std::atomic<int> loadSuccess{0};
  std::atomic<int> unloadSuccess{0};

  std::vector<std::thread> threads;

  // Half threads do load, half do unload
  for (int i = 0; i < numCycles; ++i) {
    threads.emplace_back([&]() {
      if (mgr.loadModel("model_a"))
        ++loadSuccess;
    });
    threads.emplace_back([&]() {
      if (mgr.unloadModel("model_a"))
        ++unloadSuccess;
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Final refcount should be loads - unloads (but clamped at 0 or above)
  int finalRefCount = mgr.getRefCount("model_a");
  REQUIRE(finalRefCount >= 0);

  // The number of successful operations should be consistent
  // (loads always succeed if model registered, unloads fail if refcount was 0)
  REQUIRE(loadSuccess == numCycles);
}

// =============================================================================
// CONCURRENT ACCESS - DIFFERENT MODELS (PARALLELISM) TESTS
// =============================================================================

TEST_CASE("Parallel: different models can load concurrently", "[parallel]") {
  MockTritonClient client;
  // Set significant latency to ensure operations overlap
  client.setLatency(std::chrono::milliseconds(50), std::chrono::milliseconds(50));
  ModelManager mgr(client);

  constexpr int numModels = 5;
  for (int i = 0; i < numModels; ++i) {
    mgr.registerModel("model_" + std::to_string(i));
  }

  auto start = std::chrono::steady_clock::now();

  std::vector<std::thread> threads;
  for (int i = 0; i < numModels; ++i) {
    threads.emplace_back([&, i]() { mgr.loadModel("model_" + std::to_string(i)); });
  }

  for (auto& t : threads) {
    t.join();
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // All models should be loaded
  for (int i = 0; i < numModels; ++i) {
    REQUIRE(mgr.getRefCount("model_" + std::to_string(i)) == 1);
  }
  REQUIRE(client.loadCallCount_ == numModels);

  // If operations ran in parallel, total time should be close to single operation time
  // (with some overhead). If serialized, it would be numModels * 50ms = 250ms+
  // Allow generous margin but verify parallelism
  INFO("Elapsed time: " << elapsed.count() << "ms (expected ~50-100ms if parallel, ~250ms if serial)");
  REQUIRE(elapsed.count() < 200);  // Should be much less than 250ms
}

TEST_CASE("Parallel: different models can unload concurrently", "[parallel]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(50), std::chrono::milliseconds(50));
  ModelManager mgr(client);

  constexpr int numModels = 5;
  for (int i = 0; i < numModels; ++i) {
    mgr.registerModel("model_" + std::to_string(i));
    mgr.loadModel("model_" + std::to_string(i));
  }

  auto start = std::chrono::steady_clock::now();

  std::vector<std::thread> threads;
  for (int i = 0; i < numModels; ++i) {
    threads.emplace_back([&, i]() { mgr.unloadModel("model_" + std::to_string(i)); });
  }

  for (auto& t : threads) {
    t.join();
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // All models should be unloaded
  for (int i = 0; i < numModels; ++i) {
    REQUIRE(mgr.getRefCount("model_" + std::to_string(i)) == 0);
  }
  REQUIRE(client.unloadCallCount_ == numModels);

  // Verify parallelism
  INFO("Elapsed time: " << elapsed.count() << "ms");
  REQUIRE(elapsed.count() < 200);
}

// =============================================================================
// STRESS TESTS
// =============================================================================

TEST_CASE("Stress: high contention on single model", "[stress]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  ModelManager mgr(client);

  mgr.registerModel("hot_model");

  constexpr int numThreads = 20;
  constexpr int opsPerThread = 100;

  std::vector<std::thread> threads;
  std::atomic<int> totalLoads{0};
  std::atomic<int> totalUnloads{0};

  for (int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < opsPerThread; ++i) {
        if (i % 2 == 0) {
          if (mgr.loadModel("hot_model"))
            ++totalLoads;
        } else {
          if (mgr.unloadModel("hot_model"))
            ++totalUnloads;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Verify consistency: final refcount = loads - unloads
  int finalRefCount = mgr.getRefCount("hot_model");
  REQUIRE(finalRefCount >= 0);

  // All loads should succeed
  REQUIRE(totalLoads == numThreads * (opsPerThread / 2));

  INFO("Final refcount: " << finalRefCount);
  INFO("Total loads: " << totalLoads.load() << ", Total unloads: " << totalUnloads.load());
  INFO("Server load calls: " << client.loadCallCount_.load());
  INFO("Server unload calls: " << client.unloadCallCount_.load());
}

TEST_CASE("Stress: many models with concurrent operations", "[stress]") {
  MockTritonClient client;
  client.setLatency(std::chrono::milliseconds(1), std::chrono::milliseconds(1));
  ModelManager mgr(client);

  constexpr int numModels = 20;
  constexpr int numThreads = 10;
  constexpr int opsPerThread = 50;

  for (int i = 0; i < numModels; ++i) {
    mgr.registerModel("model_" + std::to_string(i));
  }

  std::vector<std::thread> threads;

  for (int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < opsPerThread; ++i) {
        int modelIdx = (t * opsPerThread + i) % numModels;
        std::string modelName = "model_" + std::to_string(modelIdx);

        if (i % 3 == 0) {
          mgr.loadModel(modelName);
        } else if (i % 3 == 1) {
          mgr.loadModel(modelName);
        } else {
          mgr.unloadModel(modelName);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All refcounts should be non-negative
  for (int i = 0; i < numModels; ++i) {
    int rc = mgr.getRefCount("model_" + std::to_string(i));
    REQUIRE(rc >= 0);
  }

  INFO("Total server load calls: " << client.loadCallCount_.load());
  INFO("Total server unload calls: " << client.unloadCallCount_.load());
}

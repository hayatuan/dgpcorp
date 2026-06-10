# Show Control Unit Tests

Basic unit tests for critical functionality in Show Control application.

## Tests Included

1. **testCleanVietnamese** - Tests Vietnamese character normalization
2. **testLoopStateInitialization** - Tests loop state for BGM lists
3. **testProjectVersion** - Tests project XML structure and version
4. **testAudioFilePath** - Tests audio file loading and validation
5. **testTimeFormatting** - Tests time string formatting (MM:SS.D)
6. **testTrimState** - Tests trim start/end state validation

## Compiling

### macOS/Linux (with clang/gcc):
```bash
cd Tests
clang++ -std=c++17 -o ShowControlTests ShowControlTests.cpp
./ShowControlTests
```

### With CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build . --target ShowControlTests
./ShowControlTests
```

## Test Results

All tests use a simple assert-based framework with visual feedback:
- ✅ = Test passed
- ❌ = Test failed

## Future Improvements

1. Integrate with JUCE's testing framework when JUCE testing is available
2. Add property-based testing for edge cases
3. Add integration tests for file I/O operations
4. Add performance benchmarks for audio processing

#include "GetAnotherIoLoop.h"

trantor::EventLoop* getAnotherIoLoop() {
    constexpr size_t defaultLoopIdA = 101;
    constexpr size_t defaultLoopIdB = 100;
    auto loopA = drogon::app().getIOLoop(defaultLoopIdA);
    auto loopB = drogon::app().getIOLoop(defaultLoopIdB);
    
    if ( loopA == loopB ) {
        LOG_WARN << "Please provide at least 2 threads for this plugin";
        return drogon::app().getLoop();
    }

    auto loop = loopA->isInLoopThread() ? loopB : loopA;
    
    assert(loop);                    // Should never be null
}
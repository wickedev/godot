// Does this Mac support Metal GPU counter sampling, and at which boundaries?
// Answers part (b) of the timestamp verdict: whether implementing timestamps in the
// Metal backend is even possible on Apple silicon, independent of whether Godot
// currently implements it (it does not).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static const char *pointName(MTLCounterSamplingPoint p) {
    switch (p) {
        case MTLCounterSamplingPointAtStageBoundary:        return "AtStageBoundary";
        case MTLCounterSamplingPointAtDrawBoundary:         return "AtDrawBoundary";
        case MTLCounterSamplingPointAtBlitBoundary:         return "AtBlitBoundary";
        case MTLCounterSamplingPointAtDispatchBoundary:     return "AtDispatchBoundary";
        case MTLCounterSamplingPointAtTileDispatchBoundary: return "AtTileDispatchBoundary";
    }
    return "?";
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { printf("RESULT device=NONE\n"); return 1; }
        printf("RESULT device=%s\n", [[dev name] UTF8String]);
        printf("RESULT os=%s\n", [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String]);

        MTLCounterSamplingPoint pts[] = {
            MTLCounterSamplingPointAtStageBoundary,
            MTLCounterSamplingPointAtDrawBoundary,
            MTLCounterSamplingPointAtBlitBoundary,
            MTLCounterSamplingPointAtDispatchBoundary,
            MTLCounterSamplingPointAtTileDispatchBoundary,
        };
        for (int i = 0; i < 5; i++) {
            BOOL ok = [dev supportsCounterSampling:pts[i]];
            printf("RESULT supportsCounterSampling[%-22s]=%s\n", pointName(pts[i]), ok ? "YES" : "no");
        }

        // A timestamp counter set must exist for GPU-side timing to be recoverable.
        BOOL haveTimestampSet = NO;
        for (id<MTLCounterSet> cs in [dev counterSets]) {
            printf("RESULT counterSet=%s\n", [[cs name] UTF8String]);
            if ([[cs name] isEqualToString:MTLCommonCounterSetTimestamp]) haveTimestampSet = YES;
        }
        printf("RESULT hasTimestampCounterSet=%s\n", haveTimestampSet ? "YES" : "no");

        // CPU<->GPU correlation, which any cross-timeline profiler needs.
        MTLTimestamp cpu = 0, gpu = 0;
        [dev sampleTimestamps:&cpu gpuTimestamp:&gpu];
        printf("RESULT sampleTimestamps cpu=%llu gpu=%llu usable=%s\n",
               (unsigned long long)cpu, (unsigned long long)gpu,
               (cpu != 0 && gpu != 0) ? "YES" : "no");
    }
    return 0;
}

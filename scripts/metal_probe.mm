#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstdio>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            std::fprintf(stderr, "ERROR: no Metal device is available\n");
            return 2;
        }

        static NSString *source =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "kernel void netherite_probe(device uint *out [[buffer(0)]], "
             "uint gid [[thread_position_in_grid]]) {\n"
             "  if (gid == 0) out[0] = 0x4d544c31u;\n"
             "}\n";

        MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            options.mathMode = MTLMathModeSafe;
        } else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            options.fastMathEnabled = NO;
#pragma clang diagnostic pop
        }
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                    options:options
                                                      error:&error];
        if (library == nil) {
            std::fprintf(stderr, "ERROR: Metal runtime compilation failed: %s\n",
                         error.localizedDescription.UTF8String);
            return 3;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"netherite_probe"];
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&error];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLBuffer> output = [device newBufferWithLength:sizeof(std::uint32_t)
                                                options:MTLResourceStorageModeShared];
        if (pipeline == nil || queue == nil || output == nil) {
            std::fprintf(stderr, "ERROR: Metal probe resource creation failed: %s\n",
                         error != nil ? error.localizedDescription.UTF8String : "unknown error");
            return 4;
        }

        *static_cast<std::uint32_t *>(output.contents) = 0;
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:output offset:0 atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(1, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];

        if (command.status != MTLCommandBufferStatusCompleted ||
            *static_cast<std::uint32_t *>(output.contents) != 0x4d544c31u) {
            std::fprintf(stderr, "ERROR: Metal command failed: %s\n",
                         command.error != nil
                             ? command.error.localizedDescription.UTF8String
                             : "unexpected probe result");
            return 5;
        }

        const unsigned long long max_buffer =
            static_cast<unsigned long long>(device.maxBufferLength);
        const unsigned long long working_set =
            static_cast<unsigned long long>(device.recommendedMaxWorkingSetSize);
        std::printf("Metal device: %s\n", device.name.UTF8String);
        std::printf("unified-memory: %s\n", device.hasUnifiedMemory ? "yes" : "no");
        std::printf("max-buffer-bytes: %llu\n", max_buffer);
        std::printf("recommended-working-set-bytes: %llu\n", working_set);
        std::printf("runtime-msl-dispatch: PASS\n");
    }
    return 0;
}

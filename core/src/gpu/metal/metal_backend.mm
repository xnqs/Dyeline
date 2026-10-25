// Metal backend: compiles the shared kernels for the host's device and records work on the
// host's command queue.

#import <Metal/Metal.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "dyeline/gpu.h"

// Generated at build time from kernels.metal and the shared kernel headers.
extern const char* const kDyelineMetalSource;

namespace {

struct Pipelines {
    id<MTLComputePipelineState> chain = nil;
};

// One set of compiled pipelines per Metal device, built on first use.
std::string pipelines_for(id<MTLDevice> device, Pipelines& out) {
    static std::mutex mutex;
    static std::unordered_map<uintptr_t, Pipelines> cache;

    std::lock_guard<std::mutex> lock(mutex);
    const auto key = reinterpret_cast<uintptr_t>((__bridge void*)device);
    if (auto it = cache.find(key); it != cache.end()) {
        out = it->second;
        return {};
    }

    @autoreleasepool {
        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:kDyelineMetalSource];
        MTLCompileOptions* options = [MTLCompileOptions new];
        // Precise math, so a look renders the same on a Mac as on the CPU and other GPUs.
        options.fastMathEnabled = NO;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
        if (library == nil) {
            return std::string("Metal kernel compile failed: ") +
                   (error ? error.localizedDescription.UTF8String : "unknown error");
        }
        id<MTLFunction> function = [library newFunctionWithName:@"dl_chain_kernel"];
        if (function == nil) {
            return "Metal kernel dl_chain_kernel not found";
        }
        Pipelines pipelines;
        pipelines.chain = [device newComputePipelineStateWithFunction:function error:&error];
        if (pipelines.chain == nil) {
            return std::string("Metal pipeline creation failed: ") +
                   (error ? error.localizedDescription.UTF8String : "unknown error");
        }
        cache.emplace(key, pipelines);
        out = pipelines;
    }
    return {};
}

}  // namespace

namespace dyeline::metal {

bool available() {
    return true;
}

std::string render_chain(void* command_queue, const void* src_buffer, void* dst_buffer,
                         const dl_image_args& args, const dl_chain& chain, bool wait) {
    if (args.width <= 0 || args.height <= 0) {
        return {};
    }
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue;
    id<MTLBuffer> src = (__bridge id<MTLBuffer>)src_buffer;
    id<MTLBuffer> dst = (__bridge id<MTLBuffer>)dst_buffer;
    if (queue == nil || src == nil || dst == nil) {
        return "Metal render called without a command queue or buffers";
    }

    Pipelines pipelines;
    if (std::string err = pipelines_for(queue.device, pipelines); !err.empty()) {
        return err;
    }

    @autoreleasepool {
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:pipelines.chain];
        [encoder setBuffer:src offset:0 atIndex:0];
        [encoder setBuffer:dst offset:0 atIndex:1];
        [encoder setBytes:&args length:sizeof(args) atIndex:2];
        [encoder setBytes:&chain length:sizeof(chain) atIndex:3];  // under setBytes' 4 KB limit

        const NSUInteger tw = pipelines.chain.threadExecutionWidth;
        const NSUInteger th = pipelines.chain.maxTotalThreadsPerThreadgroup / tw;
        const MTLSize group = MTLSizeMake(tw, th, 1);
        const MTLSize groups = MTLSizeMake((static_cast<NSUInteger>(args.width) + tw - 1) / tw,
                                           (static_cast<NSUInteger>(args.height) + th - 1) / th, 1);
        [encoder dispatchThreadgroups:groups threadsPerThreadgroup:group];
        [encoder endEncoding];
        [commands commit];
        if (wait) {
            [commands waitUntilCompleted];
            if (commands.error != nil) {
                return std::string("Metal render failed: ") +
                       commands.error.localizedDescription.UTF8String;
            }
        }
    }
    return {};
}

}  // namespace dyeline::metal

// Metal backend: compiles the shared kernels for the host's device and records a render
// plan's passes on the host's command queue.

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
    id<MTLComputePipelineState> extract = nil;
    id<MTLComputePipelineState> box = nil;
};

id<MTLComputePipelineState> make_pipeline(id<MTLDevice> device, id<MTLLibrary> library,
                                          NSString* name, std::string& error) {
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (function == nil) {
        error = std::string("Metal kernel ") + name.UTF8String + " not found";
        return nil;
    }
    NSError* ns_error = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&ns_error];
    if (pipeline == nil) {
        error = std::string("Metal pipeline creation failed: ") +
                (ns_error ? ns_error.localizedDescription.UTF8String : "unknown error");
    }
    return pipeline;
}

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
        std::string failure;
        Pipelines pipelines;
        pipelines.chain = make_pipeline(device, library, @"dl_chain_kernel", failure);
        if (pipelines.chain != nil) {
            pipelines.extract = make_pipeline(device, library, @"dl_extract_kernel", failure);
        }
        if (pipelines.extract != nil) {
            pipelines.box = make_pipeline(device, library, @"dl_box_kernel", failure);
        }
        if (pipelines.box == nil) {
            return failure;
        }
        cache.emplace(key, pipelines);
        out = pipelines;
    }
    return {};
}

void dispatch_2d(id<MTLComputeCommandEncoder> encoder, id<MTLComputePipelineState> pipeline,
                 int width, int height) {
    const NSUInteger tw = pipeline.threadExecutionWidth;
    const NSUInteger th = pipeline.maxTotalThreadsPerThreadgroup / tw;
    const MTLSize group = MTLSizeMake(tw, th, 1);
    const MTLSize groups = MTLSizeMake((static_cast<NSUInteger>(width) + tw - 1) / tw,
                                       (static_cast<NSUInteger>(height) + th - 1) / th, 1);
    [encoder dispatchThreadgroups:groups threadsPerThreadgroup:group];
}

}  // namespace

namespace dyeline::metal {

bool available() {
    return true;
}

std::string render_plan(void* command_queue, const void* src_buffer, void* dst_buffer,
                        const RenderPlan& plan, const float* table, std::size_t table_floats,
                        bool wait) {
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
        // Copies the command buffer keeps alive until the work is done. Empty ones are bound
        // as a single float, since a kernel argument can't be left unbound.
        id<MTLBuffer> table_buffer = nil;
        if (table_floats > 0) {
            table_buffer = [queue.device newBufferWithBytes:table
                                                     length:table_floats * sizeof(float)
                                                    options:MTLResourceStorageModeShared];
        } else {
            table_buffer = [queue.device newBufferWithLength:sizeof(float)
                                                     options:MTLResourceStorageModeShared];
        }
        id<MTLBuffer> work = [queue.device
            newBufferWithLength:(plan.work_floats > 0 ? plan.work_floats : 1) * sizeof(float)
                        options:MTLResourceStorageModePrivate];
        if (table_buffer == nil || work == nil) {
            return "Could not allocate Metal memory for the look";
        }

        // One encoder per pass, so each pass sees the previous one's results.
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        for (const RenderPass& pass : plan.passes) {
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            switch (pass.kind) {
                case PassKind::Extract:
                    if (pass.image.width > 0 && pass.image.height > 0) {
                        [encoder setComputePipelineState:pipelines.extract];
                        [encoder setBuffer:src offset:0 atIndex:0];
                        [encoder setBuffer:work offset:0 atIndex:1];
                        [encoder setBytes:&pass.image length:sizeof(pass.image) atIndex:2];
                        // Under setBytes' 4 KB limit.
                        [encoder setBytes:&plan.chain length:sizeof(plan.chain) atIndex:3];
                        [encoder setBuffer:table_buffer offset:0 atIndex:4];
                        [encoder setBytes:&pass.stop length:sizeof(pass.stop) atIndex:5];
                        dispatch_2d(encoder, pipelines.extract, pass.image.width, pass.image.height);
                    }
                    break;
                case PassKind::Blur:
                    if (pass.box.lines > 0 && pass.box.length > 0) {
                        [encoder setComputePipelineState:pipelines.box];
                        [encoder setBuffer:work offset:0 atIndex:0];
                        [encoder setBytes:&pass.box length:sizeof(pass.box) atIndex:1];
                        const NSUInteger width = pipelines.box.threadExecutionWidth;
                        [encoder dispatchThreadgroups:MTLSizeMake((static_cast<NSUInteger>(pass.box.lines) +
                                                                   width - 1) / width, 1, 1)
                                threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
                    }
                    break;
                case PassKind::Final:
                    if (pass.image.width > 0 && pass.image.height > 0) {
                        [encoder setComputePipelineState:pipelines.chain];
                        [encoder setBuffer:src offset:0 atIndex:0];
                        [encoder setBuffer:dst offset:0 atIndex:1];
                        [encoder setBytes:&pass.image length:sizeof(pass.image) atIndex:2];
                        [encoder setBytes:&plan.chain length:sizeof(plan.chain) atIndex:3];
                        [encoder setBuffer:table_buffer offset:0 atIndex:4];
                        [encoder setBuffer:work offset:0 atIndex:5];
                        dispatch_2d(encoder, pipelines.chain, pass.image.width, pass.image.height);
                    }
                    break;
            }
            [encoder endEncoding];
        }
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

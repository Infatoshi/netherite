#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "mcsim_metal.h"
#include "mcsim_kernels_source.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

struct McSimMetalContext {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> library;
    id<MTLComputePipelineState> smoke_pipeline;
    id<MTLComputePipelineState> camera_pipeline;
    id<MTLComputePipelineState> layout_pipeline;
};

namespace {

constexpr size_t kOcPixels = 64u * 36u;
constexpr size_t kSinTableCount = 65536u;
constexpr size_t kPreferredThreads = 127u;
constexpr uint64_t kSmokeSentinel = UINT64_C(0xd15ea5edcafef00d);
constexpr uint16_t kIdSentinel = UINT16_C(0xa55a);
constexpr uint8_t kDepthSentinel = UINT8_C(0xcd);
constexpr uint8_t kEdgeSentinel = UINT8_C(0xfe);

struct DispatchShape {
    NSUInteger threads_per_group;
    NSUInteger groups;
    size_t dispatched_threads;
};

static void clear_error(McSimMetalError *error) {
    if (!error) return;
    error->status = MCSIM_METAL_OK;
    error->message[0] = '\0';
}

static McSimMetalStatus set_error(McSimMetalError *error,
                                  McSimMetalStatus status,
                                  const char *format, ...) {
    if (error) {
        error->status = status;
        va_list ap;
        va_start(ap, format);
        std::vsnprintf(error->message, sizeof(error->message), format, ap);
        va_end(ap);
    }
    return status;
}

static const char *ns_error_text(NSError *error) {
    if (!error) return "unknown Metal error";
    const char *text = error.localizedDescription.UTF8String;
    return text ? text : "Metal error without a UTF-8 description";
}

static bool checked_add(size_t a, size_t b, size_t *out) {
    if (a > std::numeric_limits<size_t>::max() - b) return false;
    *out = a + b;
    return true;
}

static bool checked_mul(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) return false;
    *out = a * b;
    return true;
}

static McSimMetalStatus dispatch_shape(id<MTLComputePipelineState> pipeline,
                                       size_t logical_threads,
                                       DispatchShape *shape,
                                       McSimMetalError *error) {
    NSUInteger maximum = pipeline.maxTotalThreadsPerThreadgroup;
    if (maximum == 0)
        return set_error(error, MCSIM_METAL_PIPELINE,
                         "Metal pipeline reports zero threads per threadgroup");
    NSUInteger threads = std::min<NSUInteger>(maximum, kPreferredThreads);
    size_t rounded;
    if (!checked_add(logical_threads, static_cast<size_t>(threads) - 1, &rounded))
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "threadgroup rounding overflow for %zu threads",
                         logical_threads);
    size_t base_groups = rounded / static_cast<size_t>(threads);
    size_t groups;
    /* One complete extra group is deliberate: every gate exercises the
     * shader's overdispatch guard, even when the logical count is divisible. */
    if (!checked_add(base_groups, 1, &groups) ||
        !checked_mul(groups, static_cast<size_t>(threads), &shape->dispatched_threads))
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "Metal dispatch size overflow for %zu threads",
                         logical_threads);
    if (groups > std::numeric_limits<NSUInteger>::max())
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "Metal threadgroup count does not fit NSUInteger");
    shape->threads_per_group = threads;
    shape->groups = static_cast<NSUInteger>(groups);
    return MCSIM_METAL_OK;
}

static McSimMetalStatus checked_buffer_length(McSimMetalContext *context,
                                              size_t count,
                                              size_t element_size,
                                              const char *label,
                                              size_t *bytes,
                                              McSimMetalError *error) {
    if (!checked_mul(count, element_size, bytes))
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "%s buffer size overflow: %zu elements of %zu bytes",
                         label, count, element_size);
    if (*bytes == 0)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "%s buffer may not be empty", label);
    if (*bytes > static_cast<size_t>(context->device.maxBufferLength))
        return set_error(error, MCSIM_METAL_ALLOC,
                         "%s buffer requires %zu bytes, device maximum is %llu",
                         label, *bytes,
                         static_cast<unsigned long long>(context->device.maxBufferLength));
    return MCSIM_METAL_OK;
}

static id<MTLBuffer> new_shared_buffer(McSimMetalContext *context,
                                       size_t count,
                                       size_t element_size,
                                       const void *initial,
                                       const char *label,
                                       McSimMetalError *error) {
    size_t bytes;
    if (checked_buffer_length(context, count, element_size, label, &bytes, error)
        != MCSIM_METAL_OK)
        return nil;
    id<MTLBuffer> buffer;
    if (initial) {
        buffer = [context->device newBufferWithBytes:initial
                                              length:static_cast<NSUInteger>(bytes)
                                             options:MTLResourceStorageModeShared];
    } else {
        buffer = [context->device newBufferWithLength:static_cast<NSUInteger>(bytes)
                                               options:MTLResourceStorageModeShared];
    }
    if (!buffer) {
        set_error(error, MCSIM_METAL_ALLOC,
                  "Metal failed to allocate %s buffer (%zu bytes)", label, bytes);
        return nil;
    }
    buffer.label = [NSString stringWithUTF8String:label];
    return buffer;
}

static McSimMetalStatus finish_command(id<MTLCommandBuffer> command,
                                       const char *operation,
                                       McSimMetalError *error) {
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) {
        return set_error(error, MCSIM_METAL_COMMAND,
                         "%s command failed (status %lu): %s",
                         operation, static_cast<unsigned long>(command.status),
                         ns_error_text(command.error));
    }
    return MCSIM_METAL_OK;
}

static McSimMetalStatus make_pipeline(McSimMetalContext *context,
                                      NSString *name,
                                      id<MTLComputePipelineState> *out_pipeline,
                                      McSimMetalError *error) {
    id<MTLFunction> function = [context->library newFunctionWithName:name];
    if (!function)
        return set_error(error, MCSIM_METAL_PIPELINE,
                         "Metal shader function '%s' is missing",
                         name.UTF8String);
    NSError *ns_error = nil;
    id<MTLComputePipelineState> pipeline =
        [context->device newComputePipelineStateWithFunction:function error:&ns_error];
    if (!pipeline)
        return set_error(error, MCSIM_METAL_PIPELINE,
                         "Metal pipeline '%s' failed: %s",
                         name.UTF8String, ns_error_text(ns_error));
    *out_pipeline = pipeline;
    return MCSIM_METAL_OK;
}

static McSimMetalStatus validate_context(McSimMetalContext *context,
                                         McSimMetalError *error) {
    if (!context)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "Metal context is null");
    return MCSIM_METAL_OK;
}

}  // namespace

extern "C" const char *mcsim_metal_status_string(McSimMetalStatus status) {
    switch (status) {
        case MCSIM_METAL_OK: return "ok";
        case MCSIM_METAL_NO_DEVICE: return "no-device";
        case MCSIM_METAL_SHADER_COMPILE: return "shader-compile";
        case MCSIM_METAL_PIPELINE: return "pipeline";
        case MCSIM_METAL_SIZE_OVERFLOW: return "size-overflow";
        case MCSIM_METAL_ALLOC: return "allocation";
        case MCSIM_METAL_COMMAND: return "command";
        case MCSIM_METAL_UNSUPPORTED_FP64: return "unsupported-fp64";
        case MCSIM_METAL_INVALID_ARGUMENT: return "invalid-argument";
        case MCSIM_METAL_DETERMINISM: return "determinism";
        case MCSIM_METAL_LAYOUT: return "layout";
    }
    return "unknown";
}

extern "C" McSimMetalStatus mcsim_metal_create(McSimMetalContext **out_context,
                                                McSimMetalError *error) {
    McSimMetalError local_error{};
    if (!error) error = &local_error;
    clear_error(error);
    if (!out_context)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "output context pointer is null");
    *out_context = nullptr;

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return set_error(error, MCSIM_METAL_NO_DEVICE,
                             "MTLCreateSystemDefaultDevice returned no Metal device");

        McSimMetalContext *context = new (std::nothrow) McSimMetalContext();
        if (!context)
            return set_error(error, MCSIM_METAL_ALLOC,
                             "failed to allocate Metal context");
        context->device = device;
        context->queue = [device newCommandQueue];
        if (!context->queue) {
            delete context;
            return set_error(error, MCSIM_METAL_ALLOC,
                             "failed to create Metal command queue");
        }

        MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
        if (@available(macOS 15.0, *)) {
            options.mathMode = MTLMathModeSafe;
            options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            options.fastMathEnabled = NO;
#pragma clang diagnostic pop
        }
        NSError *ns_error = nil;
        NSString *source = [[NSString alloc] initWithBytes:kMcsimMetalSource
                                                    length:sizeof(kMcsimMetalSource) - 1
                                                  encoding:NSUTF8StringEncoding];
        context->library = [device newLibraryWithSource:source
                                                options:options
                                                  error:&ns_error];
        if (!context->library) {
            McSimMetalStatus result =
                set_error(error, MCSIM_METAL_SHADER_COMPILE,
                          "runtime Metal shader compilation failed: %s",
                          ns_error_text(ns_error));
            delete context;
            return result;
        }

        McSimMetalStatus status;
        id<MTLComputePipelineState> pipeline = nil;
        status = make_pipeline(context, @"mcsim_smoke", &pipeline, error);
        if (status == MCSIM_METAL_OK) context->smoke_pipeline = pipeline;
        if (status == MCSIM_METAL_OK) {
            pipeline = nil;
            status = make_pipeline(context, @"mcsim_obs_camera", &pipeline,
                                   error);
            if (status == MCSIM_METAL_OK) context->camera_pipeline = pipeline;
        }
        if (status == MCSIM_METAL_OK) {
            pipeline = nil;
            status = make_pipeline(context, @"mcsim_layout_probe", &pipeline,
                                   error);
            if (status == MCSIM_METAL_OK) context->layout_pipeline = pipeline;
        }
        if (status != MCSIM_METAL_OK) {
            delete context;
            return status;
        }

        McSimMetalLayoutProbe probe{};
        status = mcsim_metal_layout_probe(context, &probe, error);
        if (status != MCSIM_METAL_OK) {
            delete context;
            return status;
        }
        *out_context = context;
    }
    return MCSIM_METAL_OK;
}

extern "C" void mcsim_metal_destroy(McSimMetalContext *context) {
    delete context;
}

extern "C" McSimMetalStatus mcsim_metal_layout_probe(
    McSimMetalContext *context, McSimMetalLayoutProbe *out_probe,
    McSimMetalError *error) {
    McSimMetalError local_error{};
    if (!error) error = &local_error;
    clear_error(error);
    McSimMetalStatus status = validate_context(context, error);
    if (status != MCSIM_METAL_OK) return status;
    if (!out_probe)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "layout probe output is null");

    @autoreleasepool {
        id<MTLBuffer> buffer = new_shared_buffer(
            context, 1, sizeof(McSimMetalLayoutProbe), nullptr,
            "mc-sim layout probe", error);
        if (!buffer) return error ? error->status : MCSIM_METAL_ALLOC;
        std::memset(buffer.contents, 0, sizeof(McSimMetalLayoutProbe));

        id<MTLCommandBuffer> command = [context->queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!command || !encoder)
            return set_error(error, MCSIM_METAL_COMMAND,
                             "failed to create layout-probe command encoder");
        [encoder setComputePipelineState:context->layout_pipeline];
        [encoder setBuffer:buffer offset:0 atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                  threadsPerThreadgroup:MTLSizeMake(7, 1, 1)];
        [encoder endEncoding];
        status = finish_command(command, "layout probe", error);
        if (status != MCSIM_METAL_OK) return status;

        McSimMetalLayoutProbe probe =
            *static_cast<const McSimMetalLayoutProbe *>(buffer.contents);
        McSimMetalLayoutProbe expected{
            MCSIM_METAL_ABI_VERSION,
            static_cast<uint32_t>(sizeof(McSimMetalSmokeParams)),
            static_cast<uint32_t>(sizeof(McSimMetalOcRegionDesc)),
            static_cast<uint32_t>(sizeof(McSimMetalOcCameraDesc)),
            static_cast<uint32_t>(sizeof(McSimMetalOcPose))
        };
        if (std::memcmp(&probe, &expected, sizeof(probe)) != 0)
            return set_error(error, MCSIM_METAL_LAYOUT,
                             "Metal ABI mismatch: device={v%u smoke=%u region=%u camera=%u pose=%u} "
                             "host={v%u smoke=%u region=%u camera=%u pose=%u}",
                             probe.abi_version, probe.smoke_params_size,
                             probe.region_desc_size, probe.camera_desc_size,
                             probe.pose_size, expected.abi_version,
                             expected.smoke_params_size, expected.region_desc_size,
                             expected.camera_desc_size, expected.pose_size);
        *out_probe = probe;
    }
    return MCSIM_METAL_OK;
}

extern "C" McSimMetalStatus mcsim_metal_smoke(
    McSimMetalContext *context, uint64_t world_seed, uint64_t *out_values,
    size_t count, unsigned repeat_count, McSimMetalError *error) {
    McSimMetalError local_error{};
    if (!error) error = &local_error;
    clear_error(error);
    McSimMetalStatus status = validate_context(context, error);
    if (status != MCSIM_METAL_OK) return status;
    if (count > UINT32_MAX)
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "smoke count %zu exceeds the fixed uint32 ABI", count);
    if (count != 0 && !out_values)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "smoke output is null for %zu values", count);
    if (repeat_count == 0) repeat_count = 1;

    @autoreleasepool {
        DispatchShape shape{};
        status = dispatch_shape(context->smoke_pipeline, count, &shape, error);
        if (status != MCSIM_METAL_OK) return status;

        McSimMetalSmokeParams params{
            world_seed, static_cast<uint32_t>(count), static_cast<uint32_t>(count)
        };
        id<MTLBuffer> params_buffer = new_shared_buffer(
            context, 1, sizeof(params), &params, "mc-sim smoke params", error);
        id<MTLBuffer> output_buffer = new_shared_buffer(
            context, shape.dispatched_threads, sizeof(uint64_t), nullptr,
            "mc-sim smoke output", error);
        if (!params_buffer || !output_buffer)
            return error ? error->status : MCSIM_METAL_ALLOC;

        uint64_t *device_output = static_cast<uint64_t *>(output_buffer.contents);
        for (unsigned repeat = 0; repeat < repeat_count; ++repeat) {
            std::fill(device_output,
                      device_output + shape.dispatched_threads,
                      kSmokeSentinel);
            id<MTLCommandBuffer> command = [context->queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            if (!command || !encoder)
                return set_error(error, MCSIM_METAL_COMMAND,
                                 "failed to create smoke command encoder");
            [encoder setComputePipelineState:context->smoke_pipeline];
            [encoder setBuffer:params_buffer offset:0 atIndex:0];
            [encoder setBuffer:output_buffer offset:0 atIndex:1];
            [encoder dispatchThreadgroups:MTLSizeMake(shape.groups, 1, 1)
                      threadsPerThreadgroup:MTLSizeMake(shape.threads_per_group, 1, 1)];
            [encoder endEncoding];
            status = finish_command(command, "smoke", error);
            if (status != MCSIM_METAL_OK) return status;

            for (size_t i = count; i < shape.dispatched_threads; ++i) {
                if (device_output[i] != kSmokeSentinel)
                    return set_error(error, MCSIM_METAL_DETERMINISM,
                                     "smoke overdispatch wrote index %zu: "
                                     "expected sentinel %016llx, got %016llx",
                                     i,
                                     static_cast<unsigned long long>(kSmokeSentinel),
                                     static_cast<unsigned long long>(device_output[i]));
            }
            if (repeat == 0) {
                if (count != 0)
                    std::memcpy(out_values, device_output,
                                count * sizeof(uint64_t));
            } else {
                for (size_t i = 0; i < count; ++i) {
                    if (device_output[i] != out_values[i])
                        return set_error(error, MCSIM_METAL_DETERMINISM,
                                         "smoke repeat %u first differs at %zu: "
                                         "run0=%016llx repeat=%016llx",
                                         repeat + 1, i,
                                         static_cast<unsigned long long>(out_values[i]),
                                         static_cast<unsigned long long>(device_output[i]));
                }
            }
        }
    }
    return MCSIM_METAL_OK;
}

extern "C" McSimMetalStatus mcsim_metal_obs_camera(
    McSimMetalContext *context, const uint16_t *cells, size_t cell_count,
    const McSimMetalOcRegionDesc *region, const float *sin_table,
    size_t sin_table_count, const McSimMetalOcCameraDesc *camera,
    const McSimMetalOcPose *poses, size_t pose_count,
    uint16_t *out_ids, uint8_t *out_depth, uint8_t *out_edge,
    unsigned repeat_count, McSimMetalError *error) {
    McSimMetalError local_error{};
    if (!error) error = &local_error;
    clear_error(error);
    McSimMetalStatus status = validate_context(context, error);
    if (status != MCSIM_METAL_OK) return status;
    if (!region || !camera || !cells || !sin_table || !poses || !out_ids ||
        !out_depth || !out_edge)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera received a null descriptor or buffer");
    if (region->nx <= 0 || region->ny <= 0 || region->nz <= 0)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera region dimensions must be positive, got %d x %d x %d",
                         region->nx, region->ny, region->nz);
    if (pose_count == 0 || pose_count > UINT32_MAX)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera pose count %zu is outside 1..UINT32_MAX",
                         pose_count);
    if (camera->pose_count != pose_count)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera descriptor pose count %u differs from array count %zu",
                         camera->pose_count, pose_count);
    if (sin_table_count != kSinTableCount)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera requires %zu sine values, got %zu",
                         kSinTableCount, sin_table_count);

    size_t xy, needed_cells, total;
    if (!checked_mul(static_cast<size_t>(region->nx),
                     static_cast<size_t>(region->ny), &xy) ||
        !checked_mul(xy, static_cast<size_t>(region->nz), &needed_cells) ||
        !checked_mul(pose_count, kOcPixels, &total))
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "obs_camera region or output size overflow");
    if (cell_count < needed_cells)
        return set_error(error, MCSIM_METAL_INVALID_ARGUMENT,
                         "obs_camera region needs %zu cells, caller supplied %zu",
                         needed_cells, cell_count);
    if (total > UINT32_MAX)
        return set_error(error, MCSIM_METAL_SIZE_OVERFLOW,
                         "obs_camera output %zu exceeds the uint32 shader index",
                         total);
    if (repeat_count == 0) repeat_count = 1;

    @autoreleasepool {
        DispatchShape shape{};
        status = dispatch_shape(context->camera_pipeline, total, &shape, error);
        if (status != MCSIM_METAL_OK) return status;

        id<MTLBuffer> cells_buffer = new_shared_buffer(
            context, needed_cells, sizeof(uint16_t), cells,
            "mc-sim camera cells", error);
        id<MTLBuffer> sin_buffer = new_shared_buffer(
            context, kSinTableCount, sizeof(float), sin_table,
            "mc-sim camera sine table", error);
        id<MTLBuffer> region_buffer = new_shared_buffer(
            context, 1, sizeof(*region), region,
            "mc-sim camera region descriptor", error);
        id<MTLBuffer> camera_buffer = new_shared_buffer(
            context, 1, sizeof(*camera), camera,
            "mc-sim camera descriptor", error);
        id<MTLBuffer> poses_buffer = new_shared_buffer(
            context, pose_count, sizeof(*poses), poses,
            "mc-sim camera poses", error);
        id<MTLBuffer> ids_buffer = new_shared_buffer(
            context, shape.dispatched_threads, sizeof(uint16_t), nullptr,
            "mc-sim camera ids", error);
        id<MTLBuffer> depth_buffer = new_shared_buffer(
            context, shape.dispatched_threads, sizeof(uint8_t), nullptr,
            "mc-sim camera depth", error);
        id<MTLBuffer> edge_buffer = new_shared_buffer(
            context, shape.dispatched_threads, sizeof(uint8_t), nullptr,
            "mc-sim camera edge", error);
        if (!cells_buffer || !sin_buffer || !region_buffer || !camera_buffer ||
            !poses_buffer || !ids_buffer || !depth_buffer || !edge_buffer)
            return error ? error->status : MCSIM_METAL_ALLOC;

        uint16_t *device_ids = static_cast<uint16_t *>(ids_buffer.contents);
        uint8_t *device_depth = static_cast<uint8_t *>(depth_buffer.contents);
        uint8_t *device_edge = static_cast<uint8_t *>(edge_buffer.contents);

        for (unsigned repeat = 0; repeat < repeat_count; ++repeat) {
            std::fill(device_ids, device_ids + shape.dispatched_threads,
                      kIdSentinel);
            std::fill(device_depth, device_depth + shape.dispatched_threads,
                      kDepthSentinel);
            std::fill(device_edge, device_edge + shape.dispatched_threads,
                      kEdgeSentinel);

            id<MTLCommandBuffer> command = [context->queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            if (!command || !encoder)
                return set_error(error, MCSIM_METAL_COMMAND,
                                 "failed to create obs_camera command encoder");
            [encoder setComputePipelineState:context->camera_pipeline];
            [encoder setBuffer:cells_buffer offset:0 atIndex:0];
            [encoder setBuffer:sin_buffer offset:0 atIndex:1];
            [encoder setBuffer:region_buffer offset:0 atIndex:2];
            [encoder setBuffer:camera_buffer offset:0 atIndex:3];
            [encoder setBuffer:poses_buffer offset:0 atIndex:4];
            [encoder setBuffer:ids_buffer offset:0 atIndex:5];
            [encoder setBuffer:depth_buffer offset:0 atIndex:6];
            [encoder setBuffer:edge_buffer offset:0 atIndex:7];
            [encoder dispatchThreadgroups:MTLSizeMake(shape.groups, 1, 1)
                      threadsPerThreadgroup:MTLSizeMake(shape.threads_per_group, 1, 1)];
            [encoder endEncoding];
            status = finish_command(command, "obs_camera", error);
            if (status != MCSIM_METAL_OK) return status;

            for (size_t i = total; i < shape.dispatched_threads; ++i) {
                if (device_ids[i] != kIdSentinel ||
                    device_depth[i] != kDepthSentinel ||
                    device_edge[i] != kEdgeSentinel)
                    return set_error(error, MCSIM_METAL_DETERMINISM,
                                     "obs_camera overdispatch wrote index %zu: "
                                     "id=%04x depth=%02x edge=%02x",
                                     i, static_cast<unsigned>(device_ids[i]),
                                     static_cast<unsigned>(device_depth[i]),
                                     static_cast<unsigned>(device_edge[i]));
            }

            if (repeat == 0) {
                std::memcpy(out_ids, device_ids, total * sizeof(uint16_t));
                std::memcpy(out_depth, device_depth, total * sizeof(uint8_t));
                std::memcpy(out_edge, device_edge, total * sizeof(uint8_t));
            } else {
                for (size_t i = 0; i < total; ++i) {
                    size_t pose = i / kOcPixels;
                    size_t pixel = i % kOcPixels;
                    size_t x = pixel % 64u;
                    size_t y = pixel / 64u;
                    if (device_ids[i] != out_ids[i])
                        return set_error(error, MCSIM_METAL_DETERMINISM,
                                         "obs_camera repeat %u first ID differs at "
                                         "pose=%zu pixel=%zu x=%zu y=%zu: "
                                         "run0=%04x repeat=%04x",
                                         repeat + 1, pose, pixel, x, y,
                                         static_cast<unsigned>(out_ids[i]),
                                         static_cast<unsigned>(device_ids[i]));
                    if (device_depth[i] != out_depth[i])
                        return set_error(error, MCSIM_METAL_DETERMINISM,
                                         "obs_camera repeat %u first depth differs at "
                                         "pose=%zu pixel=%zu x=%zu y=%zu: "
                                         "run0=%02x repeat=%02x",
                                         repeat + 1, pose, pixel, x, y,
                                         static_cast<unsigned>(out_depth[i]),
                                         static_cast<unsigned>(device_depth[i]));
                    if (device_edge[i] != out_edge[i])
                        return set_error(error, MCSIM_METAL_DETERMINISM,
                                         "obs_camera repeat %u first edge differs at "
                                         "pose=%zu pixel=%zu x=%zu y=%zu: "
                                         "run0=%02x repeat=%02x",
                                         repeat + 1, pose, pixel, x, y,
                                         static_cast<unsigned>(out_edge[i]),
                                         static_cast<unsigned>(device_edge[i]));
                }
            }
        }
    }
    return MCSIM_METAL_OK;
}

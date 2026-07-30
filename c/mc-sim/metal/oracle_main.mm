/* Command-line Metal oracle.  stdout deliberately matches the corresponding
 * CPU drivers so oracle/runner.py can report the first exact difference. */
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "mcsim_metal.h"
#include "../core/region_tensor.h"
#include "../core/obs_camera.h"

#define RG_X0 (-48)
#define RG_Y0 28
#define RG_Z0 (-48)
#define RG_NX 113
#define RG_NY 104
#define RG_NZ 113

static const McSimMetalOcPose kPoses[] = {
    {180.0f, 0.0f}, {0.0f, 0.0f}, {90.0f, 15.0f},
    {270.0f, -30.0f}, {45.0f, 60.0f}, {200.0f, -75.0f},
};

static constexpr size_t kPoseCount = sizeof(kPoses) / sizeof(kPoses[0]);
static constexpr size_t kOcPixels = OC_NPIX;

static int report_error(const char *operation, McSimMetalStatus status,
                        const McSimMetalError *error) {
    std::fprintf(stderr, "metal %s failed [%s]: %s\n", operation,
                 mcsim_metal_status_string(status),
                 error && error->message[0] ? error->message : "no detail");
    return 1;
}

static bool parse_u64(const char *text, uint64_t *value) {
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = static_cast<uint64_t>(parsed);
    return true;
}

static bool parse_size(const char *text, size_t *value) {
    if (text[0] == '-') return false;
    uint64_t parsed;
    if (!parse_u64(text, &parsed) ||
        parsed > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return false;
    *value = static_cast<size_t>(parsed);
    return true;
}

static int run_layout(McSimMetalContext *context) {
    McSimMetalError error{};
    McSimMetalLayoutProbe probe{};
    McSimMetalStatus status = mcsim_metal_layout_probe(context, &probe, &error);
    if (status != MCSIM_METAL_OK)
        return report_error("layout", status, &error);
    std::printf("metal_layout abi=%u smoke=%u region=%u camera=%u pose=%u\n",
                probe.abi_version, probe.smoke_params_size,
                probe.region_desc_size, probe.camera_desc_size,
                probe.pose_size);
    return 0;
}

static int run_smoke(McSimMetalContext *context, int argc, char **argv) {
    uint64_t seed = 12345;
    size_t count = 256;
    if (argc > 0 && !parse_u64(argv[0], &seed)) {
        std::fprintf(stderr, "invalid smoke seed: %s\n", argv[0]);
        return 2;
    }
    if (argc > 1 && !parse_size(argv[1], &count)) {
        std::fprintf(stderr, "invalid smoke count: %s\n", argv[1]);
        return 2;
    }
    if (argc > 2) {
        std::fprintf(stderr, "usage: mcsim_metal_oracle smoke [seed [count]]\n");
        return 2;
    }
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(uint64_t)) {
        std::fprintf(stderr,
                     "smoke count %zu exceeds the fixed uint32 Metal ABI\n",
                     count);
        return 2;
    }

    uint64_t *values = count
        ? static_cast<uint64_t *>(std::malloc(count * sizeof(uint64_t)))
        : nullptr;
    if (count && !values) {
        std::fprintf(stderr, "smoke host allocation failed for %zu values\n", count);
        return 1;
    }
    McSimMetalError error{};
    McSimMetalStatus status =
        mcsim_metal_smoke(context, seed, values, count, 3, &error);
    if (status != MCSIM_METAL_OK) {
        std::free(values);
        return report_error("smoke", status, &error);
    }
    for (size_t i = 0; i < count; ++i)
        std::printf("%016llx\n", static_cast<unsigned long long>(values[i]));
    std::free(values);
    return 0;
}

static int run_obs_camera(McSimMetalContext *context, int argc, char **argv) {
    static const int64_t kSeeds[] = {0, 3, 12345};
    if (argc > 1) {
        std::fprintf(stderr, "usage: mcsim_metal_oracle obs_camera [seed]\n");
        return 2;
    }
    int64_t requested_seed = 0;
    if (argc == 1) {
        char *end = nullptr;
        errno = 0;
        long long parsed = std::strtoll(argv[0], &end, 10);
        if (errno != 0 || end == argv[0] || *end != '\0') {
            std::fprintf(stderr, "invalid obs_camera seed: %s\n", argv[0]);
            return 2;
        }
        requested_seed = static_cast<int64_t>(parsed);
    }

    long cell_count_long = rt_count(RG_NX, RG_NY, RG_NZ);
    if (cell_count_long <= 0) {
        std::fprintf(stderr, "invalid obs_camera region cell count: %ld\n",
                     cell_count_long);
        return 1;
    }
    size_t cell_count = static_cast<size_t>(cell_count_long);
    size_t output_count = kPoseCount * kOcPixels;

    McSinTable *sin_table =
        static_cast<McSinTable *>(std::malloc(sizeof(McSinTable)));
    ChunkPrimer *primer =
        static_cast<ChunkPrimer *>(std::malloc(sizeof(ChunkPrimer)));
    CpScratch *scratch =
        static_cast<CpScratch *>(std::malloc(sizeof(CpScratch)));
    uint16_t *cells = static_cast<uint16_t *>(
        std::malloc(cell_count * sizeof(uint16_t)));
    uint16_t *ids = static_cast<uint16_t *>(
        std::malloc(output_count * sizeof(uint16_t)));
    uint8_t *depth = static_cast<uint8_t *>(std::malloc(output_count));
    uint8_t *edge = static_cast<uint8_t *>(std::malloc(output_count));
    if (!sin_table || !primer || !scratch || !cells || !ids || !depth || !edge) {
        std::fprintf(stderr, "obs_camera host allocation failed\n");
        std::free(edge); std::free(depth); std::free(ids); std::free(cells);
        std::free(scratch); std::free(primer); std::free(sin_table);
        return 1;
    }
    mc_sin_table_init(sin_table);

    McSimMetalOcRegionDesc region{
        RG_X0, RG_Y0, RG_Z0, RG_NX, RG_NY, RG_NZ
    };
    McSimMetalOcCameraDesc camera{
        8.5f, 80.0f, 8.5f, static_cast<uint32_t>(kPoseCount)
    };
    int result = 0;
    int seed_count = argc == 1 ? 1 : 3;
    for (int seed_index = 0; seed_index < seed_count; ++seed_index) {
        int64_t seed = argc == 1 ? requested_seed : kSeeds[seed_index];
        rt_fill(cells, static_cast<uint64_t>(seed),
                RG_X0, RG_Y0, RG_Z0, RG_NX, RG_NY, RG_NZ,
                primer, scratch, sin_table);

        McSimMetalError error{};
        McSimMetalStatus status = mcsim_metal_obs_camera(
            context, cells, cell_count, &region,
            sin_table->sin_table, MC_SIN_TABLE_LEN,
            &camera, kPoses, kPoseCount, ids, depth, edge, 3, &error);
        if (status != MCSIM_METAL_OK) {
            result = report_error("obs_camera", status, &error);
            break;
        }

        std::printf("obs_camera seed=%lld poses=%zu %dx%d\n",
                    static_cast<long long>(seed), kPoseCount, OC_W, OC_H);
        for (size_t pose = 0; pose < kPoseCount; ++pose) {
            size_t base = pose * kOcPixels;
            for (size_t pixel = 0; pixel < kOcPixels; ++pixel)
                std::printf("%04x\n", static_cast<unsigned>(ids[base + pixel]));
            for (size_t pixel = 0; pixel < kOcPixels; ++pixel)
                std::printf("%02x\n", static_cast<unsigned>(depth[base + pixel]));
            for (size_t pixel = 0; pixel < kOcPixels; ++pixel)
                std::printf("%d\n", static_cast<int>(edge[base + pixel]));
        }
    }

    std::free(edge); std::free(depth); std::free(ids); std::free(cells);
    std::free(scratch); std::free(primer); std::free(sin_table);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: mcsim_metal_oracle <layout|smoke|obs_camera> [args...]\n");
        return 2;
    }

    McSimMetalContext *context = nullptr;
    McSimMetalError error{};
    McSimMetalStatus status = mcsim_metal_create(&context, &error);
    if (status != MCSIM_METAL_OK)
        return report_error("create", status, &error);

    int result;
    if (std::strcmp(argv[1], "layout") == 0)
        result = argc == 2 ? run_layout(context) : 2;
    else if (std::strcmp(argv[1], "smoke") == 0)
        result = run_smoke(context, argc - 2, argv + 2);
    else if (std::strcmp(argv[1], "obs_camera") == 0)
        result = run_obs_camera(context, argc - 2, argv + 2);
    else {
        std::fprintf(stderr, "unsupported Metal oracle kernel: %s\n", argv[1]);
        result = 2;
    }

    mcsim_metal_destroy(context);
    return result;
}

#include "PlatformPch.h"
#include "ImprovedNoise.h"
#include "PerlinNoise.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const int64_t SEED = INT64_C(0x12345678ABCDEF01);

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

uint64_t bits(double value)
{
    uint64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t bits(float value)
{
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void exact(const std::vector<double>& a, const std::vector<double>& b, const std::string& label)
{
    require(a.size() == b.size(), label + ": size mismatch");
    for (std::size_t i = 0; i < a.size(); ++i) {
        require(std::isfinite(a[i]) && std::isfinite(b[i]), label + ": non-finite sample");
        require(bits(a[i]) == bits(b[i]), label + ": bit mismatch at " + std::to_string(i));
    }
}

// Test-only ownership: the original shallow-copy arrayWithLength deliberately
// has no destructor. Production algorithm/array bodies remain byte-identical.
std::vector<double> region(PerlinNoise& noise, int x, int y, int z,
                           int nx, int ny, int nz, double sx, double sy, double sz)
{
    doubleArray result = noise.getRegion(doubleArray(), x, y, z, nx, ny, nz, sx, sy, sz);
    require(result.length == static_cast<unsigned>(nx * ny * nz), "region length");
    std::vector<double> values(result.data, result.data + result.length);
    delete[] result.data;
    return values;
}

std::size_t index(int x, int y, int z, int ny, int nz)
{
    return static_cast<std::size_t>((x * nz + z) * ny + y);
}

void test_platform()
{
#if !defined(__aarch64__)
#error "These tests must execute as an ARM64 native binary"
#endif
    static_assert(sizeof(void*) == 8, "64-bit native pointers required");
    LARGE_INTEGER before, after;
    require(QueryPerformanceCounter(&before) == 1, "counter return");
    require(QueryPerformanceCounter(&after) == 1 && after >= before, "monotonic counter");
    MemSect(2);
    MemSect(0);
    Random random;
    require(random.nextDouble() >= 0.0, "default Random constructor");
    ImprovedNoise improved;
    PerlinNoise perlin(4);
    require(std::isfinite(improved.getValue(-1.25, 3.5)), "default ImprovedNoise constructor");
    require(std::isfinite(perlin.getValue(-2.5, 0.25, 1.75)), "default PerlinNoise constructor");
    std::cout << "aarch64 native; int=32, int64_t=64; chrono counter and no-op MemSect OK\n";
}

void test_random()
{
    // Fixed external oracle: OpenJDK java.util.Random with these exact 64-bit
    // seeds. No replacement PRNG is implemented in the tests or library.
    const int64_t seeds[] = {0, SEED, -INT64_C(0x123456789ABCDEF)};
    const int expected[][10] = {
        {-1155484576, -723955400, 1033096058, -1690734402, -1557280266,
         1327362106, -1930858313, 502539523, -1728529858, -938301587},
        {1516430616, -1731436136, 1047038488, -1254666235, -344146775,
         -1812541573, 689937147, 61231955, -401691504, -553319239},
        {332025833, 297328329, -1761215869, 1135306804, 1751327408,
         642407207, 568003551, 342329939, 6020217, 1357167335}
    };
    const char* doubles[][6] = {
        {"0x1.764168ea6ca89p-1", "0x1.ec9e5b3672e14p-3", "0x1.465b93a78ef81p-1",
         "0x1.19d2e10efa128p-1", "0x1.31f174640953bp-1", "0x1.55373440b5f04p-2"},
        {"0x1.698b9498cc618p-2", "0x1.f344516a6eap-3", "0x1.d6f97949fb67bp-1",
         "0x1.48fcd6074ca68p-3", "0x1.d01d596f82825p-1", "0x1.d5007fba8bbe3p-1"},
        {"0x1.3ca4fc46e37bp-4", "0x1.2e0bf521d5b41p-1", "0x1.a18c8a264a5b2p-2",
         "0x1.0ed85e28cf148p-3", "0x1.6f7150e4baep-10", "0x1.b8b8af948c9dp-2"}
    };
    const uint32_t floats[][6] = {
        {0x3f3b20b4, 0x3f54d951, 0x3e764f2c, 0x3f1b3970, 0x3f232dc9, 0x3e9e3be0},
        {0x3eb4c5ca, 0x3f18cc61, 0x3e79a228, 0x3f353750, 0x3f6b7cbc, 0x3f13f6cf},
        {0x3d9e5278, 0x3d8dc6f0, 0x3f1705fa, 0x3e8756d0, 0x3ed0c644, 0x3e19296c}
    };
    const int bounds[] = {1, 2, 3, 16, 31, 1000, 1073741825, 2147483647};
    const int bounded[][8] = {
        {0, 1, 1, 9, 13, 53, 251269761, 1283218719},
        {0, 1, 2, 11, 19, 861, 344968573, 30615977},
        {0, 0, 1, 4, 8, 603, 284001775, 171164969}
    };
    const bool booleans[][12] = {
        {1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0},
        {0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0},
        {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1}
    };
    for (int s = 0; s < 3; ++s) {
        Random r(seeds[s]);
        for (int value : expected[s]) require(r.nextInt() == value, "Java nextInt vector");
        r.setSeed(seeds[s]);
        for (const char* value : doubles[s])
            require(bits(r.nextDouble()) == bits(std::strtod(value, nullptr)), "Java nextDouble vector");
        r.setSeed(seeds[s]);
        for (uint32_t value : floats[s]) require(bits(r.nextFloat()) == value, "Java nextFloat vector");
        r.setSeed(seeds[s]);
        for (int i = 0; i < 8; ++i) require(r.nextInt(bounds[i]) == bounded[s][i], "Java bounded/rejection vector");
        r.setSeed(seeds[s]);
        for (bool value : booleans[s]) require(r.nextBoolean() == value, "Java nextBoolean vector");
        // The upstream nextBytes consumes next(8) per byte, unlike JDK
        // nextBytes. Its expected top bytes come from the fixed nextInt oracle.
        r.setSeed(seeds[s]);
        byte bytes[10];
        r.nextBytes(bytes, 10);
        for (int i = 0; i < 10; ++i)
            require(bytes[i] == (static_cast<uint32_t>(expected[s][i]) >> 24), "original nextBytes consumption");
    }
    Random gaussian(0);
    require(std::abs(gaussian.nextGaussian() - 0.8025330637390305) < 2e-15, "Java Gaussian vector 1");
    require(std::abs(gaussian.nextGaussian() - -0.9015460884175122) < 2e-15, "Java Gaussian cached vector 2");
    gaussian.setSeed(0);
    require(std::abs(gaussian.nextGaussian() - 0.8025330637390305) < 2e-15, "Gaussian setSeed cache reset");
    gaussian.setSeed(SEED);
    Random fresh(SEED);
    require(bits(gaussian.nextGaussian()) == bits(fresh.nextGaussian()), "Gaussian stale cache after seed change");
    Random high(SEED), collision(SEED ^ (INT64_C(1) << 60));
    for (int i = 0; i < 50; ++i) require(high.nextInt() == collision.nextInt(), "original 48-bit seed equivalence");
    std::cout << "JDK fixed vectors passed (including >32-bit and negative seeds, bounded rejection); nextLong intentionally NOT called\n";
}

void test_lfloor()
{
    const double inputs[] = {0.0, -0.0, 1.0, -1.0, 0.25, -0.25, 1.999, -1.999,
                             -16777216.25, 16777216.75, -4294967296.5, 4294967296.5};
    const int64_t expected[] = {0, 0, 1, -1, 0, -1, 1, -2,
                                -16777217, 16777216, -INT64_C(4294967297), INT64_C(4294967296)};
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i)
        require(Mth::lfloor(inputs[i]) == expected[i], "original lfloor positive/negative vector");
}

void test_improved()
{
    Random a(SEED), b(SEED), c(SEED + 1);
    ImprovedNoise first(&a), same(&b), different(&c);
    bool changed = false;
    for (int i = -40; i <= 40; ++i) {
        const double x = i * 0.125, y = -11.5 + i * 0.25, z = -37.75 + i * 0.5;
        const double value = first.getValue(x, y, z);
        require(std::isfinite(value), "finite ImprovedNoise sample");
        require(bits(value) == bits(same.getValue(x, y, z)), "ImprovedNoise seed determinism");
        require(bits(first.getValue(x, y)) == bits(first.noise(x, y, 0)), "ImprovedNoise 2D overload");
        changed = changed || bits(value) != bits(different.getValue(x, y, z));
    }
    require(changed, "ImprovedNoise different seeds");
    const int nx = 5, ny = 4, nz = 7;
    std::vector<double> values(nx * ny * nz, 0.0);
    first.add(doubleArray(values.data(), static_cast<unsigned>(values.size())),
              -101.5, -10.25, -89.75, nx, ny, nz, 0.125, 1.0, 0.25, 1.0);
    for (int x = 0; x < nx; ++x)
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                require(bits(values[index(x, y, z, ny, nz)]) ==
                        bits(first.noise(-101.5 + x * 0.125, -10.25 + y, -89.75 + z * 0.25)),
                        "ImprovedNoise 3D add matches scalar noise (unit y step)");
}

void test_perlin()
{
    Random a(SEED), b(SEED), c(SEED + 1), d(SEED ^ (INT64_C(1) << 40));
    PerlinNoise first(&a, 16), same(&b, 16), different(&c, 16), highBits(&d, 16);
    const int origins[][3] = {{7, 0, 13}, {-31, -7, -19}, {-1, -1, -1},
                              {-33554433, -2, 33554431}};
    for (const auto& origin : origins) {
        const auto expected = region(first, origin[0], origin[1], origin[2], 5, 17, 5, 0.125, 0.25, 0.125);
        exact(expected, region(same, origin[0], origin[1], origin[2], 5, 17, 5, 0.125, 0.25, 0.125), "same 64-bit seed");
        exact(expected, region(first, origin[0], origin[1], origin[2], 5, 17, 5, 0.125, 0.25, 0.125), "repeated region");
        const auto other = region(different, origin[0], origin[1], origin[2], 5, 17, 5, 0.125, 0.25, 0.125);
        const auto high = region(highBits, origin[0], origin[1], origin[2], 5, 17, 5, 0.125, 0.25, 0.125);
        require(expected != other, "different 64-bit seeds must differ");
        require(expected != high, "seed bits above bit 32 must not be truncated");
    }
    const auto expected = region(first, -9, -3, 11, 5, 17, 5, 0.125, 0.25, 0.125);
    std::vector<double> reused(expected.size(), 12345.0);
    doubleArray view(reused.data(), static_cast<unsigned>(reused.size()));
    const doubleArray result = first.getRegion(view, -9, -3, 11, 5, 17, 5, 0.125, 0.25, 0.125);
    require(result.data == reused.data(), "getRegion reuses caller buffer");
    exact(expected, reused, "getRegion clears caller buffer");
    doubleArray plane = first.getRegion(doubleArray(), -17, -33, 17, 17, 0.0625, 0.0625, 0.5);
    std::vector<double> planar(plane.data, plane.data + plane.length);
    delete[] plane.data;
    exact(planar, region(first, -17, 10, -33, 17, 1, 17, 0.0625, 1.0, 0.0625), "original 2D region overload");
    // Original planar add ignores the y origin/scale when ySize == 1.
    exact(planar, region(first, -17, -77, -33, 17, 1, 17, 0.0625, 23.0, 0.0625), "original planar fast path");
    for (int i = -16; i <= 16; ++i) {
        const double x = i * 0.125;
        require(bits(first.getValue(x, -3.5)) == bits(same.getValue(x, -3.5)), "Perlin scalar 2D determinism");
        require(bits(first.getValue(x, -3.5, 7.25)) == bits(same.getValue(x, -3.5, 7.25)), "Perlin scalar 3D determinism");
    }
    std::cout << "16-octave regions: seed reproducibility, seed divergence, negative/wrapped coordinates, buffer reuse and 2D fast path OK\n";
}

void test_seams()
{
    Random random(SEED);
    PerlinNoise noise(&random, 16);
    std::size_t compared = 0;
    double maxDecimalError = 0;
    // Original coarse chunk lattice: 16 blocks / CHUNK_WIDTH(4) -> 4
    // intervals and 5 samples. This checks noise only, not terrain/chunks.
    for (int chunk = -2; chunk <= 1; ++chunk) {
        for (int planar = 0; planar <= 1; ++planar) {
            const int intervals = planar ? 16 : 4;
            const int n = intervals + 1, ny = planar ? 1 : 17;
            const int ox = chunk * intervals, oz = (chunk - 1) * intervals;
            const double scale = planar ? 0.0625 : 0.125;
            const auto left = region(noise, ox, -3, oz, n, ny, n, scale, 0.25, scale);
            const auto right = region(noise, ox + intervals, -3, oz, n, ny, n, scale, 0.25, scale);
            const auto nextZ = region(noise, ox, -3, oz + intervals, n, ny, n, scale, 0.25, scale);
            const auto combined = region(noise, ox, -3, oz, 2 * intervals + 1, ny, n, scale, 0.25, scale);
            for (int z = 0; z < n; ++z)
                for (int y = 0; y < ny; ++y) {
                    require(bits(left[index(intervals, y, z, ny, n)]) == bits(right[index(0, y, z, ny, n)]), "bit-exact X seam");
                    require(bits(left[index(z, y, intervals, ny, n)]) == bits(nextZ[index(z, y, 0, ny, n)]), "bit-exact Z seam");
                    compared += 2;
                }
            for (int x = 0; x < n; ++x)
                for (int z = 0; z < n; ++z)
                    for (int y = 0; y < ny; ++y) {
                        require(bits(left[index(x, y, z, ny, n)]) == bits(combined[index(x, y, z, ny, n)]), "left partition vs combined region");
                        require(bits(right[index(x, y, z, ny, n)]) == bits(combined[index(x + intervals, y, z, ny, n)]), "right partition vs combined region");
                    }
        }
        // The upstream terrain's non-binary scale may have harmless roundoff
        // from x*scale + local*scale vs (x+local)*scale; never change its math.
        const auto left = region(noise, chunk * 4, 0, chunk * 4, 5, 17, 5, 684.412, 684.412, 684.412);
        const auto right = region(noise, chunk * 4 + 4, 0, chunk * 4, 5, 17, 5, 684.412, 684.412, 684.412);
        for (int z = 0; z < 5; ++z)
            for (int y = 0; y < 17; ++y) {
                const double a = left[index(4, y, z, 17, 5)], b = right[index(0, y, z, 17, 5)];
                const double error = std::abs(a - b);
                maxDecimalError = std::max(maxDecimalError, error);
                require(error <= 2e-10 * (1 + std::max(std::abs(a), std::abs(b))), "decimal-scale X seam tolerance");
            }
    }
    std::cout << compared << " bit-exact X/Z overlap samples plus split/combined checks; decimal-scale max error="
              << std::setprecision(17) << maxDecimalError << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Specify platform/random/lfloor/improved/perlin/seams");
        const std::string test(argv[1]);
        if (test == "platform") test_platform();
        else if (test == "random") test_random();
        else if (test == "lfloor") test_lfloor();
        else if (test == "improved") test_improved();
        else if (test == "perlin") test_perlin();
        else if (test == "seams") test_seams();
        else throw std::runtime_error("Unknown test: " + test);
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

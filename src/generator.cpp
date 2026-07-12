// generator.cpp – C++23
//
// Generates the initial grid using the same xorshift RNG as the original
// code so results remain bit-for-bit reproducible.
// Output is now a flat std::vector instead of a char*** triple-pointer,
// eliminating N² separate heap allocations and freeing the caller from
// manual memory management.

#include "generator.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace generator
{

namespace
{

// ── RNG ─────────────────────────────────────────────────────────────────────
// Kept as a plain translation-unit static so the sequence matches the
// original exactly.  Thread safety is not required here: generation happens
// once, serially, before any parallel work begins.

static std::uint32_t g_seed = 0;

void init_r4uni(std::int32_t input_seed) noexcept
{
  g_seed = static_cast<std::uint32_t>(input_seed) + 987654321u;
}

float r4_uni() noexcept
{
  const std::uint32_t seed_in = g_seed;
  g_seed ^= (g_seed << 13);
  g_seed ^= (g_seed >> 17);
  g_seed ^= (g_seed << 5);
  // Two corrections versus a naive translation of the reference RNG, both
  // required for cross-architecture reproducibility (see the ARM build goal):
  //
  //  1. The combine step adds two 32-bit values that routinely exceed
  //     INT32_MAX.  It is done in UNSIGNED arithmetic (well-defined modular
  //     wraparound) and then reinterpreted as int32_t; doing the add in signed
  //     int would be undefined behaviour, which the optimiser may resolve
  //     differently across opt-levels / grid sizes / ISAs.
  //
  //  2. FP CONTRACTION IS DISABLED for this translation unit (see the
  //     set_source_files_properties(... -ffp-contract=off) entry in
  //     CMakeLists.txt).  With contraction enabled — the -O3/-march=native
  //     default on FMA-capable CPUs — `base + scale * x` fuses into a single
  //     fused-multiply-add with ONE rounding step, whereas without it the
  //     multiply and add round separately.  The two forms differ in the last
  //     bit for some inputs, which flips a few `r4_uni() < density` draws and
  //     silently perturbs the generated grid.  Because FMA availability differs
  //     by ISA (x86 vs ARM) and by flags, the separately-rounded form is pinned
  //     so the initial grid — and every downstream result — is identical on
  //     every platform.
  const std::uint32_t combined = seed_in + g_seed;
  return 0.5f + 0.2328306e-09f * static_cast<float>(static_cast<std::int32_t>(combined));
}

// Pick a species label in [1, N_SPECIES] from a uniform draw.
[[nodiscard]] unsigned char pick_species() noexcept
{
  auto species = static_cast<std::uint32_t>(r4_uni() * static_cast<float>(N_SPECIES)) + 1u;
  if (species > N_SPECIES)
    species = N_SPECIES;
  return static_cast<unsigned char>(species);
}

// ── Uniform fill ────────────────────────────────────────────────────────────
// Bit-for-bit identical to the original implementation: same x-outer/z-inner
// iteration order and same RNG call sequence.  Do not modify — the golden
// regression files depend on this exact sequence.
void fill_uniform(std::vector<unsigned char>& grid, const GridSpec& spec) noexcept
{
  const std::uint64_t N       = spec.grid_dimension;
  const float         density = spec.density;

  for (std::uint64_t x = 0; x < N; ++x)
    for (std::uint64_t y = 0; y < N; ++y)
      for (std::uint64_t z = 0; z < N; ++z)
      {
        if (r4_uni() < density)
        {
          const std::size_t flat_idx =
              (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N
              + static_cast<std::size_t>(x);
          grid[flat_idx] = pick_species();
        }
      }
}

// ── Gaussian fill ───────────────────────────────────────────────────────────
// Alive cells cluster toward the grid centre.  For each cell we compute its
// normalised radial distance from the centre and accept it with a Gaussian
// probability density × exp(−r² / 2σ²).  σ is a fixed fraction of the grid so
// the cluster scales with N.  Uses the same xorshift RNG for reproducibility.
//
// This produces a spikier alive distribution than uniform — denser core,
// sparser edges — which is the case the larger Gaussian scratch-reserve factor
// is designed to absorb.
void fill_gaussian(std::vector<unsigned char>& grid, const GridSpec& spec) noexcept
{
  const std::uint64_t N       = spec.grid_dimension;
  const float         density = spec.density;

  const float center  = 0.5f * static_cast<float>(N - 1);
  const float sigma   = 0.25f * static_cast<float>(N); // cluster radius ≈ N/4
  const float inv_2s2 = (sigma > 0.0f) ? 1.0f / (2.0f * sigma * sigma) : 0.0f;

  for (std::uint64_t x = 0; x < N; ++x)
    for (std::uint64_t y = 0; y < N; ++y)
      for (std::uint64_t z = 0; z < N; ++z)
      {
        const float dx = static_cast<float>(x) - center;
        const float dy = static_cast<float>(y) - center;
        const float dz = static_cast<float>(z) - center;
        const float r2 = dx * dx + dy * dy + dz * dz;
        const float p  = density * std::exp(-r2 * inv_2s2);

        if (r4_uni() < p)
        {
          const std::size_t flat_idx =
              (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N
              + static_cast<std::size_t>(x);
          grid[flat_idx] = pick_species();
        }
      }
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

std::vector<unsigned char> gen_initial_grid(const GridSpec& spec)
{
  // Flat layout: cell(x, y, z) = grid[z*N*N + y*N + x]
  // (Same z-major order used by the Simulation class so the copy-in is a
  // simple element-wise transfer without any axis permutation.)
  const auto        N = spec.grid_dimension;
  const std::size_t total =
      static_cast<std::size_t>(N) * static_cast<std::size_t>(N) * static_cast<std::size_t>(N);
  std::vector<unsigned char> grid(total, 0);

  init_r4uni(spec.seed);

  switch (spec.distribution)
  {
  case DistributionType::kGaussian:
    fill_gaussian(grid, spec);
    break;
  case DistributionType::kUniform:
  default:
    fill_uniform(grid, spec);
    break;
  }

  return grid;
}

} // namespace generator

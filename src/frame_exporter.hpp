// frame_exporter.hpp – C++23
//
// Single Responsibility: serialise one central z-slice per generation to a
// compact binary frame-log for offline visualisation.
//
// This is an OUTPUT side-channel only.  It never reads or writes simulation
// state and is invoked once per generation from Simulation::run(), entirely
// outside the per-cell hot path.  When no export path is supplied the object is
// never constructed (run() holds it in a std::optional), so a normal run pays
// nothing — not even a per-generation branch in the step kernels.
//
// Binary format (little-endian), consumed by scripts/visualize.py:
//
//   Header (16 bytes):
//     char     magic[4]      = "LIFE"
//     uint32_t version       = 1
//     uint32_t n             = N (logical cells per axis of the slice)
//     uint32_t num_frames    = generations + 1
//   Then num_frames records, each:
//     uint32_t generation
//     uint8_t  cells[N*N]    central z-slice, row-major: cells[y*N + x], 0..9
//
// The slice is taken at logical z = N/2 (ghost index gz = N/2 + 1).

#ifndef FRAME_EXPORTER_HPP
#define FRAME_EXPORTER_HPP

#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>

class FrameExporter
{
public:
  // Grid dimension N and frame count, bundled so the two same-typed counts
  // cannot be passed in the wrong order (matches the struct-parameter pattern
  // used elsewhere in the project for easily-swapped scalars).
  struct Spec
  {
    std::uint32_t grid_dimension; // N — logical cells per axis of the slice
    std::uint32_t num_frames;     // generations + 1
  };

  // Opens `path` for binary writing and emits the header immediately.
  // Check ok() afterwards; on failure the object becomes inert and every
  // write_slice() is a no-op.
  FrameExporter(std::string_view path, const Spec& spec);

  // Append one central z-slice from a ghost-padded grid buffer.
  // `grid` points to the full (N+2)³ buffer; stride_y/stride_z are its strides.
  void write_slice(std::uint32_t        generation,
                   const unsigned char* grid,
                   std::ptrdiff_t       stride_y,
                   std::ptrdiff_t       stride_z) noexcept;

  [[nodiscard]] bool ok() const noexcept
  {
    return ok_;
  }

  // Move-only: owns a file handle.
  FrameExporter(const FrameExporter&)            = delete;
  FrameExporter& operator=(const FrameExporter&) = delete;
  FrameExporter(FrameExporter&&)                 = default;
  FrameExporter& operator=(FrameExporter&&)      = default;
  ~FrameExporter()                               = default;

private:
  std::ofstream              out_;
  std::uint32_t              n_{0};
  bool                       ok_{false};
  std::vector<unsigned char> slice_; // reused scratch, N*N bytes
};

#endif // FRAME_EXPORTER_HPP

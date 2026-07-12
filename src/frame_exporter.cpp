// frame_exporter.cpp – C++23
//
// Implementation of FrameExporter.  See frame_exporter.hpp for the format.

#include "frame_exporter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

// Write a uint32 little-endian, independent of host byte order.
void write_u32(std::ofstream& out, std::uint32_t v) noexcept
{
  const std::array<char, 4> bytes{
      static_cast<char>(v & 0xFFU),
      static_cast<char>((v >> 8) & 0xFFU),
      static_cast<char>((v >> 16) & 0xFFU),
      static_cast<char>((v >> 24) & 0xFFU),
  };
  out.write(bytes.data(), bytes.size());
}

} // namespace

FrameExporter::FrameExporter(std::string_view path, const Spec& spec)
  : out_(std::string(path), std::ios::binary | std::ios::trunc)
  , n_(spec.grid_dimension)
{
  if (!out_)
    return;

  // Header: "LIFE", version, N, num_frames.
  out_.write("LIFE", 4);
  write_u32(out_, 1U);
  write_u32(out_, n_);
  write_u32(out_, spec.num_frames);

  slice_.resize(static_cast<std::size_t>(n_) * static_cast<std::size_t>(n_));
  ok_ = static_cast<bool>(out_);
}

void FrameExporter::write_slice(std::uint32_t        generation,
                                const unsigned char* grid,
                                std::ptrdiff_t       stride_y,
                                std::ptrdiff_t       stride_z) noexcept
{
  if (!ok_)
    return;

  const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(n_);

  // Central z-slice: logical z = N/2 → ghost index gz = N/2 + 1.
  const std::ptrdiff_t gz     = (N / 2) + 1;
  const std::ptrdiff_t z_base = gz * stride_z;

  for (std::ptrdiff_t y = 0; y < N; ++y)
  {
    // Logical (x,y) → ghost (x+1, y+1); row starts at the first interior x.
    const std::ptrdiff_t row = z_base + (y + 1) * stride_y + 1;
    for (std::ptrdiff_t x = 0; x < N; ++x)
      slice_[static_cast<std::size_t>(y * N + x)] = grid[static_cast<std::size_t>(row + x)];
  }

  write_u32(out_, generation);
  out_.write(reinterpret_cast<const char*>(slice_.data()),
             static_cast<std::streamsize>(slice_.size()));

  if (!out_)
    ok_ = false;
}

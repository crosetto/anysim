//
// Created by egi on 6/2/19.
//

#ifdef GPU_BUILD
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

#include "core/config/configuration_node.h"
#include "core/gpu/fdtd_gpu_interface.h"
#include "core/gpu/coloring.cuh"
#include "core/cpu/sources_holder.h"
#include "core/cpu/thread_pool.h"
#include "core/solver/solver.h"
#include "core/common/curl.h"
#include "cpp/common_funcs.h"
#include "core/grid/grid.h"

#include <iostream>
#include <chrono>
#include <memory>
#include <cmath>

constexpr double C0 = 299792458; /// Speed of light [metres per second]

#ifndef ANYSIM_FDTD_2D_H
#define ANYSIM_FDTD_2D_H

enum class boundary_condition
{
  dirichlet, periodic
};

template <class float_type>
class region_initializer
{
public:
  region_initializer () = delete;
  region_initializer (unsigned int nx_arg, unsigned int ny_arg)
    : nx (nx_arg)
    , ny (ny_arg)
  { }

  virtual void fill_region (float_type *er, float_type *hr) const = 0;

protected:
  const unsigned int nx, ny;
};

template <class float_type>
class rectangular_region_initializer : public region_initializer<float_type>
{
public:
  rectangular_region_initializer () = delete;
  rectangular_region_initializer (
    unsigned int nx,
    unsigned int ny,
    unsigned int region_i_start_arg,
    unsigned int region_j_start_arg,
    unsigned int width_arg,
    unsigned int height_arg,
    float_type er_value_arg,
    float_type hr_value_arg)
    : region_initializer<float_type> (nx, ny)
    , region_i_start (region_i_start_arg)
    , region_j_start (region_j_start_arg)
    , width (width_arg)
    , height (height_arg)
    , er_value (er_value_arg)
    , hr_value (hr_value_arg)
  { }

  void fill_region (float_type *er, float_type *hr) const final
  {
    using ri = region_initializer<float_type>;

    for (unsigned int j = region_j_start; j < region_j_start + height; j++)
      {
        for (unsigned int i = region_i_start; i < region_i_start + width; i++)
          {
            er[j * ri::nx + i] = er_value;
            hr[j * ri::nx + i] = hr_value;
          }
      }
  }

protected:
  const unsigned int region_i_start, region_j_start, width, height;
  const float_type er_value, hr_value;
};

template <class float_type>
class fdtd_2d : public solver
{
  boundary_condition left_bc, bottom_bc, right_bc, top_bc;

  unsigned int nx = 0;
  unsigned int ny = 0;

  float_type dx = 0.1;
  float_type dy = 0.1;
  float_type dt = 0.1;
  float_type t = 0.0;

  float_type *m_e = nullptr;
  float_type *m_h = nullptr;
  float_type *dz = nullptr;
  float_type *ez = nullptr;
  float_type *hx = nullptr;
  float_type *hy = nullptr;
  float_type *er = nullptr;
  float_type *hr = nullptr; /// Materials properties (for now assume mu_xx = mu_yy)

  float_type *d_mh = nullptr;
  float_type *d_er = nullptr;
  float_type *d_ez = nullptr;
  float_type *d_dz = nullptr;
  float_type *d_hx = nullptr;
  float_type *d_hy = nullptr;

  unsigned int sources_count = 0;
  float_type *d_sources_frequencies = nullptr;
  unsigned int *d_sources_offsets = nullptr;


  std::unique_ptr<sources_holder<float_type>> sources;

public:
  fdtd_2d () = delete;
  fdtd_2d (
    thread_pool &threads_arg,
    workspace &solver_workspace_arg)
  : solver (threads_arg, solver_workspace_arg)
  , left_bc (boundary_condition::periodic)
  , bottom_bc (boundary_condition::periodic)
  , right_bc (boundary_condition::periodic)
  , top_bc (boundary_condition::periodic)
  { }

  void fill_configuration_scheme (configuration_node &configuration_scheme) final
  {
    configuration_scheme.append_node ("cfl", 0.5);

    configuration_node source_scheme ("source_scheme");
    source_scheme.append_node ("frequency", 1E+8);
    source_scheme.append_node ("x", 0.5);
    source_scheme.append_node ("y", 0.5);

    configuration_scheme.append_array ("sources", source_scheme);
  }

  void apply_configuration (const configuration_node &config, grid &solver_grid) final
  {
    dx = solver_grid.dx;
    dy = solver_grid.dy;

    const float_type cfl = std::get<double> (config.child (0).value);
    dt = cfl * std::min (dx, dy) / C0;
    t = 0.0;

    nx = solver_grid.nx;
    ny = solver_grid.ny;

    sources = std::make_unique<sources_holder<float_type>> ();
    for (auto &source: config.child (1).group ())
    {
      const double frequency = std::get<double> (source.child (0).value);
      const double x = std::get<double> (source.child (1).value);
      const double y = std::get<double> (source.child (2).value);

      const unsigned int grid_x = std::ceil (x / dx);
      const unsigned int grid_y = std::ceil (y / dy);

      sources->append_source (frequency, nx * grid_y + grid_x);
    }

    solver_grid.create_field<float_type> ("me", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("mh", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("dz", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("ez", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("hx", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("hy", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("er", memory_holder_type::host, 1);
    solver_grid.create_field<float_type> ("hr", memory_holder_type::host, 1);

    m_e = reinterpret_cast<float_type *> (solver_workspace.get ("me"));
    m_h = reinterpret_cast<float_type *> (solver_workspace.get ("mh"));
    dz  = reinterpret_cast<float_type *> (solver_workspace.get ("dz"));
    ez  = reinterpret_cast<float_type *> (solver_workspace.get ("ez"));
    hx  = reinterpret_cast<float_type *> (solver_workspace.get ("hx"));
    hy  = reinterpret_cast<float_type *> (solver_workspace.get ("hy"));
    er  = reinterpret_cast<float_type *> (solver_workspace.get ("er"));
    hr  = reinterpret_cast<float_type *> (solver_workspace.get ("hr"));

    std::fill_n (er, nx * ny, 1.0);
    std::fill_n (hr, nx * ny, 1.0);

    std::fill_n (hx, nx * ny, 0.0);
    std::fill_n (hy, nx * ny, 0.0);
    std::fill_n (ez, nx * ny, 0.0);
    std::fill_n (dz, nx * ny, 0.0);

    for (unsigned int i = 0; i < nx * ny; i++)
      m_e[i] = C0 * dt / er[i];
    for (unsigned int i = 0; i < nx * ny; i++)
      m_h[i] = C0 * dt / hr[i];

    // TODO Apply initializers
  }

  void update_h (unsigned int thread_id, unsigned int total_threads)
  {
    auto yr = work_range::split (ny, thread_id, total_threads);

    for (unsigned int j = yr.chunk_begin; j < yr.chunk_end; j++)
    {
      for (unsigned int i = 0; i < nx; i++)
      {
        const float_type cex = update_curl_ex (i, j, nx, ny, dy, ez);
        const float_type cey = update_curl_ey (i, j, nx, dx, ez);

        const unsigned int idx = j * nx + i;

        // update_h
        hx[idx] -= m_h[idx] * cex;
        hy[idx] -= m_h[idx] * cey;
      }
    }
  }

  void update_e (unsigned int thread_id, unsigned int total_threads, const sources_holder<float_type> &s)
  {
    auto yr = work_range::split (ny, thread_id, total_threads);
    const float_type C0_p_dt = C0 * dt;

    auto sources_offsets = s.get_sources_offsets ();
    auto sources_frequencies = s.get_sources_frequencies ();

    for (unsigned int j = yr.chunk_begin; j < yr.chunk_end; j++)
    {
      for (unsigned int i = 0; i < nx; i++)
      {
        const unsigned int idx = j * nx + i;
        const float_type chz = update_curl_h (i, j, nx, ny, dx, dy, hx, hy);

        dz[idx] += C0_p_dt * chz; // update d = C0 * dt * curl Hz

        for (unsigned int source_id = 0; source_id < s.get_sources_count (); source_id++)
          if (sources_offsets[source_id] == idx)
            dz[idx] += calculate_source (t, sources_frequencies[source_id]);

        ez[idx] = dz[idx] / er[idx]; // update e
      }
    }
  }

#ifdef GPU_BUILD
  void preprocess_gpu (int gpu_num, const sources_holder<float_type> &s)
  {
    cudaSetDevice (gpu_num);

    sources_count = s.get_sources_count ();

    cudaMalloc (&d_mh, nx * ny * sizeof (float_type));
    cudaMalloc (&d_er, nx * ny * sizeof (float_type));
    cudaMalloc (&d_ez, nx * ny * sizeof (float_type));
    cudaMalloc (&d_dz, nx * ny * sizeof (float_type));
    cudaMalloc (&d_hx, nx * ny * sizeof (float_type));
    cudaMalloc (&d_hy, nx * ny * sizeof (float_type));
    cudaMalloc (&d_sources_frequencies, sources_count * sizeof (float_type));
    cudaMalloc (&d_sources_offsets, sources_count * sizeof (unsigned int));

    cudaMemcpy (d_mh, m_h, nx * ny * sizeof (float_type), cudaMemcpyHostToDevice);
    cudaMemcpy (d_er, er,  nx * ny * sizeof (float_type), cudaMemcpyHostToDevice);

    cudaMemcpy (d_sources_frequencies, s.get_sources_frequencies (), sources_count * sizeof (float_type), cudaMemcpyHostToDevice);
    cudaMemcpy (d_sources_offsets, s.get_sources_offsets (), sources_count * sizeof (unsigned int), cudaMemcpyHostToDevice);

    cudaMemset (d_ez, 0, nx * ny * sizeof (float_type));
    cudaMemset (d_hx, 0, nx * ny * sizeof (float_type));
    cudaMemset (d_hy, 0, nx * ny * sizeof (float_type));
  }

  void calculate_gpu (unsigned int steps)
  {
    for (unsigned int step = 0; step < steps; t += dt, step++)
      {
        const auto begin = std::chrono::high_resolution_clock::now();
        std::cout << "step: " << step << "; t: " << t << " ";

        fdtd_step (t, dt, nx, ny, dx, dy, d_mh, d_er, d_ez, d_dz, d_hx, d_hy, sources_count, d_sources_frequencies, d_sources_offsets);

        auto cuda_error = cudaGetLastError ();
        if (cuda_error != cudaSuccess)
          {
            std::cout << "Error on GPU: " << cudaGetErrorString (cuda_error) << std::endl;
            return;
          }

        const auto end = std::chrono::high_resolution_clock::now ();
        const std::chrono::duration<double> duration = end - begin;
        std::cout << "in " << duration.count () << "s\n";

      }

    // cudaMemcpy (ez.get (), d_ez, nx * ny * sizeof (float_type), cudaMemcpyDeviceToHost);
  }

  void postprocess_gpu ()
  {
    cudaFree (d_mh);
    cudaFree (d_er);
    cudaFree (d_ez);
    cudaFree (d_dz);
    cudaFree (d_hx);
    cudaFree (d_hy);
  }
#endif

  /// Ez mode
  void solve (unsigned int step, unsigned int thread_id, unsigned int total_threads) final
  {
    const auto begin = std::chrono::high_resolution_clock::now ();

    threads.barrier ();
    if (is_main_thread (thread_id))
      {
        std::cout << "Step: " << step << "; t: " << t << " ";
        t += dt;
      }

    update_h (thread_id, total_threads);
    threads.barrier ();
    update_e (thread_id, total_threads, *sources);

    const auto end = std::chrono::high_resolution_clock::now ();
    const std::chrono::duration<double> duration = end - begin;

    if (is_main_thread (thread_id))
      std::cout << "in " << duration.count () << "s\n";
  }
};


#endif //ANYSIM_FDTD_2D_H

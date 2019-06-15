//
// Created by egi on 2/18/18.
//

#ifndef BENCHMARK_EULER_AOS_H
#define BENCHMARK_EULER_AOS_H

#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>

#include "core/cpu/thread_pool.h"
#include "io/vtk/vtk.h"

template<class float_type>
class euler_2d
{
  thread_pool &threads;
  constexpr static int LEFT = 0;
  constexpr static int BOTTOM = 1;
  constexpr static int RIGHT = 2;
  constexpr static int TOP = 3;

  const unsigned int nx;
  const unsigned int ny;

  const float_type cfl = 0.1;
  float_type gamma = 1.4;
  float_type dx, dy;

  float_type edge_lengths[4];
  float_type normals_x[4];
  float_type normals_y[4];

  std::unique_ptr<float_type[]> rho_1;
  std::unique_ptr<float_type[]> rho_2;
  std::unique_ptr<float_type[]> u_1;
  std::unique_ptr<float_type[]> u_2;
  std::unique_ptr<float_type[]> v_1;
  std::unique_ptr<float_type[]> v_2;
  std::unique_ptr<float_type[]> p_1;
  std::unique_ptr<float_type[]> p_2;
public:
  euler_2d (thread_pool &thread_pool, unsigned int nx_arg, unsigned int ny_arg)
  : threads (thread_pool)
  , nx (nx_arg)
  , ny (ny_arg)
  , rho_1 (new float_type[nx * ny])
  , rho_2 (new float_type[nx * ny])
  , u_1   (new float_type[nx * ny])
  , u_2   (new float_type[nx * ny])
  , v_1   (new float_type[nx * ny])
  , v_2   (new float_type[nx * ny])
  , p_1   (new float_type[nx * ny])
  , p_2   (new float_type[nx * ny])
  {
    const float_type width = 7.0;
    const float_type height = 3.0;

    dx = width / nx;
    dy = height / ny;

    edge_lengths[LEFT] = edge_lengths[RIGHT] = dx;
    edge_lengths[BOTTOM] = edge_lengths[TOP] = dy;
    normals_x[LEFT] = -1.0f;  normals_y[LEFT] = 0.0f;
    normals_x[BOTTOM] = 0.0f; normals_y[BOTTOM] = -1.0f;
    normals_x[RIGHT] = 1.0f;  normals_y[RIGHT] = 0.0f;
    normals_x[TOP] = 0.0f;    normals_y[TOP] = 1.0f;


    // TODO Extract initialization
#if 0
    const float_type circle_x = 0.5;
    const float_type circle_y = 0.5;
    const float_type circle_rad = 0.1;
#endif

    ///
    const float_type x_0 = 1.0;
    const float_type y_0 = 1.5;

    for (unsigned int y = 0; y < ny; ++y)
    {
      for (unsigned int x = 0; x < nx; ++x)
      {
        auto i = y * nx + x;

        const float_type lbx = x * dx;
        const float_type lby = y * dy;

        if (lbx < x_0)
        {
          rho_1[i] = 1.0;
          p_1[i] = 1.0;
          v_1[i] = 0.0;
          u_1[i] = 0.0;
        }
        else
        {
          if (lby < y_0)
          {
            rho_1[i] = 1.0;
            p_1[i] = 0.1;
            v_1[i] = 0.0;
            u_1[i] = 0.0;
          }
          else
          {
            rho_1[i] = 0.125;
            p_1[i] = 0.1;
            v_1[i] = 0.0;
            u_1[i] = 0.0;
          }
        }

#if 0
        rho_1[i] = 1.0;
        p_1[i]   = 1.0;
        u_1[i]   = 0.0;
        v_1[i]   = 0.0;

        if ((lbx - circle_x) * (lbx - circle_x) + (lby - circle_y) * (lby - circle_y) <= circle_rad * circle_rad)
        {
          rho_1[i] = 1.125;
          p_1[i]   = 0.2;
        }
#else

#endif
      }
    }
  }

  float_type calculate_dt (
      unsigned int thread_id,
      unsigned int total_threads,
      const float_type *p_rho,
      const float_type *p_u,
      const float_type *p_v,
      const float_type *p_p) const
  {
    float_type min_len = std::min (dx, dy);
    float_type max_speed = 0.0;

    auto yr = work_range::split (ny, thread_id, total_threads);

    for (unsigned int y = yr.chunk_begin; y < yr.chunk_end; y++)
    {
      for (unsigned int x = 0; x < nx; x++)
      {
        auto i = y * nx + x;

        const float_type rho = p_rho[i];
        const float_type p = p_p[i];
        const float_type a = speed_of_sound_in_gas (p, rho);
        const float_type u = p_u[i];
        const float_type v = p_v[i];

        max_speed = std::max (max_speed, std::max (std::fabs (u + a), std::fabs (u - a)));
        max_speed = std::max (max_speed, std::max (std::fabs (v + a), std::fabs (v - a)));
      }
    }

    float_type new_dt = cfl * min_len / max_speed;
    threads.reduce_min (thread_id, new_dt);
    return new_dt;
  }

  static float_type calculate_total_energy (float_type p, float_type u, float_type v, float_type rho, float_type gamma)
  {
    return p / ((gamma - 1) * rho) + (u*u + v*v) / 2.0;
  }

  static float_type max_speed (float_type v_c, float_type v_n, float_type u_c, float_type u_n)
  {
    const float_type zero = 0.0;
    const float_type splus  = std::max(zero, std::max(u_c + v_c, u_n + v_n));
    const float_type sminus = std::min(zero, std::min(u_c - v_c, u_n - v_n));
    return std::max (splus, -sminus);
  }

  unsigned int get_neighbor_index (unsigned int x, unsigned int y, unsigned int f) const
  {
    if (f == LEFT)
      {
        if (x == 0) // BC
          return y * nx + x;
        return y * nx + x - 1;
      }
    else if (f == BOTTOM)
      {
        if (y == 0) // BC
          return y * nx + x;
        return (y - 1) * nx + x;
      }
    else if (f == RIGHT)
      {
        if (x == nx - 1) // BC
          return y * nx + x;
        return y * nx + x + 1;
      }
    else if (f == TOP)
      {
        if (y == ny - 1) // BC
          return y * nx + x;
        return (y + 1) * nx + x;
      }

    return 0; // TODO Assert
  }

  float_type speed_of_sound_in_gas (float_type p, float_type rho) const
  {
    return std::sqrt (gamma * p / rho);
  }

  /**
   * Rusanov scheme calculation of numerical flux (Thierry Gallouet, Jean-Marc Herard, Nicolas Seguin,
   * Some recent Finite Volume schemes to compute Euler equations using real gas EOS, 2002):
   *
   *  \f$
   *   \phi\left(W_{L}, W_{R}\right) = \frac{F\left(W_{L}\right) + F\left(W_{R}\right)}{2} - \frac{1}{2} \lambda^{max}_{i+1/2}
   *   \lambda^{max}_{i+1/2} = max\left(\left|u_{L}\right| + c_{L}, \left|u_{R}\right| + c_{R}\right)
   *  \f$
   *
   * The idea behind this flux, insstead of approximating the exact Riemann solver, is to recall
   * that the entropy solution is the limit of viscous solution and to take a centred flux to
   * which some viscosity (with the right sight) is added (Eric Sonnendrucker,
   * Numerical methods for hyperbolic systems - lecture notes, 2013).
   *
   * @param F_sigma Rusanov flux output
   */
  void rusanov_scheme (
      const float_type pc, const float_type pn,
      const float_type rhoc, const float_type rhon,
      const float_type U_c, const float_type U_n,
      const float_type *F_c, const float_type *F_n,
      const float_type *Q_n, const float_type *Q_c,
      float_type *F_sigma)
  {
    for (int c = 0; c < 4; c++)
    {
      const float_type central_difference = (F_c[c] + F_n[c]) / 2;

      const float_type ss_c = speed_of_sound_in_gas (pc, rhoc);
      const float_type ss_n = speed_of_sound_in_gas (pn, rhon);

      const float_type sp = max_speed (ss_c, ss_n, U_c, U_n);
      const float_type viscosity = sp * (Q_n[c] - Q_c[c]) / 2;

      F_sigma[c] = central_difference - viscosity;
    }
  }

  void fill_state_vector (
      unsigned int i,
      const float_type *p_rho,
      const float_type *p_u,
      const float_type *p_v,
      const float_type *p_p,
      float_type *q)
  {
    const float_type rhoc = p_rho[i];
    const float_type uc   = p_u[i];
    const float_type vc   = p_v[i];
    const float_type pc   = p_p[i];

    q[0] = rhoc;
    q[1] = rhoc * uc;
    q[2] = rhoc * vc;
    q[3] = rhoc * calculate_total_energy (pc, uc, vc, rhoc, gamma);
  }

  static void fill_flux_vector (
      const float_type *Q_c,
      float_type *F_c)
  {
    F_c[0] = Q_c[0] * Q_c[1];
    F_c[1] = Q_c[0] * Q_c[1] * Q_c[1] + Q_c[3];
    F_c[2] = Q_c[0] * Q_c[1] * Q_c[2];
    F_c[3] = Q_c[0] * Q_c[1] * calculate_total_energy (Q_c[3], Q_c[1], Q_c[2], Q_c[0], 1.4) + Q_c[1] * Q_c[3];
  }

  void rotate_vector_to_edge_coordinates (
      const unsigned int edge,
      const float_type *v,
      float_type *V) const
  {
    const float_type normal_x = normals_x[edge];
    const float_type normal_y = normals_y[edge];

    V[0] =  v[0];
    V[1] =  v[1] * normal_x + v[2] * normal_y;
    V[2] = -v[1] * normal_y + v[2] * normal_x;
    V[3] =  v[3];
  }

  void rotate_vector_from_edge_coordinates (
      const unsigned int edge,
      const float_type *V,
      float_type *v) const
  {
    const float_type normal_x = normals_x[edge];
    const float_type normal_y = normals_y[edge];

    v[0] = V[0];
    v[1] = V[1] * normal_x - V[2] * normal_y;
    v[2] = V[1] * normal_y + V[2] * normal_x;
    v[3] = V[3];
  }

  /**
   * Calculate pressure for ideal gas (Majid Ahmadi, Wahid S. Ghaly, A Finite Volume for the
   * two-dimensional euler equations with solution adaptation on unstructured meshes)
   */
  float_type calculate_p (float_type E, float_type u, float_type v, float_type rho) const
  {
    return (E - (u * u + v * v) / 2) * (gamma - 1) * rho;
  }

  void calculate (unsigned int steps)
  {
    const float_type cell_area = dx * dy;

    threads.execute ([&] (unsigned int thread_id, unsigned int total_threads)
    {
      for (unsigned int step = 0; step < steps; step++)
      {
        const auto begin = std::chrono::high_resolution_clock::now();
        float_type *p_rho = step % 2 == 0 ? rho_1.get () : rho_2.get ();
        float_type *p_u   = step % 2 == 0 ? u_1.get ()   : u_2.get ();
        float_type *p_v   = step % 2 == 0 ? v_1.get ()   : v_2.get ();
        float_type *p_p   = step % 2 == 0 ? p_1.get ()   : p_2.get ();

        float_type *p_rho_next = (step + 1) % 2 == 0 ? rho_1.get () : rho_2.get ();
        float_type *p_u_next   = (step + 1) % 2 == 0 ? u_1.get ()   : u_2.get ();
        float_type *p_v_next   = (step + 1) % 2 == 0 ? v_1.get ()   : v_2.get ();
        float_type *p_p_next   = (step + 1) % 2 == 0 ? p_1.get ()   : p_2.get ();

        const float_type dt = calculate_dt (thread_id, total_threads, p_rho, p_u, p_v, p_p);

        float_type q_c[4];
        float_type q_n[4];

        float_type Q_c[4];
        float_type Q_n[4];

        float_type F_c[4];
        float_type F_n[4];

        float_type F_sigma[4];  /// Edge flux in local coordinate system
        float_type f_sigma[4];  /// Edge flux in global coordinate system

        auto yr = work_range::split (ny, thread_id, total_threads);

        for (unsigned int y = yr.chunk_begin; y < yr.chunk_end; ++y)
        {
          for (unsigned int x = 0; x < nx; ++x)
          {
            auto i = y * nx + x;

            fill_state_vector (i, p_rho, p_u, p_v, p_p, q_c);

            float_type flux[4] = {0.0, 0.0, 0.0, 0.0};

            /// Edge flux
            for (int edge = 0; edge < 4; edge++)
            {
              const unsigned int j = get_neighbor_index (x, y, edge);

              fill_state_vector (j, p_rho, p_u, p_v, p_p, q_n);
              rotate_vector_to_edge_coordinates (edge, q_c, Q_c);
              rotate_vector_to_edge_coordinates (edge, q_n, Q_n);
              fill_flux_vector (Q_c, F_c);
              fill_flux_vector (Q_n, F_n);

              const float_type U_c = Q_c[1] / Q_c[0];
              const float_type U_n = Q_n[1] / Q_n[0];

              rusanov_scheme (p_p[i], p_p[j], p_rho[i], p_rho[j], U_c, U_n, F_c, F_n, Q_n, Q_c, F_sigma);

              rotate_vector_from_edge_coordinates (edge, F_sigma, f_sigma);

              for (int c = 0; c < 4; c++)
                flux[c] += edge_lengths[edge] * f_sigma[c];
            }

            float_type new_q[4];
            for (int c = 0; c < 4; c++)
              new_q[c] = q_c[c] - (dt / cell_area) * (flux[c]);

            const float_type rho = new_q[0];
            const float_type u   = new_q[1] / rho;
            const float_type v   = new_q[2] / rho;
            const float_type E   = new_q[3] / rho;

            p_rho_next[i] = rho;
            p_u_next[i] = u;
            p_v_next[i] = v;
            p_p_next[i] = calculate_p (E, u, v, rho);
          }
        }

        threads.barrier ();

        const auto end = std::chrono::high_resolution_clock::now ();
        const std::chrono::duration<double> duration = end - begin;

        if (is_main_thread (thread_id))
        {
          std::cout << "Step completed in " << duration.count () << "s\n";

          if (step % 300 == 0)
            write_vtk ("output_" + std::to_string (step) + ".vtk", dx, dy, nx, ny, p_rho);
        }
      }
    });
  }
};

#endif //BENCHMARK_MESH_AOS_H
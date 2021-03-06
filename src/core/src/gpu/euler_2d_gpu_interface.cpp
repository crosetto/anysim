//
// Created by egi on 7/2/19.
//

#include "core/gpu/euler_2d.cuh"
#include "core/gpu/euler_2d_interface.h"
#include "cpp/common_funcs.h"

template <class float_type>
float_type euler_2d_calculate_dt_gpu_interface (
    float_type gamma,
    float_type cfl,

    const grid_topology &topology,
    const grid_geometry &geometry,

    float_type *workspace,
    const float_type *p_rho,
    const float_type *p_u,
    const float_type *p_v,
    const float_type *p_p)
{
#ifdef GPU_BUILD
  return euler_2d_calculate_dt_gpu (gamma, cfl, topology, geometry, workspace, p_rho, p_u, p_v, p_p);
#else
  cpp_unreferenced (gamma, cfl, topology, geometry, workspace, p_rho, p_u, p_v, p_p);
  return {};
#endif
}

template <class float_type>
void euler_2d_calculate_next_time_step_gpu_interface (
    float_type dt,
    float_type gamma,

    const grid_topology &topology,
    const grid_geometry &geometry,

    const float_type *p_rho,
    float_type *p_rho_next,
    const float_type *p_u,
    float_type *p_u_next,
    const float_type *p_v,
    float_type *p_v_next,
    const float_type *p_p,
    float_type *p_p_next)
{
#ifdef GPU_BUILD
  euler_2d_calculate_next_time_step_gpu (dt, gamma, topology, geometry, p_rho, p_rho_next, p_u, p_u_next, p_v, p_v_next, p_p, p_p_next);
#else
  cpp_unreferenced (dt, gamma, topology, geometry, p_rho, p_rho_next, p_u, p_u_next, p_v, p_v_next, p_p, p_p_next);
#endif
}

#define GEN_EULER_2D_INTERFACE_INSTANCE_FOR(type)                 \
  template type euler_2d_calculate_dt_gpu_interface <type>(       \
      type gamma, type cfl,                                       \
      const grid_topology &, const grid_geometry &,               \
      type *workspace, const type *p_rho, const type *p_u,        \
      const type *p_v,const type *p_p);                           \
  template void euler_2d_calculate_next_time_step_gpu_interface ( \
      type dt, type gamma,                                        \
      const grid_topology &, const grid_geometry &,               \
      const type *p_rho, type *p_rho_next,                        \
      const type *p_u, type *p_u_next, const type *p_v,           \
      type *p_v_next, const type *p_p, type *p_p_next);

GEN_EULER_2D_INTERFACE_INSTANCE_FOR (float)
GEN_EULER_2D_INTERFACE_INSTANCE_FOR (double)

#undef GEN_EULER_2D_INTERFACE_INSTANCE_FOR

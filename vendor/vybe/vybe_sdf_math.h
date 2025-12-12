#ifndef VYBE_SDF_MATH_H
#define VYBE_SDF_MATH_H

// vybe SDF math helpers - external header to avoid ODR violations in jank standalone builds
// These functions were originally in cpp/raw blocks in vybe/sdf/math.jank

#include <cmath>

inline double sdf_sqrt(double x) { return std::sqrt(x); }
inline double sdf_pow(double x, double y) { return std::pow(x, y); }
inline double sdf_sin(double x) { return std::sin(x); }
inline double sdf_cos(double x) { return std::cos(x); }
inline double sdf_tan(double x) { return std::tan(x); }
inline double sdf_atan(double x) { return std::atan(x); }
inline double sdf_atan2(double y, double x) { return std::atan2(y, x); }
inline double sdf_floor(double x) { return std::floor(x); }
inline double sdf_ceil(double x) { return std::ceil(x); }
inline double sdf_fmod(double x, double y) { return std::fmod(x, y); }
inline double sdf_abs(double x) { return std::fabs(x); }
inline double sdf_log(double x) { return std::log(x); }
inline double sdf_exp(double x) { return std::exp(x); }

#endif // VYBE_SDF_MATH_H

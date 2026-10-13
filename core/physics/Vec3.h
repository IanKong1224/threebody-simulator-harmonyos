#pragma once
#include <cmath>

namespace threebody {

template <typename T>
struct Vec3 {
    T x{};
    T y{};
    T z{};

    Vec3() = default;
    Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(T s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(T s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

template <typename T>
inline Vec3<T> operator*(T s, const Vec3<T>& v) { return v * s; }

template <typename T>
inline T dot(const Vec3<T>& a, const Vec3<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <typename T>
inline Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

template <typename T>
inline T normSquared(const Vec3<T>& v) { return dot(v, v); }

template <typename T>
inline T norm(const Vec3<T>& v) { return std::sqrt(normSquared(v)); }

} // namespace threebody

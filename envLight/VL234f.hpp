#ifndef VL234F_HPP
#define VL234F_HPP

#include <cmath>
#include <cassert>

namespace SSLib {

    // 3D 向量
    struct Vec3f {
        float v[3];

        Vec3f() : v{ 0,0,0 } {}
        Vec3f(float x, float y, float z) : v{ x,y,z } {}

        float& operator[](int i) { return v[i]; }
        const float& operator[](int i) const { return v[i]; }

        Vec3f operator+(const Vec3f& o) const { return { v[0] + o.v[0], v[1] + o.v[1], v[2] + o.v[2] }; }
        Vec3f operator-(const Vec3f& o) const { return { v[0] - o.v[0], v[1] - o.v[1], v[2] - o.v[2] }; }
        Vec3f operator*(float s) const { return { v[0] * s, v[1] * s, v[2] * s }; }
        Vec3f operator/(float s) const { float inv = 1.0f / s; return { v[0] * inv, v[1] * inv, v[2] * inv }; }

        float dot(const Vec3f& o) const { return v[0] * o.v[0] + v[1] * o.v[1] + v[2] * o.v[2]; }
        Vec3f cross(const Vec3f& o) const {
            return { v[1] * o.v[2] - v[2] * o.v[1],
                    v[2] * o.v[0] - v[0] * o.v[2],
                    v[0] * o.v[1] - v[1] * o.v[0] };
        }
        float length() const { return std::sqrt(dot(*this)); }
        Vec3f normalized() const { float l = length(); return (l > 0.0f) ? (*this) / l : Vec3f(); }
    };

    inline Vec3f normalize(const Vec3f& v) { return v.normalized(); }

} // namespace SSLib

#endif // VL234F_HPP
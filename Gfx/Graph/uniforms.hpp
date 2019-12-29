#pragma once
#include <QColor>
#include <ossia/network/value/value.hpp>
#include <array>
#include <variant>
struct image
{
};
enum class Types
{
  Empty,
  Float,
  Vec2,
  Vec3,
  Vec4,
  Image
};

using ValueVariant
    = std::variant<std::monostate, float, ossia::vec2f, ossia::vec3f, ossia::vec4f, image>;

#define ensure(...) do { bool ok = __VA_ARGS__; assert(ok); } while(0)

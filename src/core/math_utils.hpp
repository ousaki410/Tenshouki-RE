/**
 * math_utils.hpp - math utility functions
 */

#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <random>

namespace tensyoki {

// Linear interpolation
float lerp(float a, float b, float t);

// Clamp value to range
float clamp(float value, float min, float max);

// Smoothstep
float smoothstep(float edge0, float edge1, float x);
float smootherstep(float edge0, float edge1, float x);

// Angle/radian conversion
float deg_to_rad(float degrees);
float rad_to_deg(float radians);

// Vector rotation
glm::vec2 rotate(const glm::vec2& v, float angle);
glm::vec2 reflect(const glm::vec2& v, const glm::vec2& n);

// Matrix creation
glm::mat4 ortho(float left, float right, float bottom, float top, float near, float far);
glm::mat4 perspective(float fovy, float aspect, float near, float far);
glm::mat4 look_at(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up);

// Noise generation
std::vector<float> generate_noise_2d(int width, int height, float scale, int octaves, float persistence, float lacunarity, int seed);

// Random numbers
float random_float(float min, float max);
int random_int(int min, int max);

} // namespace tensyoki
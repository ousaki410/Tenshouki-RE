/**
 * math_utils.cpp - math utility implementation
 */

#include "math_utils.hpp"
#include <cmath>
#include <random>

namespace tensyoki {

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float clamp(float value, float min, float max) {
    return std::max(min, std::min(max, value));
}

float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float smootherstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float deg_to_rad(float degrees) {
    return degrees * (M_PI / 180.0f);
}

float rad_to_deg(float radians) {
    return radians * (180.0f / M_PI);
}

glm::vec2 rotate(const glm::vec2& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

glm::vec2 reflect(const glm::vec2& v, const glm::vec2& n) {
    return v - 2.0f * glm::dot(v, n) * n;
}

glm::mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
    glm::mat4 result(1.0f);
    result[0][0] = 2.0f / (right - left);
    result[1][1] = 2.0f / (top - bottom);
    result[2][2] = -2.0f / (far - near);
    result[3][0] = -(right + left) / (right - left);
    result[3][1] = -(top + bottom) / (top - bottom);
    result[3][2] = -(far + near) / (far - near);
    return result;
}

glm::mat4 perspective(float fovy, float aspect, float near, float far) {
    float tan_half_fovy = std::tan(fovy * 0.5f);
    glm::mat4 result(0.0f);
    result[0][0] = 1.0f / (aspect * tan_half_fovy);
    result[1][1] = 1.0f / tan_half_fovy;
    result[2][2] = -(far + near) / (far - near);
    result[2][3] = -1.0f;
    result[3][2] = -(2.0f * far * near) / (far - near);
    return result;
}

glm::mat4 look_at(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) {
    glm::vec3 f = glm::normalize(center - eye);
    glm::vec3 s = glm::normalize(glm::cross(f, up));
    glm::vec3 u = glm::cross(s, f);
    
    glm::mat4 result(1.0f);
    result[0][0] = s.x;
    result[1][0] = s.y;
    result[2][0] = s.z;
    result[0][1] = u.x;
    result[1][1] = u.y;
    result[2][1] = u.z;
    result[0][2] = -f.x;
    result[1][2] = -f.y;
    result[2][2] = -f.z;
    result[3][0] = -glm::dot(s, eye);
    result[3][1] = -glm::dot(u, eye);
    result[3][2] = glm::dot(f, eye);
    return result;
}

std::vector<float> generate_noise_2d(int width, int height, float scale, int octaves, float persistence, float lacunarity, int seed) {
    std::vector<float> noise(width * height);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // Simple value noise implementation
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float amplitude = 1.0f;
            float frequency = scale;
            float noise_value = 0.0f;
            float max_amplitude = 0.0f;
            
            for (int i = 0; i < octaves; ++i) {
                float sample_x = x * frequency / width;
                float sample_y = y * frequency / height;
                
                // Bilinear interpolation of random values
                int x0 = static_cast<int>(sample_x);
                int y0 = static_cast<int>(sample_y);
                int x1 = x0 + 1;
                int y1 = y0 + 1;
                
                float tx = sample_x - x0;
                float ty = sample_y - y0;
                
                // Hash-based pseudo-random
                auto hash = [&](int hx, int hy) -> float {
                    uint32_t h = (hx * 73856093) ^ (hy * 19349663) ^ (seed * 83492791);
                    h = (h ^ (h >> 13)) * 1274126177;
                    h = (h ^ (h >> 16)) * 2685821657;
                    h ^= h >> 13;
                    return static_cast<float>(h) / static_cast<float>(UINT32_MAX);
                };
                
                float v00 = hash(x0, y0);
                float v10 = hash(x1, y0);
                float v01 = hash(x0, y1);
                float v11 = hash(x1, y1);
                
                float i0 = lerp(v00, v10, tx);
                float i1 = lerp(v01, v11, tx);
                float value = lerp(i0, i1, ty);
                
                noise_value += value * amplitude;
                max_amplitude += amplitude;
                
                amplitude *= persistence;
                frequency *= lacunarity;
            }
            
            noise[y * width + x] = noise_value / max_amplitude;
        }
    }
    
    return noise;
}

float random_float(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

int random_int(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

} // namespace tensyoki
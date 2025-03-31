#include <Windows.h>
#include <iostream>
#include <vector>
#include <limits>
#include <memory>
#include <random>
#include <GL/glew.h>
#include <GL/GL.h>
#include <GL/freeglut.h>
#define GLFW_INCLUDE_GLU
#define GLFW_DLL
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <cmath>

using namespace glm;

// -------------------------------------------------
// Global Variables
// -------------------------------------------------
int Width = 512;
int Height = 512;
std::vector<float> OutputImage;
// -------------------------------------------------

std::default_random_engine rng;
std::uniform_real_distribution<float> dist(0.0f, 1.0f);

class Ray {
public:
    vec3 origin;
    vec3 direction;
    Ray(const vec3& o, const vec3& d) : origin(o), direction(normalize(d)) {}
};

class Material {
public:
    vec3 ka, kd, ks;
    float shininess;
    Material(vec3 a, vec3 d, vec3 s, float sh) : ka(a), kd(d), ks(s), shininess(sh) {}
};

class Surface {
public:
    Material material;
    Surface(const Material& mat) : material(mat) {}
    virtual bool intersect(const Ray& ray, float& t, vec3& normal) const = 0;
    virtual vec3 getPosition(const Ray& ray, float t) const = 0;
    virtual ~Surface() = default;
};

class Sphere : public Surface {
public:
    vec3 center;
    float radius;
    Sphere(vec3 c, float r, const Material& m) : Surface(m), center(c), radius(r) {}
    bool intersect(const Ray& ray, float& t, vec3& normal) const override {
        vec3 oc = ray.origin - center;
        float b = dot(oc, ray.direction);
        float c = dot(oc, oc) - radius * radius;
        float discriminant = b * b - c;
        if (discriminant < 0) return false;
        float sqrtD = std::sqrt(discriminant);
        float t0 = -b - sqrtD;
        float t1 = -b + sqrtD;
        t = (t0 > 0) ? t0 : ((t1 > 0) ? t1 : -1);
        if (t < 0) return false;
        vec3 p = ray.origin + t * ray.direction;
        normal = normalize(p - center);
        return true;
    }
    vec3 getPosition(const Ray& ray, float t) const override {
        return ray.origin + t * ray.direction;
    }
};

class Plane : public Surface {
public:
    vec3 point, normal;
    Plane(vec3 p, vec3 n, const Material& m) : Surface(m), point(p), normal(normalize(n)) {}
    bool intersect(const Ray& ray, float& t, vec3& outNormal) const override {
        float denom = dot(normal, ray.direction);
        if (abs(denom) < 1e-6) return false;
        t = dot(point - ray.origin, normal) / denom;
        if (t < 0) return false;
        outNormal = normal;
        return true;
    }
    vec3 getPosition(const Ray& ray, float t) const override {
        return ray.origin + t * ray.direction;
    }
};

class Camera {
public:
    vec3 e, u, v, w;
    float l, r, b, t, d;
    int nx, ny;
    Camera(vec3 eye, vec3 up, vec3 look, float l_, float r_, float b_, float t_, float d_, int nx_, int ny_)
        : e(eye), l(l_), r(r_), b(b_), t(t_), d(d_), nx(nx_), ny(ny_) {
        w = normalize(eye - look);
        u = normalize(cross(up, w));
        v = cross(w, u);
    }
    Ray getRay(float i, float j) const {
        float su = l + (r - l) * (i) / nx;
        float sv = b + (t - b) * (j) / ny;
        vec3 dir = -d * w + su * u + sv * v;
        return Ray(e, dir);
    }
};

class Light {
public:
    vec3 position;
    vec3 intensity;
    Light(const vec3& pos, const vec3& inten) : position(pos), intensity(inten) {}
};

class Scene {
public:
    std::vector<std::shared_ptr<Surface>> objects;
    std::vector<Light> lights;

    void addObject(const std::shared_ptr<Surface>& obj) {
        objects.push_back(obj);
    }

    void addLight(const Light& light) {
        lights.push_back(light);
    }
};

Scene scene;
Camera* camera;

vec3 shade(const Ray& ray, const vec3& point, const vec3& normal, const Material& mat) {
    vec3 color(0);
    for (const auto& light : scene.lights) {
        vec3 diffuse(0), reflectDir(0), specular(0);
        vec3 toLight = normalize(light.position - point);
        vec3 toView = normalize(-ray.direction);
        vec3 ambient = mat.ka * light.intensity;

        Ray shadowRay(point + normal * 1e-3f, toLight);
        for (auto& obj : scene.objects) {
            float t;
            vec3 n;
            if (obj->intersect(shadowRay, t, n)) {
                color += ambient;
                goto next_light;
            }
        }

        diffuse = mat.kd * light.intensity * std::max(dot(normal, toLight), 0.0f);
        reflectDir = reflect(-toLight, normal);
        specular = mat.ks * light.intensity * pow(std::max(dot(toView, reflectDir), 0.0f), mat.shininess);
        color += ambient + diffuse + specular;

    next_light:;
    }
    return color;
}

vec3 trace(const Ray& ray) {
    float minT = std::numeric_limits<float>::max();
    vec3 hitColor(0);
    for (auto& obj : scene.objects) {
        float t;
        vec3 normal;
        if (obj->intersect(ray, t, normal) && t < minT) {
            minT = t;
            vec3 p = obj->getPosition(ray, t);
            hitColor = shade(ray, p, normal, obj->material);
        }
    }
    return hitColor;
}

vec3 gammaCorrect(const vec3& color) {
    return pow(color, vec3(1.0f / 2.2f));
}

void render() {
    //Create our image. We don't want to do this in 
    //the main loop since this may be too slow and we 
    //want a responsive display of our beautiful image.
    //Instead we draw to another buffer and copy this to the 
    //framebuffer using glDrawPixels(...) every refresh
    camera = new Camera(vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 0, -1), -0.1f, 0.1f, -0.1f, 0.1f, 0.1f, Width, Height);

    scene.addObject(std::make_shared<Plane>(vec3(0, -2, 0), vec3(0, 1, 0), Material(vec3(0.2), vec3(1), vec3(0), 0)));
    scene.addObject(std::make_shared<Sphere>(vec3(-4, 0, -7), 1, Material(vec3(0.2, 0, 0), vec3(1, 0, 0), vec3(0), 0)));
    scene.addObject(std::make_shared<Sphere>(vec3(0, 0, -7), 2, Material(vec3(0, 0.2, 0), vec3(0, 0.5, 0), vec3(0.5), 32)));
    scene.addObject(std::make_shared<Sphere>(vec3(4, 0, -7), 1, Material(vec3(0, 0, 0.2), vec3(0, 0, 1), vec3(0), 0)));

    scene.addLight(Light(vec3(-4, 4, -3), vec3(1.0f)));

    OutputImage.resize(Width * Height * 3);
#pragma omp parallel for
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            vec3 color(0);
            for (int i = 0; i < 64; ++i) {
                float u = x + dist(rng);
                float v = y + dist(rng);
                Ray ray = camera->getRay(u, v);
                color += trace(ray);
            }
            color /= 64.0f;
            color = gammaCorrect(color);
            int idx = (y * Width + x) * 3;
            OutputImage[idx + 0] = color.r;
            OutputImage[idx + 1] = color.g;
            OutputImage[idx + 2] = color.b;
        }
    }
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawPixels(Width, Height, GL_RGB, GL_FLOAT, OutputImage.data());
    glutSwapBuffers();
}

int main(int argc, char** argv) { //using OpenAI
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(Width, Height);
    glutCreateWindow("Ray Tracer");
    glewInit();
    render();
    glutDisplayFunc(display);
    glutMainLoop();
    return 0;
}

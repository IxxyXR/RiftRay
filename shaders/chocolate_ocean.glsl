// Chocolate Ocean
// Original Shadertoy: https://www.shadertoy.com/view/MdXyzX
// Source adaptation: https://gist.github.com/rngtm/c91cfd99a70062ee5732b6078b4488ba
// Copyright afl_ext, 2017-2023. Released under the MIT License.
// Adapted for RiftRay's world-space ray API.

// @var title Chocolate Ocean
// @var author afl_ext
// @var license MIT
// @var url https://www.shadertoy.com/view/MdXyzX
// @var eyePos 0.0 1.5 0.0
// @var headSize 1.0

#define DRAG_MULT 0.28
#define WATER_DEPTH 1.0
#define ITERATIONS_RAYMARCH 12
#define ITERATIONS_NORMAL 40

#define SKY_COLOR hsv2rgb(vec3(14.0 / 256.0, 210.0 / 256.0, 6.0))
#define FOG_COLOR hsv2rgb(vec3(16.0 / 256.0, 220.0 / 256.0, 6.0))
#define FOG_START 5.0
#define FOG_END 200.0
#define FOG_EXPONENT 1.2
#define POST_BRIGHTNESS_SKY 2.0
#define POST_BRIGHTNESS 2.0
#define CHOCOLATE_BRIGHTNESS 0.2
#define BASE_COLOR vec3(0.16, 0.11, 0.11)

vec2 wavedx(vec2 position, vec2 direction, float frequency, float timeshift)
{
    float x = dot(direction, position) * frequency + timeshift;
    float wave = exp(sin(x) - 1.0);
    float dx = wave * cos(x);
    return vec2(wave, -dx);
}

float getwaves(vec2 position, int iterations)
{
    float iter = 0.0;
    float frequency = 1.0;
    float timeMultiplier = 1.0;
    float weight = 1.0;
    float sumOfValues = 0.0;
    float sumOfWeights = 0.0;

    for (int i = 0; i < iterations; i++)
    {
        vec2 direction = vec2(sin(iter), cos(iter));
        vec2 wave = wavedx(
            position,
            direction,
            frequency,
            iGlobalTime * timeMultiplier);

        position += direction * wave.y * weight * DRAG_MULT;
        sumOfValues += wave.x * weight;
        sumOfWeights += weight;
        weight *= 0.82;
        frequency *= 1.18;
        timeMultiplier *= 1.07;
        iter += 1232.399963;
    }

    return sumOfValues / sumOfWeights;
}

float raymarchwater(vec3 camera, vec3 start, vec3 end, float depth)
{
    vec3 pos = start;
    vec3 dir = normalize(end - start);
    for (int i = 0; i < 64; i++)
    {
        float height = getwaves(pos.xz, ITERATIONS_RAYMARCH) * depth - depth;
        if (height + 0.01 > pos.y)
            return distance(pos, camera);

        pos += dir * (pos.y - height);
    }
    return distance(start, camera);
}

vec3 oceanNormal(vec2 pos, float e, float depth)
{
    vec2 ex = vec2(e, 0.0);
    float height = getwaves(pos, ITERATIONS_NORMAL) * depth;
    vec3 center = vec3(pos.x, height, pos.y);
    return normalize(cross(
        center - vec3(
            pos.x - e,
            getwaves(pos - ex, ITERATIONS_NORMAL) * depth,
            pos.y),
        center - vec3(
            pos.x,
            getwaves(pos + ex.yx, ITERATIONS_NORMAL) * depth,
            pos.y + e)));
}

float intersectPlane(
    vec3 origin,
    vec3 direction,
    vec3 point,
    vec3 planeNormal)
{
    return clamp(
        dot(point - origin, planeNormal) / dot(direction, planeNormal),
        -1.0,
        9991999.0);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 k = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + k.xyz) * 6.0 - k.www);
    return c.z * mix(k.xxx, clamp(p - k.xxx, 0.0, 1.0), c.y);
}

vec3 extraCheapAtmosphere(vec3 raydir, vec3 sundir)
{
    sundir.y = max(sundir.y, -0.07);
    float horizon = 1.0 / (raydir.y + 0.1);
    float sunHeight = 1.0 / (sundir.y * 11.0 + 1.0);
    float raySun = pow(abs(dot(sundir, raydir)), 2.0);
    float sun = pow(max(0.0, dot(sundir, raydir)), 8.0);
    float mie = sun * horizon * 0.2;
    vec3 sunColor = mix(
        vec3(1.0),
        max(vec3(0.0), vec3(1.0) - SKY_COLOR / 22.4),
        sunHeight);
    vec3 blueSky = SKY_COLOR / 22.4 * sunColor;
    vec3 blueSky2 = max(
        vec3(0.0),
        blueSky - SKY_COLOR * 0.002 *
            (horizon - 6.0 * sundir.y * sundir.y));
    blueSky2 *= horizon * (0.24 + raySun * 0.24);
    return blueSky2 * (1.0 + pow(1.0 - raydir.y, 3.0)) + mie * sunColor;
}

vec3 getSunDirection()
{
    return normalize(vec3(
        sin(iGlobalTime * 0.1),
        1.0,
        cos(iGlobalTime * 0.1)));
}

vec3 getAtmosphere(vec3 dir)
{
    return extraCheapAtmosphere(dir, getSunDirection()) * 0.5;
}

float getSun(vec3 dir)
{
    return pow(max(0.0, dot(dir, getSunDirection())), 720.0) * 210.0;
}

vec3 acesTonemap(vec3 color)
{
    mat3 m1 = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777);
    mat3 m2 = mat3(
        1.60475, -0.10208, -0.00327,
       -0.53108,  1.10813, -0.07276,
       -0.07367, -0.00605,  1.07602);
    vec3 v = m1 * color;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));
}

vec3 getSceneColor(in vec3 camera, in vec3 ray)
{
    if (ray.y >= 0.0)
        return acesTonemap(FOG_COLOR * POST_BRIGHTNESS_SKY);

    // Move across the surface while retaining RiftRay's tracked eye height.
    camera.xz += vec2(iGlobalTime);

    vec3 waterPlaneHigh = vec3(0.0);
    vec3 waterPlaneLow = vec3(0.0, -WATER_DEPTH, 0.0);
    float highPlaneHit = intersectPlane(
        camera, ray, waterPlaneHigh, vec3(0.0, 1.0, 0.0));
    float lowPlaneHit = intersectPlane(
        camera, ray, waterPlaneLow, vec3(0.0, 1.0, 0.0));
    vec3 highHitPos = camera + ray * highPlaneHit;
    vec3 lowHitPos = camera + ray * lowPlaneHit;

    float dist = raymarchwater(
        camera, highHitPos, lowHitPos, WATER_DEPTH);
    vec3 waterHitPos = camera + ray * dist;
    vec3 surfaceNormal = oceanNormal(waterHitPos.xz, 0.01, WATER_DEPTH);
    surfaceNormal = mix(
        surfaceNormal,
        vec3(0.0, 1.0, 0.0),
        0.8 * min(1.0, sqrt(dist * 0.01) * 1.1));

    float fresnel = 0.04 + 0.96 * pow(
        1.0 - max(0.0, dot(-surfaceNormal, ray)),
        5.0);
    vec3 reflectionRay = normalize(reflect(ray, surfaceNormal));
    reflectionRay.y = abs(reflectionRay.y);
    vec3 reflection = getAtmosphere(reflectionRay) + getSun(reflectionRay);
    vec3 scattering = BASE_COLOR * CHOCOLATE_BRIGHTNESS *
        (0.3 + (waterHitPos.y + WATER_DEPTH) / WATER_DEPTH);
    vec3 color = fresnel * reflection + (1.0 - fresnel) * scattering;

    float fogDensity = clamp(
        (dist - FOG_START) / (FOG_END - FOG_START),
        0.0,
        1.0);
    fogDensity = pow(fogDensity, FOG_EXPONENT);
    color = mix(color, FOG_COLOR, fogDensity);
    return acesTonemap(color * POST_BRIGHTNESS);
}


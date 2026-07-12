#pragma once
#include "rarity.h"
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

class CEntity;
class CPlayer;

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <cstddef>

#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <wincrypt.h>

#else
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#endif


inline std::mt19937& GetRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

inline float GetLimitedRng(float start, float end)
{
    static std::mt19937 rng(std::random_device{}());

    std::uniform_real_distribution<float> dist(start, end);
    return dist(rng);
}

inline bool CheckChance(double chance)
{
    chance = std::clamp(chance, 0.0, 1.0);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(GetRng()) < chance;
}

inline bool CheckTeam(int t1, int t2)
{
    if (t1 == 0 || t2 == 0)
        return false;
    return t1 == t2;
}

inline int GetLevel(ERarity rarity)
{
    switch (rarity)
    {
    case ERarity::Common:
        return 1;
    case ERarity::Unusual:
        return 2;
    case ERarity::Rare:
        return 3;
    case ERarity::Epic:
        return 4;
    case ERarity::Legendary:
        return 5;
    case ERarity::Mythic:
        return 6;
    case ERarity::Ultra:
        return 7;
    case ERarity::Super:
        return 8;
    case ERarity::Eternal:
    case ERarity::Unique:
        return 9;
    case ERarity::Primordial:
        return 10;
    default:
        return 1;
    }
}

inline float Distance(sf::Vector2f v1, sf::Vector2f v2)
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    return sqrtf(dx*dx + dy*dy);
}

inline float DistanceSq(sf::Vector2f v1, sf::Vector2f v2)
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    return dx * dx + dy * dy;
}

inline float Length(sf::Vector2f v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

inline float LengthSq(sf::Vector2f v)
{
    return v.x * v.x + v.y * v.y;
}

CPlayer* FindPlayerFromEntity(CEntity* entity, const std::vector<std::unique_ptr<CPlayer>>& players);

#ifdef __EMSCRIPTEN__
EM_JS(void, emscripten_get_random_bytes, (void* ptr, size_t len), {
    const CHUNK_SIZE = 65536;
    var heapU8 = HEAPU8;
    var byteOffset = ptr;
    var remaining = len;
    while (remaining > 0) {
        var chunk = Math.min(remaining, CHUNK_SIZE);
        var array = new Uint8Array(chunk);
        crypto.getRandomValues(array);
        for (var i = 0; i < chunk; ++i) {
            heapU8[byteOffset + i] = array[i];
        }
        byteOffset += chunk;
        remaining -= chunk;
    }
});
#endif

inline bool PlatformRandomBytes(uint8_t *buf, size_t len)
{
#if defined(__EMSCRIPTEN__)
    emscripten_get_random_bytes(buf, len);
    return true;
#elif defined(_WIN32)
    HCRYPTPROV h_provider = 0;
    if(!CryptAcquireContextW(&h_provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        return false;
    }
    bool success = CryptGenRandom(h_provider, static_cast<DWORD>(len), buf);
    CryptReleaseContext(h_provider, 0);
    return success;
#elif defined(__APPLE__)
    arc4random_buf(buf, len);
    return true;
#elif defined(__ANDROID__)
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
        return false;
    size_t bytes_read = 0;
    while(bytes_read < len)
    {
        ssize_t r = read(fd, buf + bytes_read, len - bytes_read);
        if(r <= 0)
        {
            close(fd);
            return false;
        }
        bytes_read += r;
    }
    close(fd);
    return true;
#else
    ssize_t ret = getrandom(buf, len, 0);
    if(ret == static_cast<ssize_t>(len))
    {
        return true;
    }

    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        return false;
    }

    size_t bytes_read = 0;
    while(bytes_read < len)
    {
        ssize_t r = read(fd, buf + bytes_read, len - bytes_read);
        if(r <= 0)
        {
            close(fd);
            return false;
        }
        bytes_read += r;
    }
    close(fd);
    return true;
#endif
}

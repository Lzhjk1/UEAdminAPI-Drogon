#include "QrPng.h"

#include "qrcode/qrcodegen.hpp"

#include <zlib.h>

#include <cstring>
#include <optional>
#include <string>

namespace UEAdminAPI {
namespace utils {

namespace {

void AppendU32BE(std::string &out, uint32_t v)
{
    out.push_back(static_cast<char>((v >> 24) & 0xff));
    out.push_back(static_cast<char>((v >> 16) & 0xff));
    out.push_back(static_cast<char>((v >> 8) & 0xff));
    out.push_back(static_cast<char>(v & 0xff));
}

void AppendChunk(std::string &png, const char type[4], const std::string &data)
{
    AppendU32BE(png, static_cast<uint32_t>(data.size()));
    const size_t crcStart = png.size();
    png.append(type, 4);
    png.append(data);
    const uLong crc = crc32(0L, reinterpret_cast<const Bytef *>(png.data() + crcStart),
                            static_cast<uInt>(png.size() - crcStart));
    AppendU32BE(png, static_cast<uint32_t>(crc));
}

} // namespace

std::string RenderQrPng(const std::string &text, int scale)
{
    if (text.empty())
    {
        return "";
    }
    if (scale < 2)
    {
        scale = 2;
    }
    if (scale > 16)
    {
        scale = 16;
    }

    std::optional<qrcodegen::QrCode> qr;
    try
    {
        qr.emplace(qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM));
    }
    catch (const std::exception &)
    {
        return "";
    }

    const int border = 4;
    const int modules = qr->getSize();
    const int size = (modules + border * 2) * scale;

    // 每行：1 字节 filter(0) + width 字节灰度
    std::string raw;
    raw.assign(static_cast<size_t>(size) * (static_cast<size_t>(size) + 1), '\0');
    for (int y = 0; y < size; ++y)
    {
        char *row = raw.data() + static_cast<size_t>(y) * (static_cast<size_t>(size) + 1);
        row[0] = 0;
        std::memset(row + 1, 0xff, static_cast<size_t>(size));
        const int my = y / scale - border;
        if (my < 0 || my >= modules)
        {
            continue;
        }
        for (int x = 0; x < size; ++x)
        {
            const int mx = x / scale - border;
            if (mx >= 0 && mx < modules && qr->getModule(mx, my))
            {
                row[1 + x] = 0;
            }
        }
    }

    uLongf compressedLen = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(compressedLen, '\0');
    if (compress2(reinterpret_cast<Bytef *>(compressed.data()), &compressedLen,
                  reinterpret_cast<const Bytef *>(raw.data()), static_cast<uLong>(raw.size()), 9) != Z_OK)
    {
        return "";
    }
    compressed.resize(compressedLen);

    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    AppendU32BE(ihdr, static_cast<uint32_t>(size));
    AppendU32BE(ihdr, static_cast<uint32_t>(size));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(0);  // color type: grayscale
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", compressed);
    AppendChunk(png, "IEND", std::string());
    return png;
}

} // namespace utils
} // namespace UEAdminAPI

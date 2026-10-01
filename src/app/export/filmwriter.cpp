#include "filmwriter.h"
#include <QColorSpace>
#include <QImageWriter>
#include <QSaveFile>
#include <cmath>
#include <numeric>
#include <vector>

QImage toFilmImage(const QImage& dithered) {
    const QImage argb = dithered.convertToFormat(QImage::Format_ARGB32);
    bool bilevel = true;
    bool opaque = true;
    for (int y = 0; y < argb.height() && (bilevel || opaque); y++) {
        const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); x++) {
            const QRgb p = row[x];
            opaque &= qAlpha(p) == 255;
            bilevel &= p == 0xff000000u || p == 0xffffffffu;
        }
    }
    QImage result;
    if (bilevel) {
        result = QImage(argb.size(), QImage::Format_Mono);
        result.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});  // index 0 black, 1 white
        for (int y = 0; y < argb.height(); y++) {
            const QRgb* src = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
            uchar* dst = result.scanLine(y);
            std::fill(dst, dst + result.bytesPerLine(), 0);
            for (int x = 0; x < argb.width(); x++) {
                if (src[x] == 0xffffffffu) {
                    dst[x >> 3] |= static_cast<uchar>(0x80 >> (x & 7));  // Format_Mono: most significant bit first
                }
            }
        }
    } else {
        result = argb.convertToFormat(opaque ? QImage::Format_RGB32 : QImage::Format_ARGB32);
    }
    result.setDotsPerMeterX(dithered.dotsPerMeterX());
    result.setDotsPerMeterY(dithered.dotsPerMeterY());
    return result;
}

/* ---------------- TIFF ---------------- */

namespace {

enum : uint16_t { SHORT = 3, LONG = 4, RATIONAL = 5, ASCII = 2, UNDEFINED = 7 };

struct Entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;  // the value itself if it fits in 4 bytes, else an offset
};

class Buffer {
public:
    std::vector<uint8_t> bytes;
    void u8(const uint8_t v) { bytes.push_back(v); }
    void u16(const uint16_t v) { u8(v & 0xff); u8(v >> 8); }
    void u32(const uint32_t v) { u16(v & 0xffff); u16(v >> 16); }
    void put(const uint8_t* data, const size_t n) { bytes.insert(bytes.end(), data, data + n); }
    void patch32(const size_t at, const uint32_t v) {
        for (int i = 0; i < 4; i++) bytes[at + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    [[nodiscard]] size_t size() const { return bytes.size(); }
    void align() { if (bytes.size() & 1) u8(0); }  // TIFF offsets must be word-aligned
};

void packBits(const uint8_t* row, const size_t n, Buffer& out) {
    /* PackBits (TIFF compression 32773): runs of 2-128 equal bytes become (1 - n, byte), other bytes are copied
     * in literal groups of up to 128 as (n - 1, bytes...). Each row is encoded on its own, as the spec requires. */
    size_t i = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && row[i + run] == row[i]) run++;
        if (run >= 2) {
            out.u8(static_cast<uint8_t>(257 - run));  // i.e. -(run - 1) as a signed byte
            out.u8(row[i]);
            i += run;
            continue;
        }
        size_t literal = 1;
        while (i + literal < n && literal < 128 &&
               !(i + literal + 1 < n && row[i + literal] == row[i + literal + 1])) {
            literal++;
        }
        out.u8(static_cast<uint8_t>(literal - 1));
        out.put(row + i, literal);
        i += literal;
    }
}

void rational(const double value, uint32_t* numerator, uint32_t* denominator) {
    /* exact for whole and common fractional DPIs: 300 -> 300/1, 299.5 -> 599/2 */
    uint32_t den = 1000;
    uint32_t num = static_cast<uint32_t>(std::lround(value * den));
    const uint32_t g = std::gcd(num, den);
    *numerator = num / g;
    *denominator = den / g;
}

}  // namespace

bool writeTiff(const QString& path, const QImage& source, const double dpi, const TiffCompression compression,
               QString* error) {
    QImage image = source;
    if (image.format() != QImage::Format_Mono && image.format() != QImage::Format_Grayscale8 &&
        image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    const bool mono = image.format() == QImage::Format_Mono;
    const bool grey = image.format() == QImage::Format_Grayscale8;
    const bool alpha = image.format() == QImage::Format_ARGB32;
    const int samples = mono || grey ? 1 : (alpha ? 4 : 3);
    const int bits = mono ? 1 : 8;
    const int width = image.width();
    const int height = image.height();
    const size_t rowBytes = mono ? (static_cast<size_t>(width) + 7) / 8 : static_cast<size_t>(width) * samples;

    // Format_Mono stores palette indices; make sure 1 means white so the bits can go out as they are
    const bool invertMono = mono && image.colorTable().value(1) != qRgb(255, 255, 255);

    Buffer out;
    out.u8('I'); out.u8('I'); out.u16(42);  // little-endian TIFF
    const size_t ifdOffsetAt = out.size();
    out.u32(0);                            // patched once the IFD is written

    // image data in strips of ~64 KB (uncompressed), each strip starting on a word boundary
    const int rowsPerStrip = std::max(1, static_cast<int>(65536 / std::max<size_t>(rowBytes, 1)));
    std::vector<uint32_t> stripOffsets, stripCounts;
    std::vector<uint8_t> row(rowBytes);
    for (int y0 = 0; y0 < height; y0 += rowsPerStrip) {
        out.align();
        const size_t start = out.size();
        for (int y = y0; y < std::min(y0 + rowsPerStrip, height); y++) {
            const uchar* line = image.constScanLine(y);
            if (mono) {
                std::copy(line, line + rowBytes, row.begin());
                if (invertMono) for (uint8_t& b : row) b = static_cast<uint8_t>(~b);
                if (width % 8) row[rowBytes - 1] &= static_cast<uint8_t>(0xff00 >> (width % 8));  // clear padding bits
            } else if (grey) {
                std::copy(line, line + rowBytes, row.begin());
            } else {
                const QRgb* px = reinterpret_cast<const QRgb*>(line);
                for (int x = 0; x < width; x++) {
                    uint8_t* d = row.data() + static_cast<size_t>(x) * samples;
                    d[0] = static_cast<uint8_t>(qRed(px[x]));
                    d[1] = static_cast<uint8_t>(qGreen(px[x]));
                    d[2] = static_cast<uint8_t>(qBlue(px[x]));
                    if (alpha) d[3] = static_cast<uint8_t>(qAlpha(px[x]));
                }
            }
            if (compression == TiffCompression::PackBits) {
                packBits(row.data(), rowBytes, out);
            } else {
                out.put(row.data(), rowBytes);
            }
        }
        if (out.size() > 0xffffff00ull) {
            *error = QObject::tr("TIFF larger than 4 GB is not supported");
            return false;
        }
        stripOffsets.push_back(static_cast<uint32_t>(start));
        stripCounts.push_back(static_cast<uint32_t>(out.size() - start));
    }

    // out-of-line tag values
    const auto longArray = [&](const std::vector<uint32_t>& values) -> uint32_t {
        if (values.size() == 1) return values[0];  // fits in the entry itself
        out.align();
        const uint32_t at = static_cast<uint32_t>(out.size());
        for (const uint32_t v : values) out.u32(v);
        return at;
    };
    const uint32_t offsetsValue = longArray(stripOffsets);
    const uint32_t countsValue = longArray(stripCounts);
    uint32_t bitsValue = static_cast<uint32_t>(bits);
    if (samples > 2) {  // more than two SHORTs do not fit in the entry
        out.align();
        bitsValue = static_cast<uint32_t>(out.size());
        for (int i = 0; i < samples; i++) out.u16(8);
    }
    uint32_t num, den;
    rational(dpi, &num, &den);
    out.align();
    const uint32_t resolutionAt = static_cast<uint32_t>(out.size());
    out.u32(num); out.u32(den);  // X and Y share the same rational
    const QByteArray software = QByteArrayLiteral("Ditherista Press");
    const uint32_t softwareAt = static_cast<uint32_t>(out.size());
    out.put(reinterpret_cast<const uint8_t*>(software.constData()), static_cast<size_t>(software.size()));
    out.u8(0);
    // the colour profile of a colour film (Preferences > Color Management): tag 34675, the ICC data as is
    const QByteArray icc = samples >= 3 && image.colorSpace().isValid() ? image.colorSpace().iccProfile() : QByteArray();
    out.align();
    const uint32_t iccAt = static_cast<uint32_t>(out.size());
    out.put(reinterpret_cast<const uint8_t*>(icc.constData()), static_cast<size_t>(icc.size()));

    std::vector<Entry> entries = {
        {256, LONG, 1, static_cast<uint32_t>(width)},
        {257, LONG, 1, static_cast<uint32_t>(height)},
        {258, SHORT, static_cast<uint32_t>(samples), bitsValue},
        {259, SHORT, 1, compression == TiffCompression::PackBits ? 32773u : 1u},
        {262, SHORT, 1, samples >= 3 ? 2u : 1u},  // RGB, or BlackIsZero for 1-bit and grey
        {273, LONG, static_cast<uint32_t>(stripOffsets.size()), offsetsValue},
        {277, SHORT, 1, static_cast<uint32_t>(samples)},
        {278, LONG, 1, static_cast<uint32_t>(rowsPerStrip)},
        {279, LONG, static_cast<uint32_t>(stripCounts.size()), countsValue},
        {282, RATIONAL, 1, resolutionAt},
        {283, RATIONAL, 1, resolutionAt},
        {284, SHORT, 1, 1u},  // chunky
        {296, SHORT, 1, 2u},  // resolution unit: inch
        {305, ASCII, static_cast<uint32_t>(software.size() + 1), softwareAt},
    };
    if (alpha) {
        entries.push_back({338, SHORT, 1, 2u});  // extra sample: unassociated alpha
    }
    if (!icc.isEmpty()) {
        entries.push_back({34675, UNDEFINED, static_cast<uint32_t>(icc.size()), iccAt});  // tags stay in order
    }
    out.align();
    out.patch32(ifdOffsetAt, static_cast<uint32_t>(out.size()));
    out.u16(static_cast<uint16_t>(entries.size()));
    for (const Entry& e : entries) {
        out.u16(e.tag);
        out.u16(e.type);
        out.u32(e.count);
        if (e.type == SHORT && e.count == 1) {
            out.u16(static_cast<uint16_t>(e.value));  // a single SHORT sits left-justified in the field
            out.u16(0);
        } else {
            out.u32(e.value);
        }
    }
    out.u32(0);  // no next IFD

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<qint64>(out.size())) !=
            static_cast<qint64>(out.size()) ||
        !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

static bool writeWithQt(const QString& path, const QImage& source, const double dpi, const char* format,
                        QString* error) {
    QImage image = source;
    const int dotsPerMeter = static_cast<int>(std::lround(dpi / 0.0254));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, format);
    if (!writer.write(image)) {
        *error = writer.errorString();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool writePng(const QString& path, const QImage& image, const double dpi, QString* error) {
    return writeWithQt(path, image, dpi, "png", error);
}

bool writeBmp(const QString& path, const QImage& image, const double dpi, QString* error) {
    return writeWithQt(path, image, dpi, "bmp", error);
}

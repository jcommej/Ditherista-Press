#include "psdwriter.h"
#include <QObject>
#include <QSaveFile>
#include <cmath>

namespace {

class BigEndian {
    /* PSD is big-endian throughout */
public:
    std::vector<uint8_t> bytes;
    void u8(const uint8_t v) { bytes.push_back(v); }
    void u16(const uint16_t v) { u8(static_cast<uint8_t>(v >> 8)); u8(v & 0xff); }
    void u32(const uint32_t v) { u16(static_cast<uint16_t>(v >> 16)); u16(v & 0xffff); }
    void put(const uint8_t* data, const size_t n) { bytes.insert(bytes.end(), data, data + n); }
    void tag(const char* four) { put(reinterpret_cast<const uint8_t*>(four), 4); }
    void patch32(const size_t at, const uint32_t v) {
        bytes[at] = static_cast<uint8_t>(v >> 24); bytes[at + 1] = static_cast<uint8_t>(v >> 16);
        bytes[at + 2] = static_cast<uint8_t>(v >> 8); bytes[at + 3] = static_cast<uint8_t>(v);
    }
    void patch16(const size_t at, const uint16_t v) {
        bytes[at] = static_cast<uint8_t>(v >> 8); bytes[at + 1] = static_cast<uint8_t>(v);
    }
    [[nodiscard]] size_t size() const { return bytes.size(); }
};

void packBitsRow(const uint8_t* row, const size_t n, std::vector<uint8_t>& out) {
    /* PackBits, as in TIFF: runs of 2-128 equal bytes as (257 - n, byte), literals as (n - 1, bytes...) */
    size_t i = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && row[i + run] == row[i]) run++;
        if (run >= 2) {
            out.push_back(static_cast<uint8_t>(257 - run));
            out.push_back(row[i]);
            i += run;
            continue;
        }
        size_t literal = 1;
        while (i + literal < n && literal < 128 && !(i + literal + 1 < n && row[i + literal] == row[i + literal + 1])) {
            literal++;
        }
        out.push_back(static_cast<uint8_t>(literal - 1));
        out.insert(out.end(), row + i, row + i + literal);
        i += literal;
    }
}

void resource(BigEndian& out, const uint16_t id, const std::vector<uint8_t>& data) {
    /* one image resource block: signature, id, empty Pascal name (padded to even), size, data (padded to even) */
    out.tag("8BIM");
    out.u16(id);
    out.u8(0);
    out.u8(0);
    out.u32(static_cast<uint32_t>(data.size()));
    out.put(data.data(), data.size());
    if (data.size() & 1) out.u8(0);
}

bool isGrey(const QImage& image) {
    if (image.format() == QImage::Format_Mono || image.format() == QImage::Format_Grayscale8) return true;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); y++) {
        const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); x++) {
            if (qRed(row[x]) != qGreen(row[x]) || qGreen(row[x]) != qBlue(row[x])) return false;
        }
    }
    return true;
}

}  // namespace

bool writePsd(const QString& path, const QImage& composite, const std::vector<PsdSpotChannel>& spots, const double dpi,
              QString* error) {
    const int w = composite.width();
    const int h = composite.height();
    if (w > PSD_MAX_SIDE || h > PSD_MAX_SIDE) {
        *error = QObject::tr("PSD is limited to %1 px per side; use TIFF for larger films").arg(PSD_MAX_SIDE);
        return false;
    }
    for (const PsdSpotChannel& spot : spots) {
        if (spot.film.size() != composite.size()) {
            *error = QObject::tr("spot channel %1 does not match the document size").arg(spot.name);
            return false;
        }
    }
    const bool grey = spots.empty() && isGrey(composite);
    const int colourChannels = grey ? 1 : 3;
    const int channels = colourChannels + static_cast<int>(spots.size());

    // every channel as 8-bit planes, composite first, then the spot channels in order
    std::vector<QImage> planes;  // Grayscale8 per channel
    const QImage rgb = composite.convertToFormat(QImage::Format_RGB32);  // flattened: PSD composite has no alpha here
    for (int c = 0; c < colourChannels; c++) {
        QImage plane(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; y++) {
            const QRgb* src = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
            uchar* dst = plane.scanLine(y);
            for (int x = 0; x < w; x++) {
                dst[x] = static_cast<uchar>(grey ? qGray(src[x]) : (c == 0 ? qRed(src[x]) : c == 1 ? qGreen(src[x]) : qBlue(src[x])));
            }
        }
        planes.push_back(plane);
    }
    for (const PsdSpotChannel& spot : spots) {
        planes.push_back(spot.film.convertToFormat(QImage::Format_Grayscale8));  // 0 = full ink, as Photoshop stores it
    }

    BigEndian out;
    // header
    out.tag("8BPS");
    out.u16(1);  // PSD (2 would be PSB)
    for (int i = 0; i < 6; i++) out.u8(0);
    out.u16(static_cast<uint16_t>(channels));
    out.u32(static_cast<uint32_t>(h));
    out.u32(static_cast<uint32_t>(w));
    out.u16(8);                   // bits per channel
    out.u16(grey ? 1 : 3);        // Grayscale or RGB
    out.u32(0);                   // colour mode data: none

    // image resources
    const size_t resourcesAt = out.size();
    out.u32(0);
    {
        // 1005 ResolutionInfo: fixed-point 16.16 pixels per inch, displayed in inches
        const uint32_t fixed = static_cast<uint32_t>(std::lround(dpi * 65536.0));
        BigEndian r;
        r.u32(fixed); r.u16(1); r.u16(1);
        r.u32(fixed); r.u16(1); r.u16(1);
        resource(out, 1005, r.bytes);
    }
    if (!spots.empty()) {
        // 1006 channel names as Pascal strings (older readers), 1045 as Unicode (current Photoshop)
        BigEndian pascal, unicode;
        for (const PsdSpotChannel& spot : spots) {
            const QByteArray latin = spot.name.toLatin1().left(255);
            pascal.u8(static_cast<uint8_t>(latin.size()));
            pascal.put(reinterpret_cast<const uint8_t*>(latin.constData()), static_cast<size_t>(latin.size()));
            unicode.u32(static_cast<uint32_t>(spot.name.size() + 1));
            for (const QChar ch : spot.name) unicode.u16(ch.unicode());
            unicode.u16(0);
        }
        resource(out, 1006, pascal.bytes);
        resource(out, 1045, unicode.bytes);
        // 1077 DisplayInfo: version, then per channel an RGB colour (16-bit components), opacity 0-100, kind 2 = spot
        BigEndian display;
        display.u32(1);
        for (const PsdSpotChannel& spot : spots) {
            display.u16(0);  // RGB colour space
            display.u16(static_cast<uint16_t>(qRed(spot.ink) * 257));
            display.u16(static_cast<uint16_t>(qGreen(spot.ink) * 257));
            display.u16(static_cast<uint16_t>(qBlue(spot.ink) * 257));
            display.u16(0);
            display.u16(100);  // solidity
            display.u8(2);     // spot channel
        }
        resource(out, 1077, display.bytes);
    }
    out.patch32(resourcesAt, static_cast<uint32_t>(out.size() - resourcesAt - 4));

    out.u32(0);  // layer and mask information: none, the document is flat

    // image data: PackBits, a table of every row's compressed length (channel by channel), then the rows
    out.u16(1);
    const size_t countsAt = out.size();
    for (int i = 0; i < channels * h; i++) out.u16(0);
    std::vector<uint8_t> packed;
    for (int c = 0; c < channels; c++) {
        for (int y = 0; y < h; y++) {
            packed.clear();
            packBitsRow(planes[static_cast<size_t>(c)].constScanLine(y), static_cast<size_t>(w), packed);
            out.patch16(countsAt + 2 * (static_cast<size_t>(c) * h + y), static_cast<uint16_t>(packed.size()));
            out.put(packed.data(), packed.size());
        }
        planes[static_cast<size_t>(c)] = QImage();  // release as we go
    }

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

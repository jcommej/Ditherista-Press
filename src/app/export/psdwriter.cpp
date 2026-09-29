#include "psdwriter.h"
#include <QObject>
#include <QSaveFile>
#include <cmath>
#include <functional>

namespace {

class BigEndian {
    /* PSD is big-endian throughout */
public:
    std::vector<uint8_t> bytes;
    void u8(const uint8_t v) { bytes.push_back(v); }
    void u16(const uint16_t v) { u8(static_cast<uint8_t>(v >> 8)); u8(v & 0xff); }
    void u32(const uint32_t v) { u16(static_cast<uint16_t>(v >> 16)); u16(v & 0xffff); }
    void put(const uint8_t* data, const size_t n) { bytes.insert(bytes.end(), data, data + n); }
    void put(const std::vector<uint8_t>& data) { put(data.data(), data.size()); }
    void tag(const char* four) { put(reinterpret_cast<const uint8_t*>(four), 4); }
    void patch32(const size_t at, const uint32_t v) {
        bytes[at] = static_cast<uint8_t>(v >> 24); bytes[at + 1] = static_cast<uint8_t>(v >> 16);
        bytes[at + 2] = static_cast<uint8_t>(v >> 8); bytes[at + 3] = static_cast<uint8_t>(v);
    }
    void pad(const size_t from, const size_t multiple) { while ((size() - from) % multiple) u8(0); }
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

struct Plane {
    /* one channel, RLE-compressed: the row lengths and the packed rows */
    std::vector<uint16_t> rowLengths;
    std::vector<uint8_t> data;
};

using RowSource = std::function<void(int y, uint8_t* row)>;

Plane compressPlane(const int width, const int height, const RowSource& source) {
    Plane plane;
    std::vector<uint8_t> row(static_cast<size_t>(width));
    std::vector<uint8_t> packed;
    for (int y = 0; y < height; y++) {
        source(y, row.data());
        packed.clear();
        packBitsRow(row.data(), row.size(), packed);
        plane.rowLengths.push_back(static_cast<uint16_t>(packed.size()));
        plane.data.insert(plane.data.end(), packed.begin(), packed.end());
    }
    return plane;
}

RowSource fromImage(const QImage& image, const int channel) {
    /* channel 0/1/2 = R/G/B of an RGB32 image, -1 = grey of a Grayscale8 image */
    return [&image, channel](const int y, uint8_t* row) {
        if (channel < 0) {
            std::copy(image.constScanLine(y), image.constScanLine(y) + image.width(), row);
            return;
        }
        const QRgb* px = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x++) {
            row[x] = static_cast<uint8_t>(channel == 0 ? qRed(px[x]) : channel == 1 ? qGreen(px[x]) : qBlue(px[x]));
        }
    };
}

RowSource constant(const int width, const uint8_t value) {
    return [width, value](int, uint8_t* row) { std::fill(row, row + width, value); };
}

void resource(BigEndian& out, const uint16_t id, const std::vector<uint8_t>& data) {
    /* one image resource block: signature, id, empty Pascal name (padded to even), size, data (padded to even) */
    out.tag("8BIM");
    out.u16(id);
    out.u8(0);
    out.u8(0);
    out.u32(static_cast<uint32_t>(data.size()));
    out.put(data);
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

struct Layer {
    QString name;
    const char* blend;          // four-character blend key
    std::vector<int16_t> ids;   // channel ids: -1 transparency, 0 red, 1 green, 2 blue
    std::vector<Plane> planes;
};

void writeLayerSection(BigEndian& out, const std::vector<Layer>& layers, const int width, const int height) {
    /* Layer and mask information: layer records bottom to top, then every layer's channel data */
    const size_t sectionAt = out.size();
    out.u32(0);
    const size_t infoAt = out.size();
    out.u32(0);
    out.u16(static_cast<uint16_t>(layers.size()));
    for (const Layer& layer : layers) {
        out.u32(0); out.u32(0);  // top, left
        out.u32(static_cast<uint32_t>(height)); out.u32(static_cast<uint32_t>(width));  // bottom, right
        out.u16(static_cast<uint16_t>(layer.ids.size()));
        for (size_t c = 0; c < layer.ids.size(); c++) {
            out.u16(static_cast<uint16_t>(layer.ids[c]));
            const Plane& p = layer.planes[c];
            out.u32(static_cast<uint32_t>(2 + 2 * p.rowLengths.size() + p.data.size()));  // compression + table + rows
        }
        out.tag("8BIM");
        out.tag(layer.blend);
        out.u8(255);  // opacity
        out.u8(0);    // clipping: base
        out.u8(0);    // flags: visible, transparency not locked
        out.u8(0);    // filler
        const size_t extraAt = out.size();
        out.u32(0);
        out.u32(0);   // no layer mask
        out.u32(0);   // no blending ranges
        const QByteArray latin = layer.name.toLatin1().left(255);
        const size_t nameAt = out.size();
        out.u8(static_cast<uint8_t>(latin.size()));
        out.put(reinterpret_cast<const uint8_t*>(latin.constData()), static_cast<size_t>(latin.size()));
        out.pad(nameAt, 4);  // Pascal name padded to a multiple of 4
        // Unicode name ('luni'), which current Photoshop shows
        out.tag("8BIM");
        out.tag("luni");
        const size_t luniAt = out.size();
        out.u32(0);
        out.u32(static_cast<uint32_t>(layer.name.size()));
        for (const QChar ch : layer.name) out.u16(ch.unicode());
        out.pad(luniAt + 4, 4);
        out.patch32(luniAt, static_cast<uint32_t>(out.size() - luniAt - 4));
        out.patch32(extraAt, static_cast<uint32_t>(out.size() - extraAt - 4));
    }
    for (const Layer& layer : layers) {
        for (const Plane& p : layer.planes) {
            out.u16(1);  // RLE
            for (const uint16_t length : p.rowLengths) out.u16(length);
            out.put(p.data);
        }
    }
    out.pad(infoAt + 4, 2);
    out.patch32(infoAt, static_cast<uint32_t>(out.size() - infoAt - 4));
    out.u32(0);  // no global layer mask
    out.patch32(sectionAt, static_cast<uint32_t>(out.size() - sectionAt - 4));
}

}  // namespace

bool writePsd(const QString& path, const QImage& composite, const std::vector<PsdSpotChannel>& inks, const double dpi,
              QString* error, const PsdInkLayout layout, const bool additive) {
    const int w = composite.width();
    const int h = composite.height();
    if (w > PSD_MAX_SIDE || h > PSD_MAX_SIDE) {
        *error = QObject::tr("PSD is limited to %1 px per side; use TIFF for larger films").arg(PSD_MAX_SIDE);
        return false;
    }
    for (const PsdSpotChannel& ink : inks) {
        if (ink.film.size() != composite.size()) {
            *error = QObject::tr("ink %1 does not match the document size").arg(ink.name);
            return false;
        }
    }
    const bool withLayers = !inks.empty() && layout != PsdInkLayout::SpotChannels;
    const bool withSpots = !inks.empty() && layout != PsdInkLayout::Layers;
    const bool grey = inks.empty() && isGrey(composite);
    const int colourChannels = grey ? 1 : 3;
    const int channels = colourChannels + (withSpots ? static_cast<int>(inks.size()) : 0);

    std::vector<QImage> films;  // black/white as Grayscale8: 0 = ink
    for (const PsdSpotChannel& ink : inks) {
        films.push_back(ink.film.convertToFormat(QImage::Format_Grayscale8));
    }
    const QImage rgb = composite.convertToFormat(grey ? QImage::Format_Grayscale8 : QImage::Format_RGB32);

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
    if (withSpots) {
        // 1006 channel names as Pascal strings (older readers), 1045 as Unicode (current Photoshop)
        BigEndian pascal, unicode;
        for (const PsdSpotChannel& ink : inks) {
            const QByteArray latin = ink.name.toLatin1().left(255);
            pascal.u8(static_cast<uint8_t>(latin.size()));
            pascal.put(reinterpret_cast<const uint8_t*>(latin.constData()), static_cast<size_t>(latin.size()));
            unicode.u32(static_cast<uint32_t>(ink.name.size() + 1));
            for (const QChar ch : ink.name) unicode.u16(ch.unicode());
            unicode.u16(0);
        }
        resource(out, 1006, pascal.bytes);
        resource(out, 1045, unicode.bytes);
        // 1077 DisplayInfo: version, then per channel an RGB colour (16-bit components), opacity 0-100, kind 2 = spot
        BigEndian display;
        display.u32(1);
        for (const PsdSpotChannel& ink : inks) {
            display.u16(0);
            display.u16(static_cast<uint16_t>(qRed(ink.ink) * 257));
            display.u16(static_cast<uint16_t>(qGreen(ink.ink) * 257));
            display.u16(static_cast<uint16_t>(qBlue(ink.ink) * 257));
            display.u16(0);
            display.u16(100);
            display.u8(2);
        }
        resource(out, 1077, display.bytes);
    }
    out.patch32(resourcesAt, static_cast<uint32_t>(out.size() - resourcesAt - 4));

    // layers: a background (white paper, or a black garment for RGB inks), then one layer per ink
    if (withLayers) {
        std::vector<Layer> layers;
        const uint8_t background = additive ? 0 : 255;
        layers.push_back({additive ? QObject::tr("Garment") : QObject::tr("Paper"), "norm", {-1, 0, 1, 2},
                          {compressPlane(w, h, constant(w, 255)), compressPlane(w, h, constant(w, background)),
                           compressPlane(w, h, constant(w, background)), compressPlane(w, h, constant(w, background))}});
        for (size_t i = 0; i < inks.size(); i++) {
            const QImage& film = films[i];
            const RowSource alpha = [&film](const int y, uint8_t* row) {  // opaque where the film has ink
                const uchar* src = film.constScanLine(y);
                for (int x = 0; x < film.width(); x++) row[x] = src[x] < 128 ? 255 : 0;
            };
            const QRgb ink = inks[i].ink;
            layers.push_back({inks[i].name, additive ? "scrn" : "mul ", {-1, 0, 1, 2},
                              {compressPlane(w, h, alpha), compressPlane(w, h, constant(w, static_cast<uint8_t>(qRed(ink)))),
                               compressPlane(w, h, constant(w, static_cast<uint8_t>(qGreen(ink)))),
                               compressPlane(w, h, constant(w, static_cast<uint8_t>(qBlue(ink))))}});
        }
        writeLayerSection(out, layers, w, h);
    } else {
        out.u32(0);  // layer and mask information: none, the document is flat
    }

    // the document's own image: composite channels, then spot channels; RLE, row lengths table first
    std::vector<Plane> planes;
    for (int c = 0; c < colourChannels; c++) {
        planes.push_back(compressPlane(w, h, fromImage(rgb, grey ? -1 : c)));
    }
    if (withSpots) {
        for (const QImage& film : films) {
            planes.push_back(compressPlane(w, h, fromImage(film, -1)));  // 0 = full ink, as Photoshop stores it
        }
    }
    out.u16(1);
    for (const Plane& p : planes) for (const uint16_t length : p.rowLengths) out.u16(length);
    for (const Plane& p : planes) out.put(p.data);

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

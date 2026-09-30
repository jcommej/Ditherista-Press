#include "separation.h"
#include "libdither.h"
#include <QObject>
#include <algorithm>
#include <cmath>

std::vector<InkChannel> channelsFor(const SeparationMode mode) {
    switch (mode) {
        case SeparationMode::CMYK:
            return {{QObject::tr("Cyan"), qRgb(0, 255, 255)}, {QObject::tr("Magenta"), qRgb(255, 0, 255)},
                    {QObject::tr("Yellow"), qRgb(255, 255, 0)}, {QObject::tr("Black"), qRgb(0, 0, 0)}};
        case SeparationMode::RGB:
            return {{QObject::tr("Red"), qRgb(255, 0, 0)}, {QObject::tr("Green"), qRgb(0, 255, 0)},
                    {QObject::tr("Blue"), qRgb(0, 0, 255)}};
        case SeparationMode::Composite:
        case SeparationMode::Palette:
        default:
            return {};
    }
}

std::vector<InkChannel> paletteInks(const std::vector<QRgb>& palette) {
    std::vector<InkChannel> inks;
    for (size_t i = 0; i < palette.size(); i++) {
        const QString hex = QString("%1").arg(palette[i] & 0xFFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
        inks.push_back({QString("%1_%2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(hex), palette[i] | 0xFF000000u});
    }
    return inks;
}

std::vector<QImage> splitByPalette(const QImage& dithered, const std::vector<QRgb>& palette,
                                   const std::vector<bool>& wanted) {
    const QImage image = dithered.convertToFormat(QImage::Format_ARGB32);
    const auto opaque = [](const QRgb c) { return c | 0xFF000000u; };  // palette alpha is not part of the ink
    std::vector<QImage> films(palette.size());
    for (size_t i = 0; i < palette.size(); i++) {
        if (i < wanted.size() && wanted[i]) {
            films[i] = QImage(image.size(), QImage::Format_RGB32);
            films[i].fill(Qt::white);
        }
    }
    for (int y = 0; y < image.height(); y++) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x++) {
            if (qAlpha(row[x]) == 0) {
                continue;  // transparent: no ink
            }
            // the first entry of this colour takes the pixel, so a repeated colour does not print twice
            const auto entry = std::find_if(palette.begin(), palette.end(),
                                            [&](const QRgb c) { return opaque(c) == opaque(row[x]); });
            if (entry != palette.end()) {
                QImage& film = films[static_cast<size_t>(entry - palette.begin())];
                if (!film.isNull()) {
                    reinterpret_cast<QRgb*>(film.scanLine(y))[x] = qRgb(0, 0, 0);
                }
            }
        }
    }
    return films;
}

std::vector<std::vector<float>> separate(const QImage& srgbImage, const SeparationMode mode) {
    const QImage image = srgbImage.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    const size_t n = static_cast<size_t>(w) * h;
    const size_t channels = channelsFor(mode).size();
    std::vector<std::vector<float>> planes(channels, std::vector<float>(n, 0.0f));
    for (int y = 0; y < h; y++) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; x++) {
            const size_t i = static_cast<size_t>(y) * w + x;
            const float r = qRed(row[x]) / 255.0f;
            const float g = qGreen(row[x]) / 255.0f;
            const float b = qBlue(row[x]) / 255.0f;
            if (mode == SeparationMode::CMYK) {
                const float k = 1.0f - std::max({r, g, b});
                planes[3][i] = k;
                if (k < 1.0f) {  // pure black: no coloured ink at all
                    planes[0][i] = (1.0f - r - k) / (1.0f - k);
                    planes[1][i] = (1.0f - g - k) / (1.0f - k);
                    planes[2][i] = (1.0f - b - k) / (1.0f - k);
                }
            } else if (mode == SeparationMode::RGB) {
                planes[0][i] = r;
                planes[1][i] = g;
                planes[2][i] = b;
            }
        }
    }
    return planes;
}

QImage coverageToDitherSource(const std::vector<float>& coverage, const QImage& alphaFrom) {
    const QImage alpha = alphaFrom.convertToFormat(QImage::Format_ARGB32);
    const int w = alpha.width();
    const int h = alpha.height();
    // ImageHashMono decodes sRGB to linear light, and ditherers leave white a fraction of the pixels equal to that
    // light: encode 1 - coverage so the decode lands on it (to within the 8-bit step of the encoded value).
    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; y++) {
        const QRgb* a = reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; x++) {
            const double light = 1.0 - std::clamp(static_cast<double>(coverage[static_cast<size_t>(y) * w + x]), 0.0, 1.0);
            const int v = static_cast<int>(std::lround(gamma_encode(light) * 255.0));
            o[x] = qRgba(v, v, v, qAlpha(a[x]));
        }
    }
    return out;
}

void cleanExtremes(QImage& film, const std::vector<float>& coverage) {
    film = film.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < film.height(); y++) {
        QRgb* row = reinterpret_cast<QRgb*>(film.scanLine(y));
        for (int x = 0; x < film.width(); x++) {
            const float c = coverage[static_cast<size_t>(y) * film.width() + x];
            if (qAlpha(row[x]) == 0) {
                continue;  // transparent stays transparent
            }
            if (c <= 0.0f) {
                row[x] = qRgb(255, 255, 255);
            } else if (c >= 1.0f) {
                row[x] = qRgb(0, 0, 0);
            }
        }
    }
}

QImage compositeFromFilms(const std::vector<QImage>& films, const SeparationMode mode) {
    return compositeFromFilms(films, channelsFor(mode), mode == SeparationMode::RGB);
}

QImage compositeFromFilms(const std::vector<QImage>& films, const std::vector<InkChannel>& inks, const bool additive) {
    if (films.empty() || films.size() != inks.size()) {
        return {};
    }
    // disabled inks come as null images: they print nothing
    const auto first = std::find_if(films.begin(), films.end(), [](const QImage& f) { return !f.isNull(); });
    if (first == films.end()) {
        return {};
    }
    const int w = first->width();
    const int h = first->height();
    std::vector<QImage> argb;
    for (const QImage& f : films) {
        argb.push_back(f.isNull() ? QImage() : f.convertToFormat(QImage::Format_ARGB32));
    }
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; y++) {
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; x++) {
            int r = additive ? 0 : 255, g = r, b = r;
            for (size_t c = 0; c < argb.size(); c++) {
                if (argb[c].isNull()) {
                    continue;
                }
                const QRgb p = reinterpret_cast<const QRgb*>(argb[c].constScanLine(y))[x];
                if (qAlpha(p) == 0 || qRed(p) != 0) {
                    continue;  // white or transparent on the film: no ink here
                }
                const QRgb ink = inks[c].ink;
                if (additive) {
                    r = std::max(r, qRed(ink)); g = std::max(g, qGreen(ink)); b = std::max(b, qBlue(ink));
                } else {
                    r = r * qRed(ink) / 255; g = g * qGreen(ink) / 255; b = b * qBlue(ink) / 255;
                }
            }
            o[x] = qRgb(r, g, b);
        }
    }
    return out;
}

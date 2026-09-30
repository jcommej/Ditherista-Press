#include "colorspace.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

constexpr double XN = 0.95047;  // D65 reference white
constexpr double YN = 1.0;
constexpr double ZN = 1.08883;
constexpr double EPSILON = 216.0 / 24389.0;
constexpr double KAPPA = 24389.0 / 27.0;

double decode(const double c) {  // sRGB -> linear
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double encode(const double c) {  // linear -> sRGB; negative values stay negative, to report them out of gamut
    if (c <= 0.0031308) {
        return 12.92 * c;
    }
    return 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double f(const double t) {
    return t > EPSILON ? std::cbrt(t) : (KAPPA * t + 16.0) / 116.0;
}

double fInverse(const double t) {
    const double t3 = t * t * t;
    return t3 > EPSILON ? t3 : (116.0 * t - 16.0) / KAPPA;
}

}  // namespace

Lab srgbToLab(const double r, const double g, const double b) {
    const double lr = decode(r), lg = decode(g), lb = decode(b);
    const double x = 0.4124564 * lr + 0.3575761 * lg + 0.1804375 * lb;
    const double y = 0.2126729 * lr + 0.7151522 * lg + 0.0721750 * lb;
    const double z = 0.0193339 * lr + 0.1191920 * lg + 0.9503041 * lb;
    const double fx = f(x / XN), fy = f(y / YN), fz = f(z / ZN);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

Lab rgbToLab(const QRgb rgb) {
    return srgbToLab(qRed(rgb) / 255.0, qGreen(rgb) / 255.0, qBlue(rgb) / 255.0);
}

void labToSrgb(const Lab& lab, double* r, double* g, double* b) {
    const double fy = (lab.L + 16.0) / 116.0;
    const double fx = fy + lab.a / 500.0;
    const double fz = fy - lab.b / 200.0;
    const double x = XN * fInverse(fx);
    const double y = YN * (lab.L > KAPPA * EPSILON ? fy * fy * fy : lab.L / KAPPA);
    const double z = ZN * fInverse(fz);
    // inverse of the matrix in srgbToLab
    *r = encode(3.2404542 * x - 1.5371385 * y - 0.4985314 * z);
    *g = encode(-0.9692660 * x + 1.8760108 * y + 0.0415560 * z);
    *b = encode(0.0556434 * x - 0.2040259 * y + 1.0572252 * z);
}

QRgb labToRgb(const Lab& lab) {
    double r, g, b;
    labToSrgb(lab, &r, &g, &b);
    const auto byte = [](const double c) { return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); };
    return qRgb(byte(r), byte(g), byte(b));
}

bool inSrgbGamut(const Lab& lab, const double tolerance) {
    double r, g, b;
    labToSrgb(lab, &r, &g, &b);
    const auto inside = [tolerance](const double c) { return c >= -tolerance && c <= 1.0 + tolerance; };
    return inside(r) && inside(g) && inside(b);
}

Lab clampToSrgbGamut(const Lab& lab) {
    Lab in{std::clamp(lab.L, 0.0, 100.0), lab.a, lab.b};
    if (inSrgbGamut(in)) {
        return in;
    }
    // the neutral axis is always inside: bisect the chroma between it and the requested colour
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 30; i++) {
        const double mid = (low + high) / 2.0;
        if (inSrgbGamut({in.L, in.a * mid, in.b * mid})) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return {in.L, in.a * low, in.b * low};
}

bool parseHexColour(const QString& text, QRgb* rgb) {
    static const QRegularExpression pattern("^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    QString digits = match.captured(1);
    if (digits.size() == 3) {  // #RGB: each digit doubled
        digits = QString("%1%1%2%2%3%3").arg(digits[0]).arg(digits[1]).arg(digits[2]);
    }
    const uint value = digits.right(6).toUInt(nullptr, 16);  // AARRGGBB: the alpha is dropped
    *rgb = qRgb(static_cast<int>((value >> 16) & 0xFF), static_cast<int>((value >> 8) & 0xFF), static_cast<int>(value & 0xFF));
    return true;
}

QString hexColour(const QRgb rgb) {
    return QString("#%1").arg(rgb & 0xFFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
}

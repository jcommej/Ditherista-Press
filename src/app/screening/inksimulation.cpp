#include "inksimulation.h"
#include <algorithm>
#include <cmath>

namespace InkSimulation {

namespace {
// CIE 1931 2-degree colour matching functions and CIE illuminant D65, 380..730 nm every 10 nm (CIE tables)
constexpr double CMF_X[BANDS] = {
    0.001368, 0.004243, 0.01431, 0.04351, 0.13438, 0.2839, 0.34828, 0.3362, 0.2908, 0.19536, 0.09564, 0.03201,
    0.0049, 0.0093, 0.06327, 0.1655, 0.2904, 0.43345, 0.5945, 0.7621, 0.9163, 1.0263, 1.0622, 1.0026, 0.85445,
    0.6424, 0.4479, 0.2835, 0.1649, 0.0874, 0.04677, 0.0227, 0.0113592, 0.00579035, 0.00289933, 0.00143997};
constexpr double CMF_Y[BANDS] = {
    3.9e-05, 0.00012, 0.000396, 0.00121, 0.004, 0.0116, 0.023, 0.038, 0.06, 0.09098, 0.13902, 0.20802, 0.323, 0.503,
    0.71, 0.862, 0.954, 0.99495, 0.995, 0.952, 0.87, 0.757, 0.631, 0.503, 0.381, 0.265, 0.175, 0.107, 0.061, 0.032,
    0.017, 0.00821, 0.004102, 0.002091, 0.001047, 0.00052};
constexpr double CMF_Z[BANDS] = {
    0.00645, 0.02005, 0.06785, 0.2074, 0.6456, 1.3856, 1.74706, 1.77211, 1.6692, 1.28764, 0.81295, 0.46518, 0.272,
    0.1582, 0.07825, 0.04216, 0.0203, 0.00875, 0.0039, 0.0021, 0.00165, 0.0011, 0.0008, 0.00034, 0.00019, 5e-05,
    2e-05, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr double D65[BANDS] = {
    49.9755, 54.6482, 82.7549, 91.486, 93.4318, 86.6823, 104.865, 117.008, 117.812, 114.861, 115.923, 108.811,
    109.354, 107.802, 104.79, 107.689, 104.405, 104.046, 100, 96.3342, 95.788, 88.6856, 90.0062, 89.5991, 87.6987,
    83.2886, 83.6992, 80.0268, 80.2146, 82.2778, 78.2842, 69.7213, 71.6091, 74.349, 61.604, 69.8856};
// XYZ (D65) to linear sRGB, IEC 61966-2-1
constexpr double XYZ_TO_SRGB[3][3] = {{3.2404542, -1.5371385, -0.4985314},
                                      {-0.9692660, 1.8760108, 0.0415560},
                                      {0.0556434, -0.2040259, 1.0572252}};
constexpr double MIN_REFLECTANCE = 1e-4;  // LHTSS keeps 0 < reflectance < 1: targets are kept inside
constexpr double MAX_REFLECTANCE = 1.0 - 1e-4;

using Matrix = std::array<std::array<double, BANDS>, 3>;

const Matrix& rhoToLinearSrgb() {
    /* T: reflectance to linear sRGB under D65, each row scaled so that a perfect white gives 1, 1, 1 */
    static const Matrix t = []() {
        double norm = 0.0;
        for (int i = 0; i < BANDS; i++) {
            norm += CMF_Y[i] * D65[i];
        }
        Matrix m{};
        for (int r = 0; r < 3; r++) {
            double sum = 0.0;
            for (int i = 0; i < BANDS; i++) {
                const double x = CMF_X[i] * D65[i] / norm, y = CMF_Y[i] * D65[i] / norm, z = CMF_Z[i] * D65[i] / norm;
                m[r][i] = XYZ_TO_SRGB[r][0] * x + XYZ_TO_SRGB[r][1] * y + XYZ_TO_SRGB[r][2] * z;
                sum += m[r][i];
            }
            for (int i = 0; i < BANDS; i++) {
                m[r][i] /= sum;
            }
        }
        return m;
    }();
    return t;
}

double toLinear(const int channel) {
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

int toEncoded(const double linear) {
    const double l = std::clamp(linear, 0.0, 1.0);
    return static_cast<int>(std::lround(255.0 * (l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055)));
}

template <size_t N>
bool solve(std::array<std::array<double, N + 1>, N>& a) {
    /* Gauss-Jordan with partial pivoting on an augmented N x (N + 1) system; the solution in column N */
    for (size_t c = 0; c < N; c++) {
        size_t pivot = c;
        for (size_t r = c + 1; r < N; r++) {
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        }
        if (std::abs(a[pivot][c]) < 1e-300) return false;
        std::swap(a[c], a[pivot]);
        for (size_t r = 0; r < N; r++) {
            if (r == c || a[r][c] == 0.0) continue;
            const double f = a[r][c] / a[c][c];
            for (size_t k = c; k <= N; k++) a[r][k] -= f * a[c][k];
        }
    }
    for (size_t r = 0; r < N; r++) a[r][N] /= a[r][r];
    return true;
}

double kubelkaMunk(const double infinite, const double scattering, const double below) {
    /* reflectance of a layer (R infinity, S X) over a background of reflectance `below` */
    const double a = (1.0 / infinite + infinite) / 2.0;
    const double b = (1.0 / infinite - infinite) / 2.0;
    const double bsx = b * scattering;
    const double coth = bsx > 30.0 ? 1.0 : 1.0 / std::tanh(bsx);
    return (1.0 - below * (a - b * coth)) / (a - below + b * coth);
}
}  // namespace

Spectrum reflectanceFromSrgb(const QRgb colour) {
    /* LHTSS: reflectance = (tanh z + 1) / 2, the z of least slope sum (z[i+1] - z[i])^2 under T reflectance = rgb.
     * Newton's method on the Lagrange conditions D z + diag(r') T' lambda = 0, T r(z) = rgb. */
    constexpr size_t N = BANDS + 3;
    const Matrix& t = rhoToLinearSrgb();
    const double target[3] = {std::clamp(toLinear(qRed(colour)), MIN_REFLECTANCE, MAX_REFLECTANCE),
                              std::clamp(toLinear(qGreen(colour)), MIN_REFLECTANCE, MAX_REFLECTANCE),
                              std::clamp(toLinear(qBlue(colour)), MIN_REFLECTANCE, MAX_REFLECTANCE)};
    std::array<double, BANDS> z{};
    std::array<double, 3> lambda{};
    for (int iteration = 0; iteration < 60; iteration++) {
        std::array<double, BANDS> rho{}, d1{}, d2{}, tl{};
        for (int i = 0; i < BANDS; i++) {
            const double th = std::tanh(z[i]);
            rho[i] = (th + 1.0) / 2.0;
            d1[i] = (1.0 - th * th) / 2.0;
            d2[i] = -(1.0 - th * th) * th;
            tl[i] = t[0][i] * lambda[0] + t[1][i] * lambda[1] + t[2][i] * lambda[2];
        }
        std::array<std::array<double, N + 1>, N> system{};
        double residual = 0.0;
        for (int i = 0; i < BANDS; i++) {
            const double dz = (i > 0 && i < BANDS - 1 ? 2.0 : 1.0) * z[i] - (i > 0 ? z[i - 1] : 0.0) - (i < BANDS - 1 ? z[i + 1] : 0.0);
            const double f = dz + d1[i] * tl[i];
            residual += f * f;
            system[i][i] = (i > 0 && i < BANDS - 1 ? 2.0 : 1.0) + d2[i] * tl[i];
            if (i > 0) system[i][i - 1] = -1.0;
            if (i < BANDS - 1) system[i][i + 1] = -1.0;
            for (int r = 0; r < 3; r++) {
                system[i][BANDS + r] = d1[i] * t[r][i];
                system[BANDS + r][i] = t[r][i] * d1[i];
            }
            system[i][N] = -f;
        }
        for (int r = 0; r < 3; r++) {
            double f = -target[r];
            for (int i = 0; i < BANDS; i++) f += t[r][i] * rho[i];
            residual += f * f;
            system[BANDS + r][N] = -f;
        }
        if (residual < 1e-24 || !solve<N>(system)) {
            break;
        }
        for (int i = 0; i < BANDS; i++) z[i] += system[i][N];
        for (int r = 0; r < 3; r++) lambda[r] += system[BANDS + r][N];
    }
    Spectrum reflectance{};
    for (int i = 0; i < BANDS; i++) {
        reflectance[i] = (std::tanh(z[i]) + 1.0) / 2.0;
    }
    return reflectance;
}

QRgb srgbFromReflectance(const Spectrum& reflectance) {
    const Matrix& t = rhoToLinearSrgb();
    double rgb[3] = {0.0, 0.0, 0.0};
    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < BANDS; i++) rgb[r] += t[r][i] * reflectance[i];
    }
    return qRgb(toEncoded(rgb[0]), toEncoded(rgb[1]), toEncoded(rgb[2]));
}

Ink inkFromPrint(const QRgb printed, const double opacity, const Spectrum& paper) {
    /* the layer whose single pass on `paper` reflects the printed colour: per band, the R infinity that gives it */
    Ink ink;
    const double o = std::clamp(opacity, 0.0, 1.0);
    ink.scattering = 0.01 + 6.0 * o * o;  // S X: from a thin filter to a layer that hides its background
    const Spectrum target = reflectanceFromSrgb(printed);
    for (int i = 0; i < BANDS; i++) {
        double low = 1e-6, high = 1.0 - 1e-6;  // the reflectance over the paper grows with R infinity
        for (int step = 0; step < 60; step++) {
            const double mid = (low + high) / 2.0;
            (kubelkaMunk(mid, ink.scattering, paper[i]) < target[i] ? low : high) = mid;
        }
        ink.infinite[i] = (low + high) / 2.0;
    }
    return ink;
}

Spectrum overlay(const Ink& ink, const Spectrum& below) {
    Spectrum out{};
    for (int i = 0; i < BANDS; i++) {
        out[i] = kubelkaMunk(ink.infinite[i], ink.scattering, below[i]);
    }
    return out;
}

QRgb print(const std::vector<Ink>& inks, const std::vector<int>& layers, const Spectrum& paper) {
    Spectrum light = paper;
    for (const int layer : layers) {
        light = overlay(inks[static_cast<size_t>(layer)], light);
    }
    return srgbFromReflectance(light);
}

}  // namespace InkSimulation

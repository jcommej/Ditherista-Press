#pragma once
#ifndef PALETTETHEMES_H
#define PALETTETHEMES_H

#include <QString>
#include <array>

/* Palette themes: ready-made settings of the existing reduction of the picture's own colours (palette source
 * "reduced"), from very few colours to many. A theme is nothing more than those settings - colour count, reduction
 * method, black and white kept - so choosing one fills the fields, and the theme shown is simply the one matching
 * the fields (Custom when none does). */
struct PaletteTheme {
    const char* name;
    int colours;
    int reduction;       // index of the reduction combo: 0 Median Cut, 1 Wu, 2 KD-Tree
    bool keepBlackWhite; // black and white are two of the colours: strong contrasts for the small palettes
    const char* toolTip;
};

inline constexpr std::array<PaletteTheme, 5> PALETTE_THEMES = {{
    {"Ristretto", 3, 0, true,
     "3 colours: black, white and one more. Large flat masses and hard contrasts - very simple pictures, or a "
     "print with very few inks."},
    {"Serré", 4, 0, true,
     "4 colours: black, white and two more. Minimal, a little more nuance than Ristretto."},
    {"Filtre", 7, 1, true,
     "7 colours: black, white and five more. The all-round compromise between simplicity and nuance."},
    {"Assemblage", 11, 1, false,
     "11 colours from the picture. More detail and smoother transitions, for richer pictures."},
    {"Grand Cru", 32, 1, false,
     "32 colours from the picture. Keeps as many nuances and details as possible."},
}};

// index in PALETTE_THEMES of the theme with these settings (unique, RGB and CMY off), -1 when none matches
inline int matchingPaletteTheme(const int colours, const int reduction, const bool blackWhite, const bool unique,
                                const bool rgb, const bool cmy) {
    if (unique || rgb || cmy) {
        return -1;
    }
    for (size_t i = 0; i < PALETTE_THEMES.size(); i++) {
        const PaletteTheme& theme = PALETTE_THEMES[i];
        if (theme.colours == colours && theme.reduction == reduction && theme.keepBlackWhite == blackWhite) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

#endif // PALETTETHEMES_H

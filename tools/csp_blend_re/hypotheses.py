"""Candidate blend formulas for the two CSP modes, plus alpha models and
gamma spaces. All math is on float values in 0..255.

A "candidate" is (formula, space, alpha_model) - the analyzer evaluates every
combination against the measured cells and reports residuals.
"""

from __future__ import annotations

import numpy as np

import grid_layout as G


# ---------------------------------------------------------------------------
# gamma spaces
# ---------------------------------------------------------------------------

def _round(x: np.ndarray) -> np.ndarray:
    return np.rint(x).clip(0, 255)


def space_identity(bg, fg):
    return bg, fg


def space_srgb_lin(bg, fg):
    return G.srgb_to_lin(bg) * 255.0, G.srgb_to_lin(fg) * 255.0


def space_gamma22(bg, fg):
    return G.gamma22_to_lin(bg) * 255.0, G.gamma22_to_lin(fg) * 255.0


def back_identity(out):
    return out


def back_srgb_lin(out):
    return G.lin_to_srgb(out / 255.0)


def back_gamma22(out):
    return G.lin_to_gamma22(out / 255.0)


SPACES = {
    "gamma": (space_identity, back_identity),
    "srgb_lin": (space_srgb_lin, back_srgb_lin),
    "gamma22": (space_gamma22, back_gamma22),
}


# ---------------------------------------------------------------------------
# formulas: f(bg, fg) -> out (numpy arrays, 0..255 float), unrounded
# ---------------------------------------------------------------------------

def add_clamp(bg, fg):
    return np.minimum(bg + fg, 255.0)


def add_hdr_max(bg, fg):
    s = bg + fg
    m = s.max(axis=-1, keepdims=True)
    over = m > 255.0
    out = np.where(over, s * 255.0 / np.maximum(m, 1e-9), s)
    return out


def add_fgscale(bg, fg):
    s = bg + fg
    return np.minimum(s, 255.0) * 255.0 / (255.0 + fg)


def add_norm_sum(bg, fg):
    s = bg + fg
    tot = s.sum(axis=-1, keepdims=True)
    m = s.max(axis=-1, keepdims=True)
    over = m > 255.0
    return np.where(over, s * 255.0 / np.maximum(tot, 1e-9), s)


def add_min_luma_scale(bg, fg):
    """Soft 'glow': add, then compress highlights by scaling with luma."""
    s = bg + fg
    m = s.max(axis=-1, keepdims=True)
    over = m > 255.0
    luma = s @ np.array([G.LUM_R, G.LUM_G, G.LUM_B])
    k = 1.0 + 2.0 * luma / 255.0
    return np.where(over, s / k[..., None], s)


def dodge(bg, fg):
    """Color dodge as CSP implements it (measured): min(bg/(1-fg), 1) computed
    with INTEGER division - out = (bg*255)//max(255-fg,1), truncated - with
    the fg=255 case yielding 255 only when bg>0, else 0 (0/0 -> 0).
    Photoshop's dodge gives 255 at fg=1 regardless of bg; CSP does not."""
    div = np.maximum(255.0 - fg, 1.0)
    return np.minimum(np.floor(bg * 255.0 / div), 255.0)


def dodge_inv(bg, fg):
    """Inverse dodge: 1 - (1-bg)/fg; fg=0 -> black."""
    fg0 = fg <= 0.01
    out = 255.0 - (255.0 - bg) * 255.0 / np.maximum(fg, 1e-9)
    return np.where(fg0, 0.0, np.maximum(out, 0.0))


def dodge_hdr(bg, fg):
    """Dodge without clamping, then HDR-normalize so max channel = 255."""
    fg255 = fg >= 254.99
    s = bg * 255.0 / np.maximum(255.0 - fg, 1e-9)
    m = s.max(axis=-1, keepdims=True)
    over = m > 255.0
    out = np.where(over, s * 255.0 / np.maximum(m, 1e-9), s)
    return np.where(fg255, 255.0, out)


def dodge_lum_fg(bg, fg):
    """Dodge driven by foreground LUMA (non-separable)."""
    lum = (fg @ np.array([G.LUM_R, G.LUM_G, G.LUM_B]))[..., None]
    return dodge(bg, np.broadcast_to(lum, fg.shape))


def dodge_lum_bg(bg, fg):
    """Dodge of background LUMA per channel (non-separable)."""
    lum = (bg @ np.array([G.LUM_R, G.LUM_G, G.LUM_B]))[..., None]
    return dodge(np.broadcast_to(lum, bg.shape), fg)


def dodge_soft(bg, fg):
    """Softer dodge: bg/(1-fg) with reduced peak (fits fg=0..1 curves)."""
    fg255 = fg >= 254.99
    div = np.maximum(255.0 - 0.9 * fg, 1e-9)
    out = bg * 255.0 / div
    return np.where(fg255, 255.0, np.minimum(out, 255.0))


def dodge_addmix(bg, fg):
    """1:1 mix of dodge and add (a plausible 'glow' blend)."""
    return 0.5 * (dodge(bg, fg) + add_clamp(bg, fg))


CANDIDATES = {
    "add_glow": {
        "add_clamp": add_clamp,
        "add_hdr_max": add_hdr_max,
        "add_fgscale": add_fgscale,
        "add_norm_sum": add_norm_sum,
        "add_min_luma_scale": add_min_luma_scale,
    },
    "glow_dodge": {
        "dodge": dodge,
        "dodge_inv": dodge_inv,
        "dodge_hdr": dodge_hdr,
        "dodge_soft": dodge_soft,
        "dodge_addmix": dodge_addmix,
        "dodge_lum_fg": dodge_lum_fg,
        "dodge_lum_bg": dodge_lum_bg,
    },
}

# formulas that are defined in a specific space (no gamma variant) - kept
# distinct from applying a generic formula in another space
FORCED_SPACE = {}


# ---------------------------------------------------------------------------
# alpha models
# ---------------------------------------------------------------------------

def model_blend_then_composite(f, bg, fg, alpha):
    """out = a*F(bg,fg) + (1-a)*bg  (blend first, then alpha-composite)."""
    blended = np.rint(f(bg, fg)).clip(0, 255)
    a = alpha[..., None].astype(np.float64) / 255.0
    return _round(a * blended + (1.0 - a) * bg)


def model_premultiplied(f, bg, fg, alpha):
    """out = F(bg, a*fg)  (layer color premultiplied before blending)."""
    fgp = np.rint(fg * (alpha[..., None].astype(np.float64) / 255.0)).clip(0, 255)
    return _round(f(bg, fgp))


MODELS = {
    "blend_then_composite": model_blend_then_composite,
    "premultiplied": model_premultiplied,
}


def evaluate(formula_name, space_name, model_name, bg, fg, alpha):
    """Full pipeline: space transform -> formula -> inverse -> alpha model."""
    formula = None
    for cands in CANDIDATES.values():
        if formula_name in cands:
            formula = cands[formula_name]
    assert formula is not None, formula_name
    if formula_name in FORCED_SPACE:
        space_name = FORCED_SPACE[formula_name]
    to_lin, from_lin = SPACES[space_name]

    def f_wrapped(b, g):
        if space_name == "gamma":
            return formula(b, g)
        bl, gl = to_lin(b, g)
        return from_lin(formula(bl, gl))

    model = MODELS[model_name]
    return model(f_wrapped, np.asarray(bg, float), np.asarray(fg, float),
                 np.asarray(alpha, float))


def all_candidates(mode: str):
    """Yield (name, formula_name, space_name, model_name) for every combo."""
    for fname in CANDIDATES[mode]:
        spaces = [FORCED_SPACE[fname]] if fname in FORCED_SPACE else list(SPACES)
        for sname in spaces:
            for mname in MODELS:
                yield f"{fname} [{sname}] {mname}", fname, sname, mname

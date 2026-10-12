"""Deterministic, offline Milky Way radiance integration; no image input.

Lengths are kpc, emissivities are solar V luminosities per pc^3, and output is
V-zero-point-relative flux per steradian. Templates approximate luminosity-
weighted stellar populations, not a complete population-synthesis simulation.
"""

import numpy as np
from scipy.special import erfc
from bake_stars import cie_fit

SUN_RADIUS = 8.2
SUN_HEIGHT = 0.020
SUN_MV = 4.83
RESOLVED_LIMIT = 6.5
# IAU J2000 equatorial -> Galactic rotation. See Hipparcos/Gaia conventions.
EQ_TO_GAL = np.array(
    [
        [-0.0548755604, -0.8734370902, -0.4838350155],
        [0.4941094279, -0.4448296300, 0.7469822445],
        [-0.8676661490, -0.1980763734, 0.4559837762],
    ],
    dtype=np.float64,
)
OBLIQUITY = np.radians(23.4392911)


def world_to_galactic(world):
    # The engine's equatorial vector components are (RA-x, Dec-z, RA-y).
    x, y, z = np.moveaxis(world, -1, 0)
    eq = np.stack(
        (
            x,
            -y * np.sin(OBLIQUITY) + z * np.cos(OBLIQUITY),
            y * np.cos(OBLIQUITY) + z * np.sin(OBLIQUITY),
        ),
        axis=-1,
    )
    return eq @ EQ_TO_GAL.T


def population_density(direction, distance):
    """Smooth disk/bar emissivity. No hash noise, random stars or jitter."""
    d = np.asarray(distance, dtype=np.float64)
    xyz = np.asarray(direction, dtype=np.float64)[:, None, :] * d[None, :, None]
    x = SUN_RADIUS - xyz[..., 0]
    y = xyz[..., 1]
    z = SUN_HEIGHT + xyz[..., 2]
    radius = np.hypot(x, y)
    # Inner taper avoids extrapolating a pure exponential to a central spike.
    taper = 1 - np.exp(-((radius / 1.5) ** 2))
    radial = np.exp((SUN_RADIUS - radius) / 2.6) * taper
    old = 0.040 * radial * np.exp(-np.abs(z) / 0.30)
    young = 0.007 * radial * np.exp(-np.abs(z) / 0.12)
    thick = 0.004 * np.exp((SUN_RADIUS - radius) / 3.6) * np.exp(-np.abs(z) / 0.90)
    # A triaxial old stellar bar, 27 degrees from the Sun-center axis.
    angle = np.radians(27)
    bx = x * np.cos(angle) + y * np.sin(angle)
    by = -x * np.sin(angle) + y * np.cos(angle)
    bar = 4.0 * np.exp(-np.sqrt((bx / 1.5) ** 2 + (by / 0.60) ** 2 + (z / 0.45) ** 2))
    return old + thick + bar, young


def unresolved_fraction(distance, av, limit=RESOLVED_LIMIT):
    """Luminosity-weighted magnitude distribution below the point-star limit.

    Two normal luminosity-weighted components: giants (M_V=1 +/-1.5) and
    dwarfs (M_V=5 +/-1.5). Extinction affects the apparent-magnitude boundary.
    This selection prevents the smooth model from also drawing bright stars.
    """
    cutoff = limit - 5 * np.log10(np.asarray(distance) * 1000) + 5 - av
    return 0.65 * 0.5 * erfc((cutoff - 1) / (1.5 * np.sqrt(2))) + 0.35 * 0.5 * erfc(
        (cutoff - 5) / (1.5 * np.sqrt(2))
    )


def spectral_lut(temperature):
    """CIE-integrated blackbody under CCM89 R_V=3.1 extinction, linear RGB."""
    w = np.arange(380.0, 781.0, 2.0)
    spectrum = w**-5 / np.expm1(1.438776877e7 / (w * temperature))
    y = 1000 / w - 1.82
    a = np.polynomial.polynomial.polyval(
        y, [1, 0.17699, -0.50447, -0.02427, 0.72085, 0.01979, -0.77530, 0.32999]
    )
    b = np.polynomial.polynomial.polyval(
        y, [0, 1.41338, 2.28305, 1.07233, -5.38434, -0.62251, 5.30260, -2.09002]
    )
    curve = a + b / 3.1
    xyz = (
        spectrum[None, :] * 10 ** (-0.4 * np.arange(257)[:, None] / 8 * curve)
    ) @ cie_fit(w)
    transform = np.array(
        [
            [3.2406, -1.5372, -0.4986],
            [-0.9689, 1.8758, 0.0415],
            [0.0557, -0.2040, 1.0570],
        ]
    )
    rgb = np.maximum(xyz @ transform.T, 0)
    return rgb / (rgb[0] @ np.array([0.2126, 0.7152, 0.0722]))


class RadianceModel:
    def __init__(self, steps=96):
        if steps < 32 or steps > 512:
            raise ValueError("Integration steps must be 32..512")
        self.edges = np.geomspace(0.001, 30.0, steps + 1)
        self.distance = np.sqrt(self.edges[:-1] * self.edges[1:])
        self.width = np.diff(self.edges) * 1000  # pc
        self.colors = [spectral_lut(t) for t in (4500, 8500)]

    def integrate(self, direction, av):
        direction = np.asarray(direction, dtype=np.float64)
        if (
            direction.ndim != 2
            or direction.shape[1] != 3
            or not np.isfinite(direction).all()
            or not np.allclose(np.linalg.norm(direction, axis=1), 1, atol=1e-6)
        ):
            raise ValueError("Finite unit sky directions required")
        av = np.asarray(av, dtype=np.float64)
        if (
            av.shape != (len(direction), len(self.distance))
            or not np.isfinite(av).all()
            or (av < 0).any()
        ):
            raise ValueError(
                "Finite nonnegative extinction for every ray/depth required"
            )
        old, young = population_density(direction, self.distance)
        faint = unresolved_fraction(self.distance[None, :], av)
        # n*s^2 ds dOmega times F(10pc)*(10/s)^2; distance-square factors cancel.
        scale = 100 * 10 ** (-0.4 * SUN_MV)
        at = np.clip(av * 8, 0, 255.999999)
        lo = at.astype(np.int32)
        f = at - lo
        radiance = np.zeros((len(direction), 3), dtype=np.float64)
        for density, table in zip((old, young), self.colors):
            energy = density * faint * self.width[None, :] * scale
            for c in range(3):
                transmission = table[lo, c] * (1 - f) + table[lo + 1, c] * f
                radiance[:, c] += np.sum(energy * transmission, axis=1)
        if not np.isfinite(radiance).all() or (radiance < 0).any():
            raise ValueError("Nonfinite model radiance")
        return radiance.astype(np.float32)

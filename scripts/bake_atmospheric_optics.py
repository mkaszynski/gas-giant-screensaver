#!/usr/bin/env python3
"""Offline spectral scattering tables; the runtime never traces crystals or Mie series.

Ice: unpolarized refraction through 60-degree hexagonal-prism side faces,
with Fresnel transmission and isotropic or horizontal crystal orientations.
Water: Lorenz-Mie scattering, averaged over lognormal droplet populations.
All tables integrate a 5778 K illuminant and CIE 1931 observer, and the finite
solar disk. Units are inverse steradians (plate rows: inverse radians before
the runtime vertical orientation distribution). See docs/ATMOSPHERIC_OPTICS.md.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

import miepython
import numpy as np
from numpy.polynomial.legendre import leggauss
from scipy.ndimage import gaussian_filter1d
from bake_stars import cie_fit

WIDTH, HEIGHT = 4096, 67
SUN_RADIUS = np.radians(0.26645)
WAVELENGTHS = np.arange(390.0, 751.0, 15.0)
# Warren & Brandt 2008, visible part of their measured ice-Ih compilation.
ICE_WAVELENGTH = np.array(
    [
        0.39,
        0.40,
        0.42,
        0.44,
        0.46,
        0.48,
        0.50,
        0.52,
        0.54,
        0.56,
        0.58,
        0.60,
        0.62,
        0.64,
        0.66,
        0.68,
        0.70,
        0.72,
        0.74,
        0.76,
    ]
)
ICE_INDEX = np.array(
    [
        1.3203,
        1.3194,
        1.3177,
        1.3163,
        1.3151,
        1.3140,
        1.3130,
        1.3121,
        1.3114,
        1.3106,
        1.3100,
        1.3094,
        1.3088,
        1.3083,
        1.3078,
        1.3073,
        1.3069,
        1.3065,
        1.3060,
        1.3057,
    ]
)


def water_index(wavelength_um):
    """Daimon & Masumura 2007, 20 C Sellmeier fit (CC0 tabulation)."""
    q = np.asarray(wavelength_um) ** 2
    b = np.array([0.5684027565, 0.1726177391, 0.02086189578, 0.1130748688])
    c = np.array([0.005101829712, 0.01821153936, 0.02620722293, 10.69792721])
    return np.sqrt(1 + np.sum(b * q[..., None] / (q[..., None] - c), axis=-1))


def spectral_weights():
    spectrum = WAVELENGTHS**-5 / np.expm1(1.438776877e7 / (WAVELENGTHS * 5778.0))
    xyz = spectrum[:, None] * cie_fit(WAVELENGTHS)
    rgb = (
        xyz
        @ np.array(
            [
                [3.2406, -1.5372, -0.4986],
                [-0.9689, 1.8758, 0.0415],
                [0.0557, -0.2040, 1.0570],
            ]
        ).T
    )
    # White balance the incident solar spectrum; runtime solar transmission
    # supplies its actual sunset color. Keep signed spectral RGB until summed.
    return rgb / rgb.sum(axis=0)


def prism_cdf(n, elevation, deviation, samples=16384):
    """Integrated Fresnel-weighted probability between inverse Snell roots.

    Continuous bin integrals avoid the false rings produced by binned rays,
    including the integrable minimum-deviation caustic.
    """
    ce = np.cos(elevation)
    effective = np.sqrt(n * n - np.sin(elevation) ** 2) / max(1.0e-8, ce)
    if effective >= 2.0:
        return np.zeros_like(deviation)
    critical = np.arcsin(1.0 / effective)
    low = np.arcsin(effective * np.sin(np.pi / 3 - critical))
    incident = np.linspace(low, np.pi / 2, samples + 1)
    first = np.arcsin(np.sin(incident) / effective)
    second = np.pi / 3 - first
    exit_angle = np.arcsin(np.clip(effective * np.sin(second), -1, 1))
    rs1 = (
        (np.cos(incident) - effective * np.cos(first))
        / (np.cos(incident) + effective * np.cos(first))
    ) ** 2
    rp1 = (
        (effective * np.cos(incident) - np.cos(first))
        / (effective * np.cos(incident) + np.cos(first))
    ) ** 2
    rs2 = (
        (effective * np.cos(second) - np.cos(exit_angle))
        / (effective * np.cos(second) + np.cos(exit_angle))
    ) ** 2
    rp2 = (
        (np.cos(second) - effective * np.cos(exit_angle))
        / (np.cos(second) + effective * np.cos(exit_angle))
    ) ** 2
    weight = 0.5 * ((1 - rs1) * (1 - rs2) + (1 - rp1) * (1 - rp2)) * np.cos(incident)
    integral = np.r_[
        0.0, np.cumsum((weight[:-1] + weight[1:]) * 0.5 * np.diff(incident))
    ]
    minimum = 2 * np.arcsin(effective * 0.5) - np.pi / 3
    maximum = np.pi / 2 + low - np.pi / 3
    middle = 0.5 * (np.pi / 3 + np.clip(deviation, minimum, maximum))
    ratio = (effective**2 * 0.75 - 3 * np.sin(middle) ** 2) / (
        2 * (np.cos(2 * middle) - 0.5)
    )
    offset = np.arcsin(np.sqrt(np.clip(ratio, 0.0, 1.0)))
    probability = np.interp(middle + offset, incident, integral) - np.interp(
        middle - offset, incident, integral
    )
    probability = np.where(deviation <= minimum, 0.0, probability)
    return np.where(deviation >= maximum, integral[-1], probability)


def prism_pdf(n, elevation=0.0, samples=16384, width=WIDTH, normalized=True):
    cdf = prism_cdf(n, elevation, np.linspace(0, np.pi, width + 1), samples)
    denominator = max(1.0e-20, cdf[-1]) if normalized else 1.0
    return np.maximum(0.0, np.diff(cdf)) / denominator / (np.pi / width)


def solar_filter(phase, angles):
    """Solid-angle quadrature of the actual uniform solar disk, not a glow."""
    nodes, weights = leggauss(6)
    cos_alpha = 1 - (nodes + 1) * 0.5 * (1 - np.cos(SUN_RADIUS))
    result = np.zeros_like(phase)
    for ca, w in zip(cos_alpha, weights * 0.5):
        sa = np.sqrt(1 - ca * ca)
        for phi in (np.arange(16) + 0.5) * (2 * np.pi / 16):
            theta = np.arccos(
                np.clip(np.cos(angles) * ca + np.sin(angles) * sa * np.cos(phi), -1, 1)
            )
            for channel in range(3):
                result[:, channel] += (
                    w / 16 * np.interp(theta, angles, phase[:, channel])
                )
    return result


def ice_tables(width=WIDTH):
    angles = (np.arange(width) + 0.5) * np.pi / width
    rgb_weights = spectral_weights()
    halo = np.zeros((width, 3))
    plates = np.zeros((63, width, 3))
    # Continuous orientation quadrature avoids discrete skew-ray caustics.
    nodes, orientation_weights = leggauss(256)
    for wavelength, color in zip(WAVELENGTHS, rgb_weights):
        n = np.interp(np.log(wavelength * 0.001), np.log(ICE_WAVELENGTH), ICE_INDEX)
        sphere = np.zeros(width)
        sin_limit = np.sqrt((4 - n * n) / 3)
        for node, weight in zip(nodes, orientation_weights):
            sine = (node + 1) * 0.5 * sin_limit
            elevation = np.arcsin(sine)
            edges = np.linspace(0, np.pi, width + 1)
            planar = np.arccos(
                np.clip((np.cos(edges) - sine * sine) / (1 - sine * sine), -1.0, 1.0)
            )
            cdf = prism_cdf(n, elevation, planar)
            sphere += (
                np.maximum(0.0, np.diff(cdf))
                / (np.pi / width)
                * np.cos(elevation)
                * weight
                * 0.5
                * sin_limit
            )
        sphere /= 2 * np.pi * np.maximum(np.sin(angles), 1.0e-8)
        sphere /= np.sum(sphere * 2 * np.pi * np.sin(angles)) * (np.pi / width)
        halo += sphere[:, None] * color
        low_sun_power = prism_pdf(n, width=width, normalized=False).sum() * (
            np.pi / width
        )
        for h in range(63):
            # Keep the shrinking projected/transmitted energy of high-sun
            # paths; normalizing each elevation independently invents light.
            profile = prism_pdf(n, np.radians(h), width=width, normalized=False)
            plates[h] += (profile * np.cos(np.radians(h)) / low_sun_power)[
                :, None
            ] * color
    halo = solar_filter(np.maximum(halo, 0), angles)
    # A solar disk's marginal displacement variance is R^2/4. Small crystal
    # wobble and solar size also filter the oriented horizontal profiles.
    sigma = np.sqrt(SUN_RADIUS**2 / 4 + np.radians(0.12) ** 2) / (np.pi / width)
    plates = gaussian_filter1d(np.maximum(plates, 0), sigma, axis=1, mode="nearest")
    return np.concatenate((halo[None], plates), axis=0)


def water_table(radius_um, log_sigma, width=WIDTH):
    angles = np.linspace(2 * np.pi / 3, np.pi, width)
    rgb = np.zeros((width, 3))
    # Deterministic Gaussian quadrature in log radius; average scattered
    # power, including droplet cross section, not amplitudes or painted colors.
    nodes, weights = np.polynomial.hermite.hermgauss(5)
    radii = radius_um * np.exp(np.sqrt(2) * log_sigma * nodes)
    weights = weights * radii * radii
    weights /= weights.sum()
    for wavelength, color in zip(WAVELENGTHS, spectral_weights()):
        phase = np.zeros(width)
        for radius, weight in zip(radii, weights):
            x = 2 * np.pi * radius / (wavelength * 0.001)
            phase += weight * miepython.i_unpolarized(
                water_index(wavelength * 0.001), x, np.cos(angles), norm="one"
            )
        rgb += phase[:, None] * color
    return solar_filter(np.maximum(rgb, 0), angles)


def bake(output):
    output.mkdir(parents=True, exist_ok=True)
    ice = ice_tables()
    rows = [*ice]
    for radius, sigma in [(10.0, 0.08), (4.0, 0.15), (250.0, 0.15)]:
        print(f"Computing water distribution r={radius} um", flush=True)
        rows.append(water_table(radius, sigma))
    data = np.ones((HEIGHT, WIDTH, 4), dtype="<f2")
    data[:, :, :3] = np.asarray(rows)
    if not np.isfinite(data).all() or (data < 0).any():
        raise ValueError("Scattering values must be finite nonnegative half floats")
    payload = b"GGOPT01\0" + struct.pack("<II", WIDTH, HEIGHT) + data.tobytes()
    path = output / "phase.ggoptics"
    path.write_bytes(payload)
    manifest = {
        "version": 1,
        "width": WIDTH,
        "height": HEIGHT,
        "format": "little-endian RGBA16F",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "solar_radius_degrees": float(np.degrees(SUN_RADIUS)),
        "wavelengths_nm": WAVELENGTHS.tolist(),
        "illuminant_K": 5778,
        "miepython_version": miepython.__version__,
        "miepython_jit": miepython.USE_JIT,
        "rows": {
            "0": "random ice axes, 0..180 degrees",
            "1..63": "horizontal plates, Sun elevation0..62degrees; absolute azimuth0..180degrees",
            "64": "water10um, lognormal sigma0.08; scattering120..180degrees",
            "65": "water4um, sigma0.15; scattering120..180degrees",
            "66": "rain250um, sigma0.15; scattering120..180degrees",
        },
        "sources": [
            "https://atmos.uw.edu/ice_optical_constants/",
            "https://doi.org/10.1364/AO.46.003811",
            "https://miepython.readthedocs.io/en/latest/api/miepython.core.i_unpolarized.html",
            "https://jcgt.org/published/0002/02/01/",
        ],
        "model_limits": "Geometric side-face ice paths and prescribed crystal/drop populations; no full cloud microphysics, multiple-scattering solver, or polarization.",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {len(payload)} bytes: {path}", flush=True)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=Path("assets/world/optics"))
    bake(p.parse_args().output)

"""Simple spectrum data augmentation for PLS training.

Only physically plausible perturbations are applied:
  * additive Gaussian noise (sensor / shot noise)
  * a constant baseline offset (stray light, dark drift)
  * a small multiplicative gain change (lamp intensity drift)

These do NOT create new chemical information - they only make the model
less sensitive to measurement noise. Real samples are still required.
"""

import numpy as np


def augment_dataset(X, y, copies=5, noise=0.01, baseline=1.0, scale=0.03, seed=42):
    """
    Parameters
    ----------
    X : (n_samples, n_features) intensity matrix
    y : (n_samples,) concentration vector
    copies : how many synthetic variants per real sample
    noise : Gaussian sigma relative to each row's max intensity
    baseline : max absolute offset added to a whole spectrum
    scale : relative multiplicative gain variation (+/- fraction)
    seed : RNG seed for reproducibility

    Returns
    -------
    X_aug, y_aug including the original samples.
    """
    rng = np.random.default_rng(seed)
    X = np.asarray(X, dtype=float)
    y = np.asarray(y, dtype=float)

    X_out = [X]
    y_out = [y]

    row_max = np.maximum(X.max(axis=1, keepdims=True), 1.0)

    for _ in range(copies):
        n = rng.normal(0.0, noise, X.shape) * row_max
        b = rng.uniform(-baseline, baseline, (X.shape[0], 1))
        s = 1.0 + rng.uniform(-scale, scale, (X.shape[0], 1))
        X_out.append(X * s + n + b)
        y_out.append(y.copy())

    return np.vstack(X_out), np.concatenate(y_out)

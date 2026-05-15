# BRDF 2D Minimization

Native llc port of `brdf_2d_minimization.py` from `diff-rendering-slang`.

Run:

```bash
xmake run brdf_2d_minimization examples/brdf_2d_minimization/config.example.json
```

The example loads `resources/diffuse.jpg`, `resources/normal.jpg`, and `resources/roughness.jpg`, crops a JSON-configured full-resolution region, initializes a half-resolution BRDF by 2x2 averaging, and optimizes it with GPU-computed Slang autodiff gradients plus Adam. `full_width` and `full_height` must be even; the bundled config uses a 256x256 crop at `(64, 64)` and optimizes a 128x128 BRDF.

Outputs:

- `brdf_2d_minimization.png`: 2x2 comparison sheet. Top-left is naive downsampled BRDF, top-right is optimized BRDF, bottom-left is full-resolution reference, and bottom-right is `0.5 * abs(reference - optimized)`.
- `brdf_2d_params.png`: 3-column BRDF parameter sheet. Columns are diffuse, normal, roughness; first row is before optimization, second row is after optimization.

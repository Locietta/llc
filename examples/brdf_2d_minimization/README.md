# BRDF 2D Minimization

Native llc port of `brdf_2d_minimization.py` from `diff-rendering-slang`.

Run:

```bash
xmake run brdf_2d_minimization examples/brdf_2d_minimization/config.example.json
```

The example loads `resources/diffuse.jpg`, `resources/normal.jpg`, and `resources/roughness.jpg`, crops the same 128x128 region as the Python script, initializes a 64x64 BRDF by 2x2 averaging, and optimizes it with GPU-computed Slang autodiff gradients plus Adam.

Outputs:

- `brdf_2d_minimization.png`: 2x2 comparison sheet. Top-left is naive downsampled BRDF, top-right is optimized BRDF, bottom-left is full-resolution reference, and bottom-right is `0.5 * abs(reference - optimized)`.
- `brdf_2d_params.png`: 3-column BRDF parameter sheet. Columns are diffuse, normal, roughness; first row is before optimization, second row is after optimization.

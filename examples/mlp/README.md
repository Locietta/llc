# mlp

This example ports the Slang cooperative-vector MLP training sample into the `llc` example layout.

It targets Vulkan on NVIDIA hardware and requires cooperative-vector support at runtime.

Build and run it with:

```pwsh
xmake run mlp
```

You can also pass a JSON config path:

```pwsh
xmake run mlp examples/mlp/config.example.json
```

Supported config fields:

- `iteration_count`
- `report_interval`
- `input_count`
- `random_seed`

Example config:

```json
{
  "iteration_count": 1000,
  "report_interval": 10,
  "input_count": 32,
  "random_seed": 1072
}
```

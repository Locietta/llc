# reduce

This is an example to show how to implement a reduce operation on Compute shader using Slang. 

You can build and run this example using the following command:
```
xmake run reduce [kernel] [backend]
```

Where `kernel` can be one of the following options:
- `naive`: **Default:** A naive implementation of reduce operation.
- `warp`: An optimized implementation using warp-level primitives.

And `backend` can be one of the following options:
- `dx12`: DirectX 12 backend.
- `vk`: **Default:** Vulkan backend.
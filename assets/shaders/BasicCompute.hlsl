// BasicCompute.hlsl
// This shader does nothing but paint a pixel red.

// Define the thread group size (8x8 threads per group)
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // In the future, we will write to a texture here.
    // For now, we just want to see if this compiles!
    
    float4 red = float4(1.0, 0.0, 0.0, 1.0);
}

import Metal

/// Metal shaders compiled at runtime from source, sidestepping the need for
/// Xcode's `metal` compiler (a macOS-only binary) at build time.
enum MetalCheck {
    static let source = """
    #include <metal_stdlib>
    using namespace metal;
    kernel void double_it(device float *data [[buffer(0)]], uint id [[thread_position_in_grid]]) {
        data[id] *= 2.0;
    }
    """

    static func makeLibrary() -> MTLLibrary? {
        guard let device = MTLCreateSystemDefaultDevice() else { return nil }
        return try? device.makeLibrary(source: source, options: nil)
    }
}

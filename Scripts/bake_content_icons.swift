// Offline macOS asset bake. Runtime uses the committed PNG on every platform.
// Render the SVG sources, preserving the exact Figma exports and the custom engine mark.
import AppKit

guard CommandLine.arguments.count == 2 else {
    fatalError("Usage: swift Scripts/bake_content_icons.swift <ContentBrowser asset directory>")
}
let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let names = ["folder", "box", "image", "map", "workflow", "code", "square", "folder-open", "search", "engine"]
let side = 152 // 4x the authored 38 px slot; covers the 1.5x slider at 2x display density.
let width = side * names.count
guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: side,
                                    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                    isPlanar: false, colorSpaceName: .deviceRGB,
                                    bitmapFormat: [],
                                    bytesPerRow: width * 4, bitsPerPixel: 32),
      let context = NSGraphicsContext(bitmapImageRep: bitmap), let pixels = bitmap.bitmapData else {
    fatalError("Could not allocate icon sheet")
}
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = context
context.cgContext.clear(CGRect(x: 0, y: 0, width: width, height: side))
for (index, name) in names.enumerated() {
    guard let image = NSImage(contentsOf: directory.appendingPathComponent(name + ".svg")),
          image.size == NSSize(width: 38, height: 38) else {
        fatalError("Missing or incorrectly sized SVG: " + name)
    }
    image.draw(in: NSRect(x: index * side, y: 0, width: side, height: side),
               from: .zero, operation: .copy, fraction: 1)
}
NSGraphicsContext.restoreGraphicsState()
// Store coverage alone, not unused RGB. The runtime decodes one byte per pixel
// and copies contiguous rows directly into the shared font atlas's alpha plane.
guard let coverage = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: side,
                                      bitsPerSample: 8, samplesPerPixel: 1, hasAlpha: false,
                                      isPlanar: false, colorSpaceName: .deviceWhite,
                                      bitmapFormat: [], bytesPerRow: width, bitsPerPixel: 8),
      let alpha = coverage.bitmapData else {
    fatalError("Could not allocate coverage sheet")
}
for y in 0..<side {
    for x in 0..<width {
        alpha[y * coverage.bytesPerRow + x] = pixels[y * bitmap.bytesPerRow + x * 4 + 3]
    }
}
guard let png = coverage.representation(using: .png, properties: [:]) else { fatalError("Could not encode icon sheet") }
try png.write(to: directory.appendingPathComponent("thumbnails.png"))
print("Baked \(names.count) icon masks, \(width)x\(side)")

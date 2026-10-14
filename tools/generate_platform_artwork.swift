import AppKit
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

// These are optional authoring tools; all generated PNGs are checked into Git.
struct CoreArtwork {
    let slug: String
}

let cores = [
    CoreArtwork(slug: "cps1"),
    CoreArtwork(slug: "cps2"),
    CoreArtwork(slug: "mvs"),
    CoreArtwork(slug: "ncdz")
]

let root = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "data/artwork",
                   isDirectory: true)
let fileManager = FileManager.default

func makeBitmap(width: Int, height: Int, draw: () -> Void) -> NSBitmapImageRep {
    guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width,
        pixelsHigh: height, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
        isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
        let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
        fatalError("Unable to create image bitmap")
    }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    context.imageInterpolation = .high
    draw()
    context.flushGraphics()
    NSGraphicsContext.restoreGraphicsState()
    return bitmap
}

func drawCover(_ image: NSImage, width: Int, height: Int) {
    let targetAspect = CGFloat(width) / CGFloat(height)
    let sourceAspect = image.size.width / image.size.height
    var crop = NSRect(origin: .zero, size: image.size)
    if sourceAspect > targetAspect {
        crop.size.width = image.size.height * targetAspect
        crop.origin.x = (image.size.width - crop.width) / 2
    } else {
        crop.size.height = image.size.width / targetAspect
        crop.origin.y = (image.size.height - crop.height) / 2
    }
    image.draw(in: NSRect(x: 0, y: 0, width: width, height: height),
               from: crop, operation: .copy, fraction: 1.0)
}

// Keep the NJEMU/core logo together and preserve the original lettering and
// neon arcade treatment in both icon formats. Source art is 1666x944 and has
// intentional negative space on the left for the PSP XMB selection icon.
func drawBrandCrop(_ source: NSImage, in destination: NSRect) {
    let crop = NSRect(x: source.size.width * 0.43,
                      y: source.size.height * 0.17,
                      width: source.size.width * 0.54,
                      height: source.size.height * 0.54)
    source.draw(in: destination, from: crop, operation: .copy, fraction: 1)
}

func artworkIcon(_ source: NSImage, width: Int, height: Int) -> NSBitmapImageRep {
    makeBitmap(width: width, height: height) {
        drawCover(source, width: width, height: height)
        let bandHeight = CGFloat(width) * 80 / 144
        let band = NSRect(x: 0, y: (CGFloat(height) - bandHeight) / 2,
                          width: CGFloat(width), height: bandHeight)
        drawBrandCrop(source, in: band)
        NSColor.white.withAlphaComponent(0.65).setStroke()
        NSBezierPath(rect: NSRect(x: 0.5, y: 0.5,
                                 width: CGFloat(width) - 1,
                                 height: CGFloat(height) - 1)).stroke()
    }
}

func render(_ width: Int, _ height: Int, _ draw: () -> Void) -> NSBitmapImageRep {
    makeBitmap(width: width, height: height, draw: draw)
}

func writePNG(_ bitmap: NSBitmapImageRep, to url: URL) throws {
    try fileManager.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    guard let bytes = bitmap.representation(using: .png, properties: [:]) else {
        fatalError("Failed to encode PNG")
    }
    try bytes.write(to: url, options: .atomic)
}

// PS Vita LiveArea requires 8-bit indexed PNGs; some versions reject RGB/RGBA
// sources even when they display correctly in desktop image viewers.
func writeIndexedPNG(_ bitmap: NSBitmapImageRep, to url: URL) throws {
    let width = bitmap.pixelsWide
    let height = bitmap.pixelsHigh
    guard let pixels = bitmap.bitmapData else { fatalError("No pixel data") }
    var indexed = Data(count: width * height)
    indexed.withUnsafeMutableBytes { destination in
        let indices = destination.bindMemory(to: UInt8.self)
        for y in 0..<height {
            for x in 0..<width {
                let offset = y * bitmap.bytesPerRow + x * 4
                let red = Int(pixels[offset]) >> 5
                let green = Int(pixels[offset + 1]) >> 5
                let blue = Int(pixels[offset + 2]) >> 6
                indices[y * width + x] = UInt8((red << 5) | (green << 2) | blue)
            }
        }
    }
    var palette: [UInt8] = []
    for r in 0..<8 {
        for g in 0..<8 {
            for b in 0..<4 {
                palette.append(UInt8((r * 255) / 7))
                palette.append(UInt8((g * 255) / 7))
                palette.append(UInt8((b * 255) / 3))
            }
        }
    }
    guard let colorSpace = CGColorSpace(indexedBaseSpace: CGColorSpaceCreateDeviceRGB(),
                                       last: 255, colorTable: palette) else { fatalError("Indexed color space failed") }
    guard let provider = CGDataProvider(data: indexed as CFData) else { fatalError("Indexed provider failed") }
    guard let image = CGImage(width: width, height: height, bitsPerComponent: 8,
                              bitsPerPixel: 8, bytesPerRow: width, space: colorSpace,
                              bitmapInfo: CGBitmapInfo(rawValue: 0), provider: provider,
                              decode: nil, shouldInterpolate: false,
                              intent: .defaultIntent) else { fatalError("Indexed CGImage failed") }
    try fileManager.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    guard let destination = CGImageDestinationCreateWithURL(url as CFURL,
                              UTType.png.identifier as CFString, 1, nil) else { fatalError("Indexed destination failed") }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Unable to save indexed PNG") }
}

for core in cores {
    let coreRoot = root.appendingPathComponent(core.slug, isDirectory: true)
    let sourceURL = coreRoot.appendingPathComponent("source_1666_944.png")
    guard let source = NSImage(contentsOf: sourceURL) else { fatalError("Missing source \(sourceURL.path)") }

    try writePNG(artworkIcon(source, width: 144, height: 80),
                 to: coreRoot.appendingPathComponent("icon_144_80.png"))
    try writePNG(render(480, 272) { drawCover(source, width: 480, height: 272) },
                 to: coreRoot.appendingPathComponent("pic1_480_272.png"))
    let overlay = render(480, 272) {
        NSGraphicsContext.current!.cgContext.clear(CGRect(x: 0, y: 0, width: 480, height: 272))
        let label = NSAttributedString(string: "OPEN SOURCE ARCADE",
            attributes: [.font: NSFont.monospacedSystemFont(ofSize: 9, weight: .bold),
                         .foregroundColor: NSColor.white.withAlphaComponent(0.88)])
        let rectangle = NSBezierPath(roundedRect: NSRect(x: 338, y: 8, width: 135, height: 23),
                                     xRadius: 5, yRadius: 5)
        NSColor.black.withAlphaComponent(0.62).setFill()
        rectangle.fill()
        label.draw(at: NSPoint(x: 347, y: 14))
    }
    try writePNG(overlay, to: coreRoot.appendingPathComponent("pic0_480_272.png"))

    try writeIndexedPNG(artworkIcon(source, width: 128, height: 128),
                        to: coreRoot.appendingPathComponent("icon_128_128.png"))
    try writeIndexedPNG(render(960, 544) { drawCover(source, width: 960, height: 544) },
                        to: coreRoot.appendingPathComponent("pic0_960_544.png"))
    try writeIndexedPNG(render(840, 500) { drawCover(source, width: 840, height: 500) },
                        to: coreRoot.appendingPathComponent("bg_840_500.png"))
    try writeIndexedPNG(artworkIcon(source, width: 280, height: 158),
                        to: coreRoot.appendingPathComponent("startup_280_158.png"))
    print("Generated PSP and PS Vita artwork for \(core.slug)")
}
